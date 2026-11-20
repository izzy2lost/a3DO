#!/usr/bin/env python3
# opera-boot (GPL-2.0-only): convert the a3DO logo into the ROM's banner image.
#
# Output layout (all multi-byte fields big-endian, read byte by byte):
#   0   "A3LG"
#   4   width  (u16)
#   6   height (u16)
#   8   palette: 256 x RGB555 (u16); entry 0 is black and is never drawn
#   520 width * height palette indices, row by row
#
# The banner clears the screen to black first, so index 0 is skipped. The logo
# is composited over black before it is quantised, so its soft edges blend
# into the background the way they do in the app.
#
# Needs Pillow. The generated file is committed (assets/logo.bin), so building
# the ROM does not need Python packages; rerun this only when the logo changes.
import argparse
import struct
from pathlib import Path

from PIL import Image

MAX_W, MAX_H = 240, 168


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("logo", type=Path)
    ap.add_argument("out", type=Path)
    ap.add_argument("--height", type=int, default=160)
    args = ap.parse_args()

    src = Image.open(args.logo).convert("RGBA")
    src = src.crop(src.getbbox())
    h = min(args.height, MAX_H)
    w = round(src.width * h / src.height)
    if w > MAX_W:
        w, h = MAX_W, round(src.height * MAX_W / src.width)
    src = src.resize((w, h), Image.LANCZOS)

    black = Image.new("RGBA", src.size, (0, 0, 0, 255))
    flat = Image.alpha_composite(black, src).convert("RGB")
    quant = flat.quantize(colors=255, method=Image.Quantize.MEDIANCUT,
                          dither=Image.Dither.NONE)
    pal = quant.getpalette()[: 255 * 3]
    alpha = src.getchannel("A").load()
    idx = quant.load()

    def rgb555(r, g, b):
        return ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3)

    palette = [0] + [rgb555(*pal[i * 3:i * 3 + 3]) for i in range(len(pal) // 3)]
    palette += [0] * (256 - len(palette))

    pixels = bytearray()
    for y in range(h):
        for x in range(w):
            i = idx[x, y] + 1
            # Nearly transparent, or quantised to black: leave the background.
            if alpha[x, y] < 12 or palette[i] == 0:
                i = 0
            pixels.append(i)

    blob = b"A3LG" + struct.pack(">HH", w, h)
    blob += b"".join(struct.pack(">H", c) for c in palette) + bytes(pixels)
    args.out.write_bytes(blob)
    print(f"{args.out}: {w}x{h}, {len(blob)} bytes")


if __name__ == "__main__":
    main()
