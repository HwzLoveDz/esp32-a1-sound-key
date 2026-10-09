# SPDX-FileCopyrightText: 2026 HwzLoveDz
# SPDX-License-Identifier: Apache-2.0
"""Bounded Windows speaker + Korvo UART probe on one monotonic timeline."""
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import time
import wave
import winsound

import serial


ROOT = Path(__file__).resolve().parents[1]
PHASES = (
    (2.0, "wrong-750Hz", "wrong-750Hz.wav"),
    (7.0, "short-1000Hz-200ms", "short-1000Hz-200ms.wav"),
    (12.0, "key-1000Hz", "key-1000Hz.wav"),
)


def decode_status(line):
    match = re.search(r"\((\d+)\).*SOUND_KEY_STATUS:\s*(.*)", line)
    if not match:
        return None
    result = {"host_uptime_ms": int(match[1])}
    for name, value in re.findall(r"(\w+)=(0x[\da-fA-F]+|-?\d+(?:/\d+)?)", match[2]):
        if "/" in value:
            result["held_ms"], result["hold_ms"] = map(int, value.split("/"))
        else:
            result[name] = int(value, 16 if value.startswith("0x") else 10)
    return result if "age_ms" in result else None


def probe(output, port):
    output.mkdir(parents=True, exist_ok=True)
    reader = serial.Serial()
    reader.port, reader.baudrate, reader.timeout = port, 115200, 0.05
    reader.dtr = reader.rts = False
    raw, statuses, phases = bytearray(), [], []
    text_buffer = ""
    next_phase, active_phase, stop_at = 0, None, None
    error = None
    cleanup_errors = []
    baseline = None
    started = time.monotonic()
    started_utc = datetime.now(timezone.utc).isoformat()
    try:
        reader.open()
        reader.reset_input_buffer()
        started = time.monotonic()
        while time.monotonic() - started < 22.0:
            chunk = reader.read(4096)
            raw.extend(chunk)
            text_buffer += chunk.decode("utf-8", "replace")
            lines = text_buffer.split("\n")
            text_buffer = lines.pop()
            elapsed = time.monotonic() - started
            for line in lines:
                status = decode_status(line)
                if status is not None:
                    status["received_ms"] = round(elapsed * 1000)
                    statuses.append(status)
            if active_phase is not None and elapsed >= stop_at:
                winsound.PlaySound(None, 0)
                active_phase["stop_ms"] = round(elapsed * 1000)
                active_phase["stop_utc"] = datetime.now(timezone.utc).isoformat()
                active_phase = None
            if next_phase < len(PHASES) and elapsed >= PHASES[next_phase][0]:
                latest = statuses[-1] if statuses else {}
                if not (latest.get("connected") and latest.get("valid") and
                        not latest.get("pending") and latest.get("target") == 1000 and
                        latest.get("state") != 4 and latest.get("age_ms", 10000) < 250 and
                        elapsed * 1000 - latest.get("received_ms", -10000) < 1500 and
                        len(statuses) >= 2 and latest.get("measurement", 0) > statuses[-2].get("measurement", 0) and
                        not (latest.get("flags", 0) & 7)):
                    raise RuntimeError("Fresh valid 1000 Hz telemetry required before playback")
                if next_phase == 0 and (latest.get("state") == 2 or latest.get("held_ms", 0)):
                    raise RuntimeError("Press relock and wait for zero hold before probing")
                if baseline is None:
                    baseline = latest.copy()
                _, name, filename = PHASES[next_phase]
                with wave.open(str(ROOT / "tones" / filename), "rb") as wav:
                    file_duration_ms = round(wav.getnframes() * 1000 / wav.getframerate())
                phase = dict(name=name, file=filename, start_ms=round(elapsed * 1000),
                             start_utc=datetime.now(timezone.utc).isoformat(),
                             before_count=latest["count"], before_rms=latest["rms"],
                             file_duration_ms=file_duration_ms)
                phases.append(phase)
                print("PLAY", name, flush=True)
                winsound.PlaySound(str(ROOT / "tones" / filename),
                                   winsound.SND_FILENAME | winsound.SND_ASYNC | winsound.SND_NODEFAULT)
                active_phase, stop_at = phase, time.monotonic() - started + 3.4
                next_phase += 1
    except Exception as exc:
        error = f"{type(exc).__name__}: {exc}"
    finally:
        try:
            winsound.PlaySound(None, 0)
        except Exception as exc:
            cleanup_errors.append(f"audio stop: {exc}")
        try:
            reader.close()
        except Exception as exc:
            cleanup_errors.append(f"serial close: {exc}")
        (output / "korvo-runtime.bin").write_bytes(raw)
        (output / "korvo-runtime.log").write_text(raw.decode("utf-8", "replace"), encoding="utf-8")
        continuous = baseline is not None and all(
            s["connected"] and s["valid"] and not s["pending"] and
            s["target"] == 1000 and s["backend"] == baseline["backend"] and
            s["ack"] == baseline["ack"] and s["age_ms"] < 250 and s["state"] != 4 and
            not (s["flags"] & 7) and all(s[k] == baseline[k] for k in ("crc", "header", "tx", "gaps"))
            for s in statuses if s["received_ms"] >= baseline["received_ms"])
        continuous = continuous and all(
            b["host_uptime_ms"] > a["host_uptime_ms"] and b["measurement"] > a["measurement"]
            for a, b in zip(statuses, statuses[1:]))
        for index, phase in enumerate(phases):
            end = phases[index + 1]["start_ms"] if index + 1 < len(phases) else 22000
            window = [s for s in statuses if phase["start_ms"] <= s["received_ms"] < end]
            phase["records"] = len(window)
            phase["states"] = sorted({s["state"] for s in window})
            phase["counts"] = sorted({s["count"] for s in window})
            phase["max_strength"] = max((s["strength"] for s in window), default=None)
            phase["max_rms"] = max((s["rms"] for s in window), default=None)
            attribution_end = phase["start_ms"] + phase["file_duration_ms"] + 1200
            attributed = [s for s in window if s["received_ms"] < attribution_end]
            phase["attribution_end_ms"] = attribution_end
            changes = [s for s in attributed if baseline and s["count"] > baseline["count"]]
            phase["first_count_increase_ms"] = changes[0]["received_ms"] if changes else None
            healthy = bool(window) and all(s["valid"] and not (s["flags"] & 7) for s in window)
            if phase["name"] == "key-1000Hz":
                phase["passed"] = healthy and any(s["state"] == 2 and
                    s["count"] == baseline["count"] + 1 and s["held_ms"] == 500 for s in attributed)
                phase["passed"] = phase["passed"] and all(s["count"] in
                    (baseline["count"], baseline["count"] + 1) for s in window)
            else:
                phase["passed"] = healthy and all(s["state"] != 2 and
                    s["count"] == baseline["count"] for s in window)
                if phase["name"] == "wrong-750Hz":
                    phase["stimulus_observed"] = any(s["state"] == 3 and
                        s["rms"] >= max(180, phase["before_rms"] * 1.5) for s in attributed)
                    phase["passed"] = phase["passed"] and phase["stimulus_observed"]
                else:
                    phase["observation_limit"] = "200 ms stimulus may be missed by 1 s UART snapshots; verify unchanged unlock count after controlled playback"
        result = dict(port=port, started_utc=started_utc, duration_s=time.monotonic() - started,
                      error=error, cleanup_errors=cleanup_errors, continuous=continuous,
                      phases=phases, statuses=statuses,
                      passed=error is None and not cleanup_errors and continuous and
                      len(phases) == 3 and all(p["passed"] for p in phases))
        (output / "acoustic-result.json").write_text(
            json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        print("ACOUSTIC_RESULT", json.dumps({k: v for k, v in result.items() if k != "statuses"}), flush=True)
    if error or cleanup_errors:
        raise RuntimeError(error or "; ".join(cleanup_errors))


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--port", required=True, help="Korvo UART port, e.g. COM25")
    args = parser.parse_args()
    probe(args.output, args.port)
