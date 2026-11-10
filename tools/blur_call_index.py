#!/usr/bin/env python3
"""blur-nx (2026-11-10): an index of the generated code's calls: for every function, the functions and imports it
calls (in order), and who calls it. For placing hooks by call chain (pacman-nx's evidence style: "the only VdSwap
caller", "called by the device init after ...").

  python tools/blur_call_index.py --gen app/generated [--who sub_X] [--calls sub_X] [--imp NAME] [--pattern REGEX]
"""
import argparse
import collections
import glob
import json
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FUNC = re.compile(r'^(?:DEFINE_REX_FUNC|PPC_FUNC_IMPL)\((?:__imp__)?(sub_[0-9A-F]{8})\)')
CALL = re.compile(r'\b(sub_[0-9A-F]{8}|__imp__[A-Za-z0-9_]+)\(ctx, *base\)')


def build(gen):
    import hashlib
    cache = os.path.join(ROOT, 'out', 'call_index_%s.json' % hashlib.md5(os.path.abspath(gen).encode()).hexdigest()[:8])
    files = sorted(glob.glob(os.path.join(gen, '*_recomp.*.cpp')))
    stamp = sum(os.path.getmtime(f) for f in files)
    if os.path.exists(cache):
        d = json.load(open(cache))
        if d.get('stamp') == stamp:
            return d['calls']
    calls = {}
    cur = None
    for f in files:
        for line in open(f, encoding='utf-8', errors='replace'):
            m = FUNC.match(line)
            if m:
                cur = m.group(1)
                calls[cur] = []
                continue
            if cur:
                for c in CALL.findall(line):
                    if c != cur:
                        calls[cur].append(c)
    json.dump({'stamp': stamp, 'calls': calls}, open(cache, 'w'))
    return calls


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--gen', default=os.path.join(ROOT, 'app', 'generated'))
    ap.add_argument('--who', nargs='*', default=[])
    ap.add_argument('--calls', nargs='*', default=[])
    ap.add_argument('--imp', nargs='*', default=[])
    a = ap.parse_args()
    calls = build(a.gen)
    callers = collections.defaultdict(list)
    for f, cs in calls.items():
        for c in cs:
            callers[c].append(f)
    for w in a.who:
        w = w if w.startswith(('sub_', '__imp__')) else 'sub_' + w.upper()
        print(w, 'called by', sorted(set(callers.get(w, []))))
    for c in a.calls:
        c = c if c.startswith('sub_') else 'sub_' + c.upper()
        print(c, 'calls', calls.get(c))
    for i in a.imp:
        n = '__imp__' + i
        print(n, 'called by', sorted(set(callers.get(n, []))))


if __name__ == '__main__':
    main()
