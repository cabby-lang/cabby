#!/usr/bin/env python3
"""Draws runtime/game_font.h: a small 8 by 16 font of the printable ASCII
characters, for the game library to write with on a computer where no
font file can be found. Drawn from DejaVu Sans Mono (whose license allows
it), one bit for each pixel.

    python3 tools/make-game-font.py [path/to/DejaVuSansMono.ttf]
"""
import os
import sys
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
path = sys.argv[1] if len(sys.argv) > 1 else "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"
font = ImageFont.truetype(path, 14)
rows = []
for code in range(32, 127):
    im = Image.new("L", (8, 16), 0)
    ImageDraw.Draw(im).text((0, 0), chr(code), fill=255, font=font)
    bits = []
    for y in range(16):
        b = 0
        for x in range(8):
            if im.getpixel((x, y)) > 110:
                b |= 0x80 >> x
        bits.append(b)
    rows.append("\t{ " + ", ".join("0x%02x" % b for b in bits) + " },   /* %r */" % chr(code))
with open(os.path.join(HERE, "..", "runtime", "game_font.h"), "w") as f:
    f.write("/* game_font.h: an 8 by 16 font of the printable ASCII characters, drawn by\n")
    f.write(" * tools/make-game-font.py from DejaVu Sans Mono (Bitstream Vera license). */\n\n")
    f.write("static const unsigned char game_font[95][16] = {\n")
    f.write("\n".join(rows).replace("*/ */", "* /*/"))
    f.write("\n};\n")
print("runtime/game_font.h written")
