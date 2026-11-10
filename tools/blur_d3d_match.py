#!/usr/bin/env python3
"""blur-nx (2026-11-10): pairs functions of a reference Xbox 360 image with Blur's by instruction shape, using each
image's .pdata for function starts and lengths (no generated tree needed).

Blur links XDK 2.0.11164's Direct3D, the same build as Guwange (guwange-nx) and A World of Keflings
(worldkeflings-nx), whose ports name their Direct3D functions at their own addresses. A function of the same library
build is the same code in both images except for branch targets and relocated globals, so its shape (each
instruction with branch displacements and the 16-bit immediates of lis / addis / ori / oris / addi / D-form accesses
whose base is r0 or a register a lis set ... simplified here to: every 16-bit immediate kept but for lis/addis and
the instruction right after one that uses its register, and branch displacements removed) is equal.

For Katamari (XDK 2.0.5632) the device words move, so --loose compares opcodes and register fields only (d3d_match's
token) and ranks by difflib ratio.

  python tools/blur_d3d_match.py --ref-image R.bin --names names.txt --image out/renut_image.bin [--loose] [--out F]

names.txt: lines "<name> <hex address> ..." (comments with #). Output: name, ref address, Blur address(es), score.
"""
import argparse
import difflib
import struct
import sys
from collections import defaultdict

BASE = 0x82000000


def sections(img):
    pe = struct.unpack_from('<I', img, 0x3C)[0]
    n = struct.unpack_from('<H', img, pe + 6)[0]
    opt = struct.unpack_from('<H', img, pe + 20)[0]
    s = pe + 24 + opt
    out = []
    for i in range(n):
        name = img[s:s + 8].rstrip(b'\0').decode('ascii', 'replace')
        vsz, va = struct.unpack_from('<II', img, s + 8)
        ch = struct.unpack_from('<I', img, s + 36)[0]
        out.append((name, BASE + va, BASE + va + vsz, ch))
        s += 40
    return out


def register_functions(path, img):
    """Function starts from a generated tree's *_register.cpp (or *_init.cpp); length to the next start, trailing
    zero words dropped."""
    import glob
    import os
    import re
    if os.path.isdir(path):
        cands = glob.glob(os.path.join(path, '*_register.cpp')) or glob.glob(os.path.join(path, '*_init.cpp'))
        path = cands[0]
    starts = sorted({int(m, 16) for m in re.findall(r'0x(8[0-9A-Fa-f]{7})\s*,\s*sub_', open(path).read())})
    funcs = []
    for i, b in enumerate(starts):
        e = starts[i + 1] if i + 1 < len(starts) else b + 4
        n = (e - b) // 4
        while n > 1 and word(img, b + 4 * (n - 1)) == 0:
            n -= 1
        funcs.append((b, n))
    return funcs


def pdata_functions(img):
    secs = sections(img)
    pd = [s for s in secs if s[0] == '.pdata'][0]
    funcs = []
    for off in range(pd[1] - BASE, pd[2] - BASE, 8):
        begin, w = struct.unpack_from('>II', img, off)
        if begin == 0:
            continue
        flen = (w >> 8) & 0x3FFFFF
        funcs.append((begin, flen))
    funcs.sort()
    return funcs, secs


def code_ranges(secs):
    return [(a, b) for (n, a, b, ch) in secs if ch & 0x20]


def word(img, addr):
    return struct.unpack_from('>I', img, addr - BASE)[0]


