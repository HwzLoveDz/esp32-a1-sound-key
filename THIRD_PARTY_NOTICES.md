# Third-party notices

The top-level Apache-2.0 license covers original application code, utility
scripts, documentation and mathematically generated demonstration tones. It
does not replace the licenses of the items below.

| Item | Location | License / provenance |
|---|---|---|
| Espressif Board Manager generated code/configuration | `a1_device/components/gen_bmgr_codes/`, `korvo_host/components/gen_bmgr_codes/` | Original Espressif copyright headers retained. [Espressif Modified MIT](LICENSES/LicenseRef-Espressif-Modified-MIT.txt), for use with Espressif Systems products only. Complete license also included in each generated component. |
| Noto Sans SC variable font | `fonts/NotoSansSC.ttf` | [SIL Open Font License 1.1](fonts/OFL.txt). Original copyright and reserved-font-name notice retained. [Upstream](https://github.com/google/fonts/tree/main/ofl/notosanssc). |
| Sound Key UI generated font subset | `korvo_host/main/sound_key_fonts.c`, `.h` | Derived from bundled Noto Sans SC; OFL-1.1. Generated font name: Sound Key UI. |
| ESP-IDF managed components | Resolved during build; not vendored | Each upstream component's own license applies. Versions/constraints are in the component manifests. |
| ESP32-A1 SDK v0.5.0 / matching codec source | External, obtained separately | Not bundled. The supplied A1 SDK states Apache-2.0; retain its own notices when obtaining or redistributing it. |

The generated font C files were regenerated for this export. Microsoft YaHei
font data and local bench binaries are not distributed here.

The locally archived vendor PDFs, official document screenshot, reference
schematics, SDK archive, toolchain, flash dumps and raw device/process logs are
not part of this repository.
