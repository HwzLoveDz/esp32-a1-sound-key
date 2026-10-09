# Noto Sans SC / Sound Key UI subset

`NotoSansSC.ttf` is the unmodified Noto Sans SC variable font used by the
public export. The complete SIL Open Font License 1.1 and upstream copyright
notice are in [OFL.txt](OFL.txt).

Upstream: [google/fonts — ofl/notosanssc](https://github.com/google/fonts/tree/main/ofl/notosanssc).

`tools/generate_fonts.py` generates the finite **Sound Key UI** glyph subset
as `korvo_host/main/sound_key_fonts.c` and `.h`. These generated font assets
remain under OFL-1.1. The generator code is under the project's Apache-2.0
license. The local bench firmware used a different font; its generated
Microsoft YaHei assets and binaries are not part of this repository.
