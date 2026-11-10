#!/usr/bin/env python3
"""blur-nx (2026-11-10): two captures side by side (reference left, ours right) with a difference image below and the
mean absolute difference per channel, for a scene that both runs show (the frames need not be the same moment).

  python tools/blur_pair.py REF.png OURS.png --out pair.png [--crop x0,y0,x1,y1]
"""
import argparse

from PIL import Image, ImageChops, ImageDraw, ImageStat


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('ref')
    ap.add_argument('ours')
    ap.add_argument('--out', required=True)
    ap.add_argument('--crop')
    a = ap.parse_args()
    r = Image.open(a.ref).convert('RGB')
    o = Image.open(a.ours).convert('RGB').resize(r.size)
    if a.crop:
        box = tuple(int(v) for v in a.crop.split(','))
        r, o = r.crop(box), o.crop(box)
    d = ImageChops.difference(r, o)
    stat = ImageStat.Stat(d)
    w, h = r.size
    sheet = Image.new('RGB', (w * 2, h * 2 + 16), (30, 30, 30))
    sheet.paste(r, (0, 16))
    sheet.paste(o, (w, 16))
    sheet.paste(d.point(lambda v: min(255, v * 4)), (0, h + 16))
    dr = ImageDraw.Draw(sheet)
    dr.text((4, 2), 'reference (emulated GPU, readback resolve)', fill=(255, 255, 0))
    dr.text((w + 4, 2), 'ours (Renut)', fill=(255, 255, 0))
    dr.text((w + 4, h + 20), 'mean |diff| R %.1f G %.1f B %.1f; means ref %s ours %s' % (
        tuple(stat.mean) + (['%.1f' % v for v in ImageStat.Stat(r).mean], ['%.1f' % v for v in ImageStat.Stat(o).mean])),
        fill=(255, 255, 0))
    sheet.save(a.out)
    print(a.out, 'mean |diff|', ['%.2f' % v for v in stat.mean], 'ref', ['%.1f' % v for v in ImageStat.Stat(r).mean],
          'ours', ['%.1f' % v for v in ImageStat.Stat(o).mean])


if __name__ == '__main__':
    main()
