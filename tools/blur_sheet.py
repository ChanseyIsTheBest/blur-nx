#!/usr/bin/env python3
"""blur-nx (2026-11-10): a contact sheet of a run's captures (captura_NNN_SSSSs.png), labelled with their seconds.

  python tools/blur_sheet.py <run folder> [--cols 6] [--width 320] [--out sheet.png] [--from N] [--to N]
"""
import argparse
import glob
import os
import re

from PIL import Image, ImageDraw


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('folder')
    ap.add_argument('--cols', type=int, default=6)
    ap.add_argument('--width', type=int, default=320)
    ap.add_argument('--out')
    ap.add_argument('--from', dest='lo', type=int, default=0)
    ap.add_argument('--to', dest='hi', type=int, default=10 ** 9)
    a = ap.parse_args()
    files = []
    for f in sorted(glob.glob(os.path.join(a.folder, 'captura_*.png'))):
        m = re.search(r'_(\d+)s\.png$', f)
        s = int(m.group(1)) if m else 0
        if a.lo <= s <= a.hi:
            files.append((s, f))
    if not files:
        raise SystemExit('no captures')
    w = a.width
    first = Image.open(files[0][1])
    h = int(first.height * w / first.width)
    rows = (len(files) + a.cols - 1) // a.cols
    sheet = Image.new('RGB', (a.cols * w, rows * (h + 14)), (40, 40, 40))
    d = ImageDraw.Draw(sheet)
    for i, (s, f) in enumerate(files):
        im = Image.open(f).convert('RGB').resize((w, h))
        x, y = (i % a.cols) * w, (i // a.cols) * (h + 14)
        sheet.paste(im, (x, y + 14))
        d.text((x + 3, y + 1), '%d s' % s, fill=(255, 255, 0))
    out = a.out or os.path.join(a.folder, 'sheet.png')
    sheet.save(out)
    print(out, len(files))


if __name__ == '__main__':
    main()
