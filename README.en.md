# ESP32-A1 Sound Key

[中文](README.md)

A tone opens a lock on a touchscreen: an **ESP32-A1 EV Board V1.0** captures
audio, and an **ESP32-S31-Korvo-1** receives telemetry over USB and displays an
800×480 Chinese interface. Select 500, 1,000 or 2,000 Hz; a continuous match for
about 0.5 seconds unlocks the display. Touch controls relock and change the target.

**Unlock detection uses software Goertzel on PCM.** A1's built-in Goertzel is an
uncalibrated raw diagnostic and is not the unlock authority. The UI labels this
explicitly.

## Bench result

On 2026-10-09, the original local firmware passed synchronized speaker playback
and UART capture at a 1,000 Hz target:

| Input | Result |
|---|---|
| 750 Hz for 2 s | Remained locked |
| 1,000 Hz for 200 ms | Unlock count unchanged |
| 1,000 Hz for 2 s | Reached 500/500 ms hold and incremented the unlock count |
| Playback stopped | Unlock remained latched |

No new transport error counters or packet gaps occurred during the 22-second
capture. See the [acceptance record](docs/evidence/2026-10-09-acceptance.md).
The 500/2,000 Hz targets and hardware Goertzel have not been acoustically verified.

This public export changes dependency paths and replaces the local UI font with
SIL OFL Noto Sans SC. It has **not been rebuilt or flashed** after export; the
bench result applies to the original local firmware.

## Hardware

- Korvo USB-A Host → A1 USB DEVICE.
- Connect A1's independent POWER input; the tested board used S5=ON, J48 fitted,
  all four microphone bias jumpers fitted, and its configured I²C/I²S jumpers.
- Korvo UART Type-C → computer for logs.
- No microSD is mounted or written. Audio input is mono ADC0, 16 kHz / 16 bit.
- To fully restart A1, disconnect both POWER and USB DEVICE, wait 3 seconds,
  then reconnect POWER and USB DEVICE.

## Build requirements

1. ESP-IDF preview supporting `esp32s31` and its matching toolchain. The bench
   setup used ESP-IDF 6.2 preview commit
   `c712a0dde385d659a1470a136251980d31a70bc1` and GCC 16.
2. The vendor-provided **ESP32-A1 SDK v0.5.0**, including its source, public and
   private headers, and `examples/esp_friends_boards/`.
3. A matching `esp_codec_dev` 2.0.0-beta4 series. The component registry may
   resolve it, or supply its local source path.

The A1 SDK, preview toolchain and prepublication vendor documents are **not
bundled**. Obtain them separately; cloning this repository alone is insufficient
to build the A1 firmware.

After exporting the correct ESP-IDF environment in PowerShell:

```powershell
git clone https://github.com/HwzLoveDz/esp32-a1-sound-key.git
cd esp32-a1-sound-key
$env:ESP32_A1_SDK_ROOT = 'D:\SDK\esp32_a1_v0.5'
# Optional local matching codec component:
# $env:ESP_CODEC_DEV_ROOT = 'D:\SDK\esp_codec_dev'
.\build.ps1 -Target Both
```

Use `-Target A1` or `-Target Korvo` to build one board. In an exported Linux/macOS
ESP-IDF environment, set the same variables and run
`idf.py --preview -DIDF_TARGET=esp32s31 build` in the desired project directory.

Outputs are `a1_device/build/a1_sound_key_device.bin` and
`korvo_host/build/a1_sound_key_host.bin`. The included partition CSVs describe the
tested 16 MB boards. Read each actual device's partition table before flashing.
For an app-only update, use the verified application offset and preserve other
partitions. No automatic flashing tool or prebuilt firmware is included.

## Demo and capture

Select 1,000 Hz, relock, then play the [wrong tone](tones/wrong-750Hz.wav),
[short tone](tones/short-1000Hz-200ms.wav), and
[correct tone](tones/key-1000Hz.wav) near A1's microphone.

Optional bounded synchronized capture on Windows:

```powershell
python -m pip install -r tools/requirements.txt
python tools/acoustic_probe.py --port COM25 --output captures/session-001
```

Use your own Korvo UART port. The tool stops audio and closes serial on exit.
One-second UART snapshots may miss the 200 ms stimulus; the negative result
only establishes that controlled playback did not increase the unlock count.
Snapshots do not measure exact physical unlock latency.

## Licenses

Original application code, tools and generated tones: [Apache-2.0](LICENSE).
Espressif generated board files: Espressif Modified MIT (Espressif products only).
Noto Sans SC and the generated Sound Key UI font: SIL OFL 1.1.
See [third-party notices](THIRD_PARTY_NOTICES.md).