def shape(img, addr, n, loose):
    out = []
    lis_regs = set()
    for i in range(n):
        w = word(img, addr + 4 * i)
        op = w >> 26
        if op in (16, 18):                          # bc / b: keep the form, drop the displacement
            out.append((op, (w >> 16) & 0x3FF if op == 16 else 0, w & 3))
            lis_regs.clear()
            continue
        rd = (w >> 21) & 31
        ra = (w >> 16) & 31
        if loose:
            if op in (31, 19, 59, 63, 4, 30):
                out.append((op, (w >> 1) & 0x3FF, rd, ra, (w >> 11) & 31))
            else:
                out.append((op, rd, ra))
            continue
        if op == 15 and ra == 0:                    # lis: the global's high half
            out.append((op, rd))
            lis_regs.add(rd)
            continue
        if op in (14, 24, 25, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53,
                  54, 55, 58, 62) and ra in lis_regs:  # low half of a global built by lis
            out.append((op, rd, ra, 'lo'))
            if op == 14 or op == 24:
                lis_regs.add(rd)
            continue
        out.append(w)
        if op not in (36, 37, 38, 39, 44, 45, 52, 53, 54, 55, 62):  # stores keep the lis register live
            lis_regs.discard(rd)
    return tuple(out)


def load_names(path):
    names = []
    for line in open(path, encoding='utf-8', errors='replace'):
        line = line.split('#', 1)[0].strip()
        if not line:
            continue
        p = line.split()
        if len(p) < 2:
            continue
        try:
            a = int(p[1], 16)
        except ValueError:
            continue
        names.append((p[0], a))
    return names


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--ref-image', required=True)
    ap.add_argument('--names', required=True)
    ap.add_argument('--image', required=True)
    ap.add_argument('--loose', action='store_true')
    ap.add_argument('--lo', type=lambda s: int(s, 16), default=0)
    ap.add_argument('--hi', type=lambda s: int(s, 16), default=0xFFFFFFFF)
    ap.add_argument('--ref-gen', help='reference generated tree (function starts); else .pdata')
    ap.add_argument('--gen', help="Blur's generated tree (function starts); else .pdata")
    ap.add_argument('--out')
    a = ap.parse_args()
    ref = open(a.ref_image, 'rb').read()
    img = open(a.image, 'rb').read()
    rfuncs = register_functions(a.ref_gen, ref) if a.ref_gen else pdata_functions(ref)[0]
    gfuncs = register_functions(a.gen, img) if a.gen else pdata_functions(img)[0]
    rlen = dict(rfuncs)
    by_shape = defaultdict(list)
    by_len = defaultdict(list)
    for (b, n) in gfuncs:
        if not (a.lo <= b < a.hi):
            continue
        by_len[n].append(b)
        if not a.loose:
            by_shape[shape(img, b, n, False)].append(b)
    out = []
    for name, ra in load_names(a.names):
        n = rlen.get(ra)
        if n is None:
            out.append(f'{name:44s} {ra:08X}  -          no .pdata entry in the reference')
            continue
        if not a.loose:
            hits = by_shape.get(shape(ref, ra, n, False), [])
            if len(hits) == 1:
                out.append(f'{name:44s} {ra:08X}  {hits[0]:08X}  exact ({n} instructions)')
                continue
            if hits:
                out.append(f'{name:44s} {ra:08X}  {" ".join("%08X" % h for h in hits)}  exact, {len(hits)} candidates')
                continue
        rs = shape(ref, ra, n, True)
        best = []
        for m in range(int(n * 0.7), int(n * 1.3) + 2):
            for b in by_len.get(m, []):
                r = difflib.SequenceMatcher(None, rs, shape(img, b, m, True), autojunk=False)
                if r.real_quick_ratio() < 0.6 or r.quick_ratio() < 0.6:
                    continue
                best.append((r.ratio(), b, m))
        best.sort(reverse=True)
        if best:
            top = best[0]
            nxt = best[1][0] if len(best) > 1 else 0.0
            out.append(f'{name:44s} {ra:08X}  {top[1]:08X}  loose {top[0]:.2f} (next {nxt:.2f}; {n} -> {top[2]} instructions)')
        else:
            out.append(f'{name:44s} {ra:08X}  -          no match')
    text = '\n'.join(out) + '\n'
    if a.out:
        open(a.out, 'w').write(text)
    sys.stdout.write(text)


if __name__ == '__main__':
    main()
