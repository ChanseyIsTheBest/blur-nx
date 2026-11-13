#!/usr/bin/env python3
"""blur-nx (2026-11-13, Blur's docs/cpu-near-exact-50-1113.md; the user chose the near-exact ideas to do): the generated
code's forms that a near-exact switch changes, rewritten to helpers whose default is the expression the generator
wrote (so with every switch off the code computes what it did).

  #42  vmaxfp / vmaxfp128 / vminfp / vminfp128:  simde_mm_max_ps(A, B) -> rex::ppc::VMaxFp(A, B)
                                                 simde_mm_min_ps(A, B) -> rex::ppc::VMinFp(A, B)
       (sdk/include/rex/ppc/diet_ops.h: SSE's max / min by default, Arm's fmax / fmin with REX_NX_VMINMAX_FAST;
       the PC census counts the lanes where the two differ.)

A post-codegen step before the copies (tools/localize_native.py), so they take the same forms.

  python tools/near_exact.py --gen app/generated [--check]
Idempotent (the rewritten text no longer matches). No restore: the helpers' defaults are the expressions themselves.
"""
import argparse
import glob
import os
import re
import sys

INSN = re.compile(r'^\t// (\S+)')
FORMS = {
    'vmaxfp': ('simde_mm_max_ps(', 'rex::ppc::VMaxFp('),
    'vmaxfp128': ('simde_mm_max_ps(', 'rex::ppc::VMaxFp('),
    'vminfp': ('simde_mm_min_ps(', 'rex::ppc::VMinFp('),
    'vminfp128': ('simde_mm_min_ps(', 'rex::ppc::VMinFp('),
}


def rewrite(text, counts):
    out = []
    current = None
    for line in text.splitlines(keepends=True):
        m = INSN.match(line)
        if m:
            current = FORMS.get(m.group(1))
            out.append(line)
            continue
        if current and current[0] in line:
            n = line.count(current[0])
            line = line.replace(current[0], current[1])
            counts[current[1]] = counts.get(current[1], 0) + n
        out.append(line)
    return ''.join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--gen', required=True)
    ap.add_argument('--check', action='store_true', help='report only')
    args = ap.parse_args()
    files = sorted(glob.glob(os.path.join(args.gen, 'renut_recomp.*.cpp')))
    if not files:
        sys.exit('near_exact: no generated sources in %s' % args.gen)
    counts = {}
    changed = 0
    for path in files:
        text = open(path, encoding='utf-8').read()
        new = rewrite(text, counts)
        if new != text:
            changed += 1
            if not args.check:
                open(path, 'w', encoding='utf-8', newline='\n').write(new)
    print('near_exact: %s in %d files%s' % (', '.join('%d %s' % (v, k.rstrip('(')) for k, v in sorted(counts.items()))
                                            or 'nothing to rewrite', changed, ' (check only)' if args.check else ''))


if __name__ == '__main__':
    main()
