#!/usr/bin/env python3
"""blur-nx (2026-11-10): pairs the guest data addresses Renut hard-codes (Beautiful Katamari's, the base) with Blur's,
through the functions already paired (out/renut_map/hook_map_blur.txt): each function's data references (`lis rX,hi`
then an addi / load / store on rX) in order, Katamari's beside Blur's. katamari-nx's tools/renut_data_refs.py did the
same from N&B; this reads the images directly (function bounds from the generated trees).

  python tools/blur_data_refs.py --kat-image K.bin --kat-gen K/app/generated --image out/renut_image.bin
         --gen app/generated 0x820FA9D0 0x820FAEB4 ...
"""
import argparse
import collections
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from blur_d3d_match import register_functions, word  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def refs(img, addr, n):
    hi = {}
    out = []
    for i in range(n):
        w = word(img, addr + 4 * i)
        op = w >> 26
        rd, ra = (w >> 21) & 31, (w >> 16) & 31
        imm = w & 0xFFFF
        simm = imm - 0x10000 if imm & 0x8000 else imm
        if op == 15 and ra == 0:
            hi[rd] = (imm << 16) & 0xFFFFFFFF
            continue
        if ra in hi and op in (14, 32, 34, 36, 38, 40, 42, 44, 48, 50, 52, 54, 58, 62):
            out.append((hi[ra] + simm) & 0xFFFFFFFF)
            if op == 14 and rd != ra:
                hi[rd] = (hi[ra] + simm) & 0xFFFFFFFF
            continue
        if op == 24 and ra in hi:  # ori
            out.append(hi[ra] | imm)
            continue
        if rd in hi and op not in (36, 38, 44, 52, 54, 62):
            hi.pop(rd, None)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--kat-image', required=True)
    ap.add_argument('--kat-gen', required=True)
    ap.add_argument('--image', required=True)
    ap.add_argument('--gen', required=True)
    ap.add_argument('targets', nargs='+')
    a = ap.parse_args()
    targets = {int(t, 16) for t in a.targets}
    kimg = open(a.kat_image, 'rb').read()
    img = open(a.image, 'rb').read()
    klen = dict(register_functions(a.kat_gen, kimg))
    blen = dict(register_functions(a.gen, img))
    seen = collections.defaultdict(collections.Counter)
    where = collections.defaultdict(list)
    for line in open(os.path.join(ROOT, 'out', 'renut_map', 'hook_map_blur.txt')):
        p = line.split()
        if len(p) < 2 or p[1] == '-':
            continue
        ka, ba = int(p[0], 16), int(p[1], 16)
        if ka not in klen or ba not in blen:
            continue
        kr = refs(kimg, ka, klen[ka])
        br = refs(img, ba, blen[ba])
        if len(kr) != len(br):
            # same count only: an order that differs is not paired by position
            for t in targets:
                if t in kr:
                    where[t].append('%08X/%08X (%d vs %d refs)' % (ka, ba, len(kr), len(br)))
            continue
        for k, b in zip(kr, br):
            if k in targets:
                seen[k][b] += 1
                where[k].append('%08X/%08X' % (ka, ba))
    for t in sorted(targets):
        print('%08X -> %s   in %s' % (t, ' '.join('%08X x%d' % kv for kv in seen[t].most_common()) or '?',
                                      ', '.join(where[t][:6])))


if __name__ == '__main__':
    main()
