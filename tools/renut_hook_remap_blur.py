#!/usr/bin/env python3
"""blur-nx (2026-11-10): moves Renut's hook table from Beautiful Katamari's addresses (the base, XDK 2.0.5632) to Blur's
(XDK 2.0.11164), from out/renut_map/hook_map_blur.txt (tools/blur_hook_map.py: the state tables, guwange-nx's and
worldkeflings-nx's names through exact shapes, Katamari's shapes, tools/blur_hook_map_hand.txt). The base's tool for
the same job is tools/renut_hook_remap_katamari.py; this one keeps its forms:
  - a mapped hook gets Blur's address, and its line `// blur-nx: Katamari sub_X, <source>`;
  - an unmapped one becomes BANJO_RENUT_UNHOOKED_<kind>(sub_<Katamari>, ...) (compiled, registered nowhere);
  - a RAW hook's own body calls the original as __imp__sub_<address>: those calls and REX_EXTERNs inside the files
    are renamed with it.
Two hooks may not map to one address: the second is left unhooked and reported. Lines inside `#if
KATAMARI_BANJO_ADDRESS_HOOKS` (N&B's game hooks, not built) are left alone.

  python -I tools/renut_hook_remap_blur.py [--dry-run]
"""
import argparse
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'app', 'src', 'renut_engine', 'gpu')
LINE = re.compile(r'^(\s*)BANJO_RENUT_(HOOK|HOOK_SHARED|HOOK_RAW|HOOK_RAW_SHARED)\((sub_[0-9A-F]{8})([,)])(.*)$')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--dry-run', action='store_true')
    a = ap.parse_args()
    mp = {}
    for line in open(os.path.join(ROOT, 'out', 'renut_map', 'hook_map_blur.txt')):
        p = line.split(None, 3)
        if len(p) < 3:
            continue
        why = p[3].strip() if len(p) > 3 else ''
        mp['sub_' + p[0]] = (None if p[1] == '-' else 'sub_' + p[1], why)
    used = {}
    counts = {'mapped': 0, 'unhooked': 0, 'skipped': 0}
    report = []
    for dp, _, fs in os.walk(SRC):
        for name in sorted(fs):
            if not name.endswith(('.cpp', '.h')):
                continue
            path = os.path.join(dp, name)
            text = open(path, encoding='utf-8', newline='').read()
            if 'BANJO_RENUT_HOOK' not in text:
                continue
            nl = '\r\n' if '\r\n' in text else '\n'
            lines = text.split(nl)
            renames = {}
            changed = False
            skip = 0
            for k, line in enumerate(lines):
                s = line.strip()
                if s.startswith('#if') and 'KATAMARI_BANJO_ADDRESS_HOOKS' in s:
                    skip = 1
                elif skip and s.startswith('#if'):
                    skip += 1
                elif skip and s.startswith('#endif'):
                    skip -= 1
                if skip:
                    if LINE.match(line):
                        counts['skipped'] += 1
                    continue
                m = LINE.match(line)
                if not m:
                    continue
                indent, kind, kat, sep, rest = m.groups()
                target, why = mp.get(kat, (None, 'not in the map'))
                if target and target in used and used[target] != kat:
                    why = f'{target} already taken by {used[target]}'
                    target = None
                rest = re.sub(r'\s*// blur-nx:.*$', '', rest)
                if target:
                    used[target] = kat
                    counts['mapped'] += 1
                    new = f'{indent}BANJO_RENUT_{kind}({target}{sep}{rest}  // blur-nx: Katamari {kat}, {why[:150]}'
                    if kind.startswith('HOOK_RAW') and target != kat:
                        renames['__imp__' + kat] = '__imp__' + target
                else:
                    counts['unhooked'] += 1
                    new = f'{indent}BANJO_RENUT_UNHOOKED_{kind}({kat}{sep}{rest}  // blur-nx: not hooked ({why[:150]})'
                report.append(f'{name:16s} {kat} -> {target or "-":12s} {why[:120]}')
                if new != line:
                    lines[k] = new
                    changed = True
            if renames:
                for k, line in enumerate(lines):
                    for old, newn in renames.items():
                        if old in line:
                            lines[k] = re.sub(re.escape(old) + r'\b', newn, lines[k])
                            changed = True
            if changed and not a.dry_run:
                open(path, 'w', encoding='utf-8', newline='').write(nl.join(lines))
    os.makedirs(os.path.join(ROOT, 'out', 'renut_map'), exist_ok=True)
    open(os.path.join(ROOT, 'out', 'renut_map', 'remap_report_blur.txt'), 'w').write('\n'.join(report) + '\n')
    print(counts)


if __name__ == '__main__':
    main()
