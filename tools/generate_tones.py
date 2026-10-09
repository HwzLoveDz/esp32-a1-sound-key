# SPDX-FileCopyrightText: 2026 HwzLoveDz
# SPDX-License-Identifier: Apache-2.0
"""Generate local demo tones; standard Python only. Does not play audio."""
import math
from pathlib import Path
import struct
import wave

DESTINATION = Path(__file__).resolve().parent.parent / "tones"
RATE = 16000
PEAK = 8191  # approximately -12 dBFS peak; speaker volume is separate.


def write_tone(name, hz, active_seconds, duration=3.0):
    samples = bytearray()
    start = 0.4
    end = start + active_seconds
    for i in range(round(duration * RATE)):
        t = i / RATE
        gain = 0.0
        if start <= t < end:
            gain = min(1.0, (t - start) / 0.01, (end - t) / 0.01)
        value = round(PEAK * gain * math.sin(2 * math.pi * hz * t)) if hz else 0
        samples.extend(struct.pack("<h", value))
    with wave.open(str(DESTINATION / name), "wb") as output:
        output.setnchannels(1)
        output.setsampwidth(2)
        output.setframerate(RATE)
        output.writeframes(samples)


if __name__ == "__main__":
    DESTINATION.mkdir(exist_ok=True)
    for hz in (500, 1000, 2000):
        write_tone(f"key-{hz}Hz.wav", hz, 2.0)
    write_tone("wrong-750Hz.wav", 750, 2.0)
    write_tone("short-1000Hz-200ms.wav", 1000, 0.2)
    write_tone("silence.wav", 0, 0)
    print("Generated 6 local 16 kHz mono PCM WAV demo assets.")
