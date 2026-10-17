#!/usr/bin/env python3
"""Convert an X11 window dump (xwd, 24/32-bit ZPixmap TrueColor) to PNG.
Usage: xwd2png.py <in.xwd> <out.png>   (needs Pillow; see tools/py_tools.sh)
"""
import struct
import sys

from PIL import Image


def main(src: str, dst: str) -> None:
    data = open(src, "rb").read()
    # XWDFileHeader: 25 big-endian CARD32 fields.
    (header_size, _version, pixmap_format, _depth, width, height, _xoffset, byte_order,
     _unit, _bit_order, _pad, bits_per_pixel, bytes_per_line, _visual_class,
     red_mask, green_mask, blue_mask, _bits_per_rgb, _cmap_entries, ncolors,
     *_rest) = struct.unpack(">25I", data[:100])
    if pixmap_format != 2 or bits_per_pixel not in (24, 32):
        raise SystemExit(f"unsupported xwd: format={pixmap_format} bpp={bits_per_pixel}")
    offset = header_size + ncolors * 12  # skip window name and colormap
    bpp = bits_per_pixel // 8
    endian = "<" if byte_order == 0 else ">"

    def shift(mask: int) -> int:
        return (mask & -mask).bit_length() - 1

    rs, gs, bs = shift(red_mask), shift(green_mask), shift(blue_mask)
    out = bytearray(width * height * 3)
    for y in range(height):
        row = data[offset + y * bytes_per_line: offset + y * bytes_per_line + width * bpp]
        for x in range(width):
            raw = row[x * bpp:(x + 1) * bpp]
            px = struct.unpack(endian + "I", raw if bpp == 4 else raw + b"\0")[0]
            i = (y * width + x) * 3
            out[i] = (px & red_mask) >> rs
            out[i + 1] = (px & green_mask) >> gs
            out[i + 2] = (px & blue_mask) >> bs
    Image.frombytes("RGB", (width, height), bytes(out)).save(dst)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
