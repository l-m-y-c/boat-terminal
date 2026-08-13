#!/usr/bin/env python3
"""Convert a PNG to an LVGL v8 TRUE_COLOR_ALPHA C array (RGB565 LE + A8)."""

from __future__ import annotations

import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError as exc:
    raise SystemExit("Install Pillow: pip install pillow") from exc


def rgb565(r: int, g: int, b: int) -> int:
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def main() -> None:
    if len(sys.argv) < 3:
        raise SystemExit(f"Usage: {sys.argv[0]} input.png output_basename")

    src = Path(sys.argv[1])
    base = Path(sys.argv[2])
    img = Image.open(src).convert("RGBA")
    w, h = img.size
    pixels = list(img.getdata())

    name = base.name
    attr = f"LV_ATTRIBUTE_IMG_{name.upper()}"

    lines = [
        f"/* Auto-generated from {src.name} — do not edit by hand */",
        f'#include "{name}.h"',
        "",
        "#if LV_COLOR_DEPTH != 16",
        f"#error {name} requires LV_COLOR_DEPTH 16",
        "#endif",
        "",
        f"const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST {attr} uint8_t {name}_map[] = {{",
    ]

    row: list[str] = []
    for r, g, b, a in pixels:
        c = rgb565(r, g, b)
        row.append(f"0x{c & 0xFF:02x}, 0x{(c >> 8) & 0xFF:02x}, 0x{a:02x},")
        if len(row) >= 8:
            lines.append("  " + " ".join(row))
            row = []
    if row:
        lines.append("  " + " ".join(row))
    lines += [
        "};",
        "",
        f"const lv_img_dsc_t {name} = {{",
        "  .header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA,",
        "  .header.always_zero = 0,",
        "  .header.reserved = 0,",
        f"  .header.w = {w},",
        f"  .header.h = {h},",
        f"  .data_size = {w * h * 3},",
        f"  .data = {name}_map,",
        "};",
        "",
    ]
    base.with_suffix(".c").write_text("\n".join(lines))
    base.with_suffix(".h").write_text(
        f"""/* Auto-generated LVGL v8 image: {src.name} */
#pragma once
#include <lvgl.h>
#ifndef {attr}
#define {attr}
#endif
extern const lv_img_dsc_t {name};
"""
    )
    print(f"Wrote {base}.c ({w}x{h}) and {base}.h")


if __name__ == "__main__":
    main()
