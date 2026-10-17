#!/usr/bin/env python3
"""Make AnkiNX's hbmenu icon from the project logo: 256x256 JPEG, the size
and format nacptool/elf2nro expect. JPEG has no transparency, so the logo's
transparent rounded corners are filled with the colour of its dark frame.

Usage: logo_to_icon.py <logo.png> <output.jpg>   (needs Pillow; see tools/py_tools.sh)
"""
import sys

from PIL import Image

SIZE = 256


def main(src: str, out: str) -> None:
    logo = Image.open(src).convert("RGBA")
    w, h = logo.size
    # The frame colour just inside the rounded corner, halfway down the edge.
    frame = logo.getpixel((w // 40, h // 2))[:3]
    background = Image.new("RGBA", logo.size, frame + (255,))
    flat = Image.alpha_composite(background, logo).convert("RGB")
    flat.resize((SIZE, SIZE), Image.LANCZOS).save(out, "JPEG", quality=92, optimize=True)
    print(f"{out}: {SIZE}x{SIZE}, corners filled with {frame}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
