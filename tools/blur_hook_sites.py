#!/usr/bin/env python3
"""blur-nx (2026-11-10): lists every Renut hook site in app/src (BANJO_RENUT_HOOK*, its guest address, its hook
function and the Direct3D name in the line's first comment), for the move from Katamari's addresses (XDK 2.0.5632,
the base) to Blur's (XDK 2.0.11164).

  python tools/blur_hook_sites.py   -> out/base_hook_sites.json, out/base_hook_names.txt ("<name> <address>")
"""
import collections
import json
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HOOK = re.compile(r'(BANJO_RENUT_HOOK[A-Z_]*)\(sub_([0-9A-F]{8})(?:,\s*([A-Za-z0-9_<>]+))?\)[;\s{]*(?://\s*(\S+))?')


def main():
    rows = []
    for dp, _, fs in os.walk(os.path.join(ROOT, 'app', 'src')):
        for f in sorted(fs):
            if not f.endswith(('.cpp', '.h', '.inc')):
                continue
            p = os.path.join(dp, f)
            rel = os.path.relpath(p, ROOT).replace(os.sep, '/')
            for i, line in enumerate(open(p, encoding='utf-8', errors='replace'), 1):
                for m in HOOK.finditer(line):
                    rows.append([rel, i, m.group(1), m.group(2), m.group(3), m.group(4)])
    names = collections.defaultdict(set)
    for r in rows:
        nm = r[5] or (r[4] or '?')
        names[r[3]].add(nm.replace('_hook', ''))
    os.makedirs(os.path.join(ROOT, 'out'), exist_ok=True)
    json.dump(rows, open(os.path.join(ROOT, 'out', 'base_hook_sites.json'), 'w'), indent=0)
    with open(os.path.join(ROOT, 'out', 'base_hook_names.txt'), 'w') as o:
        for a, n in sorted(names.items()):
            o.write('%s %s\n' % (sorted(n)[0], a))
    print(len(rows), 'sites,', len(names), 'addresses')
    print(collections.Counter(r[2] for r in rows))
    print('files:', collections.Counter(r[0] for r in rows).most_common(12))


if __name__ == '__main__':
    main()
