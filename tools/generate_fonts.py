"""Generate the LVGL 9 UI font subset from the bundled SIL OFL Noto Sans SC.

The generated C assets are checked into this local demo, so firmware builds do
not need Python or a system font. Regenerate after adding Chinese UI text.
This is a finite UI subset, not a general CJK font or a font downloader.
"""
from pathlib import Path
import argparse
import re

from PIL import Image, ImageDraw, ImageFont


def emit_font(size, path, chars):
    font = ImageFont.truetype(str(path), size)
    # The bundled Noto font has a single variable weight axis.
    try:
        font.set_variation_by_axes([400])
    except OSError:
        pass  # A caller may supply an already-static regular font.
    ascent, descent = font.getmetrics()
    codes = sorted({ord(ch) for ch in chars})
    glyphs = [(0, 0, 0, 0, 0, 0)]  # glyph zero is reserved by LVGL
    bitmap = bytearray()
    for code in codes:
        ch = chr(code)
        left, top, right, bottom = font.getbbox(ch, anchor="ls")
        advance = round(font.getlength(ch) * 16)
        image = Image.new("L", (max(1, right - left), max(1, bottom - top)), 0)
        ImageDraw.Draw(image).text((-left, -top), ch, font=font, fill=255, anchor="ls")
        bbox = image.getbbox()
        if bbox:
            x0, y0, x1, y1 = bbox
            cropped = image.crop(bbox)
            width, height = cropped.size
            offset_x = left + x0
            # LVGL draws top = baseline - box_h - ofs_y.
            offset_y = -(top + y1)
            pixels = [value >> 4 for value in cropped.tobytes()]
            packed = bytearray((len(pixels) + 1) // 2)
            for index, value in enumerate(pixels):
                packed[index // 2] |= value << (4 if index % 2 == 0 else 0)
        else:
            width = height = offset_x = offset_y = 0
            packed = b""
        glyphs.append((len(bitmap), advance, width, height, offset_x, offset_y))
        bitmap.extend(packed)
    if len(bitmap) >= (1 << 20):
        raise ValueError("Font exceeds LVGL's default 20-bit bitmap index")
    start = codes[0]
    length = codes[-1] - start + 1
    if length > 65535:
        raise ValueError("Unicode range exceeds the chosen sparse LVGL cmap")
    prefix = f"sk_{size}"
    lines = [f"static const uint8_t {prefix}_bitmap[] = {{"]
    lines.extend("    " + ", ".join(f"0x{v:02x}" for v in bitmap[i:i + 20]) + ","
                 for i in range(0, len(bitmap), 20))
    lines.extend(["};", f"static const lv_font_fmt_txt_glyph_dsc_t {prefix}_glyphs[] = {{"])
    for index, (offset, advance, width, height, x, y) in enumerate(glyphs):
        suffix = "reserved" if not index else f"U+{codes[index - 1]:04X}"
        lines.append(f"    {{.bitmap_index={offset}, .adv_w={advance}, .box_w={width}, "
                     f".box_h={height}, .ofs_x={x}, .ofs_y={y}}}, /* {suffix} */")
    lines.extend(["};", f"static const uint16_t {prefix}_unicode[] = {{"])
    for i in range(0, len(codes), 12):
        lines.append("    " + ", ".join(str(code - start) for code in codes[i:i + 12]) + ",")
    lines.extend([
        "};",
        f"static const lv_font_fmt_txt_cmap_t {prefix}_cmaps[] = {{{{",
        f"    .range_start={start}, .range_length={length}, .glyph_id_start=1,",
        f"    .unicode_list={prefix}_unicode, .glyph_id_ofs_list=NULL,",
        f"    .list_length={len(codes)}, .type=LV_FONT_FMT_TXT_CMAP_SPARSE_TINY,",
        "}};",
        f"static const lv_font_fmt_txt_dsc_t {prefix}_dsc = {{",
        f"    .glyph_bitmap={prefix}_bitmap, .glyph_dsc={prefix}_glyphs, .cmaps={prefix}_cmaps,",
        "    .kern_dsc=NULL, .kern_scale=0, .cmap_num=1, .bpp=4,",
        "    .kern_classes=0, .bitmap_format=LV_FONT_FMT_TXT_PLAIN, .stride=0,",
        "};",
        f"const lv_font_t sound_key_font_{size} = {{",
        "    .get_glyph_dsc=lv_font_get_glyph_dsc_fmt_txt,",
        "    .get_glyph_bitmap=lv_font_get_bitmap_fmt_txt,",
        f"    .line_height={ascent + descent}, .base_line={descent},",
        "    .subpx=LV_FONT_SUBPX_NONE, .kerning=LV_FONT_KERNING_NONE,",
        "    .underline_position=-2, .underline_thickness=1,",
        f"    .dsc=&{prefix}_dsc,",
        "};",
        "",
    ])
    return lines, len(codes), len(bitmap)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--font", type=Path,
                        default=Path(__file__).resolve().parents[1] / "fonts/NotoSansSC.ttf")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    ui_dir = root / "korvo_host/main"
    source = (ui_dir / "sound_key_ui.c").read_text(encoding="utf-8")
    # Keep the exact non-ASCII UI repertoire. ASCII includes formatted metrics.
    non_ascii = "".join(sorted(set(re.findall(r"[^\x00-\x7f]", source))))
    ascii_chars = "".join(chr(i) for i in range(32, 127))
    specs = [(16, ascii_chars + non_ascii), (24, ascii_chars + non_ascii),
             (32, ascii_chars + non_ascii), (48, "0123456789 -Hz")]
    lines = ["/* SPDX-License-Identifier: OFL-1.1",
             " * Sound Key UI font subset generated from Noto Sans SC.",
             " * Copyright 2014-2021 Adobe; original reserved font name: Source.",
             " * See fonts/OFL.txt for the complete license.",
             " * Generated by tools/generate_fonts.py; LVGL 9, 4-bit grayscale UI subset. */",
             "#include \"sound_key_fonts.h\"", "#include <stddef.h>", ""]
    total = 0
    for size, chars in specs:
        font_lines, count, nbytes = emit_font(size, args.font, chars)
        lines.extend(font_lines)
        total += nbytes
        print(f"{size}px: {count} glyphs, {nbytes} bitmap bytes")
    (ui_dir / "sound_key_fonts.c").write_text("\n".join(lines), encoding="utf-8", newline="\n")
    header = ["/* SPDX-License-Identifier: OFL-1.1; see fonts/OFL.txt.",
              " * Generated by tools/generate_fonts.py. */", "#pragma once", "#include \"lvgl.h\"", ""]
    header.extend(f"LV_FONT_DECLARE(sound_key_font_{size})" for size, _ in specs)
    (ui_dir / "sound_key_fonts.h").write_text("\n".join(header) + "\n", encoding="utf-8", newline="\n")
    print(f"Total bitmap bytes: {total}; files saved in {ui_dir}")


if __name__ == "__main__":
    main()
