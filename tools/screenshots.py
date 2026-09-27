#!/usr/bin/env python3
"""Makes the README's screen pictures in docs/ with the firmware's own
drawing code: builds and runs tools/screenshots.c on this computer, then
turns each screen into a PNG, twice the size, in e-paper colours.

    python tools/screenshots.py

Needs a C compiler and Pillow (pip install pillow).
"""
import os
import subprocess
import sys
import tempfile

from PIL import Image, ImageOps

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCALE = 2
PAPER = (236, 236, 230)
INK = (30, 30, 30)
FRAME = (160, 160, 160)


def main():
    out_dir = os.path.join(ROOT, 'docs')
    os.makedirs(out_dir, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        exe = os.path.join(tmp, 'screenshots')
        sources = ['tools/screenshots.c', 'main/bin_calendar.c', 'main/gfx.c',
                   'main/font_bold_12.c', 'main/font_bold_20.c']
        subprocess.run(['cc', '-Wall', '-Itools/host', '-Imain', *sources, '-lm', '-o', exe],
                       cwd=ROOT, check=True)
        pbms = subprocess.run([exe, tmp], check=True, capture_output=True, text=True).stdout.split()
        for pbm in pbms:
            screen = Image.open(pbm).convert('L')  # black 0, white 255
            screen = screen.resize((screen.width * SCALE, screen.height * SCALE), Image.NEAREST)
            png = ImageOps.colorize(screen, INK, PAPER)
            png = ImageOps.expand(png, border=SCALE * 4, fill=PAPER)
            png = ImageOps.expand(png, border=1, fill=FRAME)
            name = os.path.splitext(os.path.basename(pbm))[0] + '.png'
            png.save(os.path.join(out_dir, name), optimize=True)
            print(os.path.join('docs', name))


if __name__ == '__main__':
    sys.exit(main())
