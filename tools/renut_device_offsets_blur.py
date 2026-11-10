#!/usr/bin/env python3
"""blur-nx (2026-11-10), from pacman-nx's tool of 2026-11-02 (the same XDK): Renut's Direct3D device offsets, from
Beautiful Katamari's layout (XDK 2.0.5632, the base) to Blur's (XDK 2.0.11164).

blur-nx: Blur links the same Direct3D build as Guwange and PAC-MAN's disc (77 of guwange-nx's 84 named functions are
the same code in Blur, tools/blur_d3d_match.py), and its own pairs with Katamari (tools/blur_device_layout.py,
out/renut_map/device_layout.txt) give the same deltas where they observe a field: VertexDeclaration 11812->11992,
the render-state block +184, IndexBuffer 12428->12612, InKickOff 13216->13408, CommandBuffer 13492->13728 and the
SwapCount block +236, gamma +1920, BehaviorFlags 22264->24192. The zone N&B 10944..11775 (Katamari -12 / -16) is
delta 0 here, as pacman-nx's table says ("below 11776: 0"). Lines carry `blur-nx: Y->new` after this.

Every literal katamari-nx moved carries its N&B (title update 3) value in the comment: `katamari-nx: X->Y`. The new value
comes from X by the zones below, measured with tools/d3d_device_layout.py on this game's same-shape pairs (N&B: 3,549
functions; Bangai-O 639; Guwange, the same XDK, 1,040: Guwange -> PAC-MAN delta 0 everywhere) and checked against
guwange-nx's own d3d.h constants, which are this XDK's. Masks and sizes on those lines are left alone: only literals
equal to Y are rewritten, and the line's comment says `pacman-nx: Y->new`.

  python -I tools/renut_device_offsets_pacman.py [--dry-run]
"""
import argparse
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GPU = os.path.join(ROOT, 'app', 'src', 'renut_engine', 'gpu')
PAIR = re.compile(r'katamari-nx: ?((?:0x[0-9A-Fa-f]+|\d+)->(?:0x[0-9A-Fa-f]+|\d+)(?:, ?(?:0x[0-9A-Fa-f]+|\d+)->(?:0x[0-9A-Fa-f]+|\d+))*)')
ONE = re.compile(r'(0x[0-9A-Fa-f]+|\d+)->(0x[0-9A-Fa-f]+|\d+)')

# N&B title update 3 -> PAC-MAN. (first, last, delta)
ZONES = [
    (10944, 11775, 0),     # blur-nx: N&B's own values again (Katamari moved them -12 / -16)
    (11776, 13192, 168),   # the declaration 0x2E30->0x2ED8, private render state, index buffer ... predication 0x31AC->0x3254
    (13193, 13227, 176),   # the tiling clear values 13200->13376, 13216->13392, 13220->13396 (guwange-nx draw.cpp)
    (13228, 13255, 176),   # InKickOff 13232->13408 (3 accesses)
    (13256, 13443, 180),   # 13256, 13260 (6 accesses)
    (13444, 13515, 208),   # VerticesRingEnd 0x3484->0x3554, 13504->13712, 13512->13720
    (13516, 24063, 212),   # CommandBuffer 13516->13728 ... BehaviorFlags 23980->24192 (guwange-nx's d3d.h)
]
EXPLICIT = {0x5E00: 0x5F00}  # kDeviceAllocSize: guwange-nx's d3d.h and PAC-MAN's sub_821C2008 `li r3,24320; li r4,128`; Blur's below


def blur(x):
    if x in EXPLICIT:
        return EXPLICIT[x]
    for lo, hi, d in ZONES:
        if lo <= x <= hi:
            return x + d
    return None


def respell(old, new):
    return f'0x{new:X}' if old.lower().startswith('0x') else str(new)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--dry-run', action='store_true')
    a = ap.parse_args()
    changed_lines = unplaced = 0
    report = []
    for d, _, fs in os.walk(GPU):
        for f in fs:
            if not f.endswith(('.cpp', '.h', '.inc')):
                continue
            p = os.path.join(d, f)
            t = open(p, encoding='utf-8', errors='surrogateescape', newline='').read()
            lines = t.split('\n')
            dirty = False
            for i, line in enumerate(lines):
                if 'blur-nx:' in line:
                    continue
                m = PAIR.search(line)
                if not m:
                    continue
                code, sep, comment = line.partition('//')
                notes = []
                for xs, ys in ONE.findall(m.group(1)):
                    x, y = int(xs, 0), int(ys, 0)
                    if x >= 0x10000:  # a guest data address: tools/renut_port_pacman.py
                        continue
                    new = blur(x)
                    if new is None:
                        unplaced += 1
                        report.append(f'NO ZONE {f}:{i + 1} N&B {x} Katamari {y}: {line.strip()[:90]}')
                        continue
                    rx = re.compile(r'(?<![0-9A-Za-z_.])' + re.escape(ys) + r'(?:[uU])?(?![0-9A-Za-z_.])')
                    if rx.search(code):
                        code = rx.sub(respell(ys, new), code, count=1)
                        notes.append(f'{ys}->{respell(ys, new)}')
                    else:
                        report.append(f'NOT IN CODE {f}:{i + 1} {ys}: {code.strip()[:90]}')
                if notes:
                    lines[i] = code + sep + comment.rstrip('\r') + ' (blur-nx: ' + ', '.join(notes) + ')' + ('\r' if line.endswith('\r') else '')
                    dirty = True
                    changed_lines += 1
            if dirty and not a.dry_run:
                open(p, 'w', encoding='utf-8', errors='surrogateescape', newline='').write('\n'.join(lines))
    print(f'{changed_lines} lines rewritten, {unplaced} values with no zone')
    print('\n'.join(report))


if __name__ == '__main__':
    main()
