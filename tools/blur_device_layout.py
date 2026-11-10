#!/usr/bin/env python3
"""blur-nx (2026-11-10): measures the Direct3D device layout of Blur (XDK 2.0.11164) against the base's (Beautiful
Katamari, XDK 2.0.5632) from paired functions: every pair's instructions are aligned by shape (opcode and register
fields; difflib), and each aligned load / store / addi with a displacement of --min or more on a base register other
than r1 gives one (Katamari offset, Blur offset) observation. Output: each Katamari offset with the Blur offsets seen
and how often, then the zones (runs of offsets with one delta).

Pairs: out/renut_map/hook_map_blur.txt (tools/blur_hook_map.py) and any "kat blur" lines of --pairs files.

  python tools/blur_device_layout.py --kat-image K.bin --kat-gen K/app/generated --image out/renut_image.bin
         --gen app/generated [--min 0x2A00] [--out out/renut_map/device_layout.txt]
"""
import argparse
import collections
import difflib
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from blur_d3d_match import register_functions, word  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DFORM = {32: 'lwz', 33: 'lwzu', 34: 'lbz', 35: 'lbzu', 36: 'stw', 37: 'stwu', 38: 'stb', 39: 'stbu', 40: 'lhz',
         41: 'lhzu', 42: 'lha', 43: 'lhau', 44: 'sth', 45: 'sthu', 48: 'lfs', 49: 'lfsu', 50: 'lfd', 51: 'lfdu',
         52: 'stfs', 53: 'stfsu', 54: 'stfd', 55: 'stfdu', 14: 'addi'}
DS = {58: 'ld', 62: 'std'}


def decode(img, addr, n):
    out = []
    for i in range(n):
        w = word(img, addr + 4 * i)
        op = w >> 26
        rd, ra = (w >> 21) & 31, (w >> 16) & 31
        disp = None
        if op in DFORM:
            disp = w & 0xFFFF
            if disp & 0x8000:
                disp -= 0x10000
        elif op in DS:
            disp = w & 0xFFFC
            if disp & 0x8000:
                disp -= 0x10000
        if op in (31, 19, 59, 63, 4, 30):
            key = (op, (w >> 1) & 0x3FF, rd, ra, (w >> 11) & 31)
        elif op in (16, 18):
            key = (op,)
        else:
            key = (op, rd, ra)
        out.append((key, op, ra, disp))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--kat-image', required=True)
    ap.add_argument('--kat-gen', required=True)
    ap.add_argument('--image', required=True)
    ap.add_argument('--gen', required=True)
    ap.add_argument('--min', type=lambda v: int(v, 0), default=0x2A00)
    ap.add_argument('--pairs', nargs='*', default=[])
    ap.add_argument('--out', default=os.path.join(ROOT, 'out', 'renut_map', 'device_layout.txt'))
    a = ap.parse_args()
    kimg = open(a.kat_image, 'rb').read()
    img = open(a.image, 'rb').read()
    klen = dict(register_functions(a.kat_gen, kimg))
    blen = dict(register_functions(a.gen, img))
    pairs = set()
    for line in open(os.path.join(ROOT, 'out', 'renut_map', 'hook_map_blur.txt')):
        p = line.split()
        if len(p) >= 2 and p[1] != '-':
            pairs.add((int(p[0], 16), int(p[1], 16)))
    for f in a.pairs:
        for line in open(f):
            p = line.split('#')[0].split()
            if len(p) >= 2:
                pairs.add((int(p[0], 16), int(p[1], 16)))
    obs = collections.defaultdict(collections.Counter)
    where = collections.defaultdict(set)
    for ka, ba in sorted(pairs):
        if ka not in klen or ba not in blen:
            continue
        ki = decode(kimg, ka, klen[ka])
        bi = decode(img, ba, blen[ba])
        sm = difflib.SequenceMatcher(None, [x[0] for x in ki], [x[0] for x in bi], autojunk=False)
        for tag, i1, i2, j1, j2 in sm.get_opcodes():
            if tag != 'equal':
                continue
            for k in range(i2 - i1):
                kx, bx = ki[i1 + k], bi[j1 + k]
                if kx[3] is None or bx[3] is None or kx[2] == 1 or kx[2] == 0:
                    continue
                if kx[3] < a.min and bx[3] < a.min:
                    continue
                if kx[1] == 14 and kx[2] != 3 and kx[3] < 0x2000:
                    continue
                obs[kx[3]][bx[3]] += 1
                where[(kx[3], bx[3])].add('%08X' % ka)
    lines = []
    deltas = []
    for k in sorted(obs):
        c = obs[k]
        best, n = c.most_common(1)[0]
        tot = sum(c.values())
        lines.append('%6d 0x%04X -> %6d 0x%04X  delta %+5d  %d/%d  %s' % (
            k, k & 0xFFFF, best, best & 0xFFFF, best - k, n, tot,
            ' '.join('%d:%d' % (v, m) for v, m in c.most_common()[1:4])))
        deltas.append((k, best - k, n, tot))
    zones = []
    for k, d, n, tot in deltas:
        if n * 2 < tot:
            continue
        if zones and zones[-1][2] == d:
            zones[-1][1] = k
            zones[-1][3] += n
        else:
            zones.append([k, k, d, n])
    lines.append('')
    lines.append('zones (Katamari offset range, delta, observations):')
    for z in zones:
        lines.append('  %6d .. %6d  %+5d  (%d)' % tuple(z))
    os.makedirs(os.path.dirname(a.out), exist_ok=True)
    open(a.out, 'w').write('\n'.join(lines) + '\n')
    print('\n'.join(lines[-len(zones) - 2:]))


if __name__ == '__main__':
    main()
