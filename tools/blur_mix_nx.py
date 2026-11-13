#!/usr/bin/env python3
"""blur-nx (2026-11-13, #23 and #24 of docs/cpu-near-exact-50-1113.md; the user chose the near-exact ideas to do):
Blur's audio node loop (sub_8230AD50, the mixer that stereo_mix.py narrows) picks a kernel for each (input channel,
output row) gain of a child: its current gain (f13) differs from its target (f0) -> the ramp kernel (sub_8230CBB8,
the gain interpolated over the frame); equal -> 1.0 (f30) the unity kernel (sub_822E9F20: an add), 0.0 (f31) nothing,
otherwise the constant kernel (sub_822EA500: a multiply-add). Two compares get a tolerance from Blur's keys
(app/src/blur_nx_mix.cpp, latched before the game starts; 0 = exactly as before):
  #24  8230AFC8 `fcmpu cr6,f13,f0`: |target - current| < audio_nx_ramp_eps counts as equal (the constant kernel with
       the target gain instead of a ramp to it: each sample differs by at most eps x |sample|);
  #23  8230AFD0 `fcmpu cr6,f0,f30`: |gain - 1| < audio_nx_unity_eps counts as 1 (the unity kernel: an add without the
       multiply; each sample differs by at most eps x |sample|).
  #49  the resampling kernels (822D7880 scalar, 822D9040 VMX mono, 822D79A8 VMX stereo; a subagent's reading of
       2026-11-13) step by count/256 (count: the source samples a 256-sample block consumes) and already copy the block
       (a memcpy) when the ratio is exactly 1.0. With audio_nx_resample_copy a block that consumes exactly 256 samples
       takes that copy too: its fraction is 0 at every output, so the interpolation gives the source samples (apart
       from a -0, a NaN or a denormal the vector unit flushes). Not a ratio window: just below 1.0 a voice consumes
       255 samples over 256 outputs.
  (#16, the zero-fills the code then overwrites whole, is not here: at this step those lines are the plain
  `memset((void*)REX_RAW_ADDR(ea), 0, 32);` / `REX_DCBZ128(ea);` that localize_native.py, store_cards.py and
  vec_dcbz_zva.py read in that exact form; a wrapper would take 822EA3C0, a hot copy, out of the copies.)
The PC census (REX_NX_CENSUS, PC game code only) counts at the same two compares: ramps and near ramps, unity /
constant / zero gains, near-unity and tiny ones (rex/ppc/diet_ops.h slots 16-24, the "[census] near-exact audio" line).

Each patch names its function, its guest address and the instruction's text (a mismatch stops post_codegen: the guest
code moved). Idempotent (the patched lines carry /* blur-nx-mix */). Runs after stereo_mix.py (which rewrites the same
loop's loads) and before tools/localize_native.py, so the machine copies take the same statements.

  python tools/blur_mix_nx.py --gen app/generated [--check]
"""
import argparse
import bisect
import os
import re
import sys

MARKER = '// rexglue ports: blur-nx near-exact mixer tolerances (tools/blur_mix_nx.py)'
MACROS = MARKER + '''
#ifndef REX_BLUR_NX_RAMP_SAME
extern "C" float blur_nx_ramp_eps, blur_nx_unity_eps;  // app/src/blur_nx_mix.cpp: 0 = off (exactly as before)
// #24: a gain whose target differs from its current one by less than the tolerance mixes as the constant target.
[[gnu::always_inline]] static inline bool rex_blur_nx_ramp_same(double cur, double tgt) {
  REX_NX_CENSUS_IF(cur != tgt, 19);
  REX_NX_CENSUS_IF(cur != tgt && __builtin_fabs(tgt - cur) < 1e-6, 20);
  REX_NX_CENSUS_IF(cur != tgt && __builtin_fabs(tgt - cur) < 1.0 / 65536.0, 24);
  return cur != tgt && __builtin_fabs(tgt - cur) < double(blur_nx_ramp_eps);
}
// #23: a constant gain within the tolerance of 1.0 mixes as 1.0 (the unity kernel).
[[gnu::always_inline]] static inline bool rex_blur_nx_unity(double g, double one) {
  REX_NX_CENSUS_IF(g == one, 18);
  REX_NX_CENSUS_IF(g == 0.0, 22);
  REX_NX_CENSUS_IF(g != one && g != 0.0, 16);
  REX_NX_CENSUS_IF(g != one && __builtin_fabs(g - one) < 1e-7, 17);
  REX_NX_CENSUS_IF(g != one && __builtin_fabs(g - one) < 1e-5, 23);
  REX_NX_CENSUS_IF(g != 0.0 && __builtin_fabs(g) < 1.0 / 65536.0, 21);
  return g != one && __builtin_fabs(g - one) < double(blur_nx_unity_eps);
}
// #49: a block consuming exactly 256 source samples takes the kernel's copy (audio_nx_resample_copy).
extern "C" bool blur_nx_resample_copy;  // app/src/blur_nx_mix.cpp
[[gnu::always_inline]] static inline bool rex_blur_nx_copy256(bool eq, uint32_t count) {
  REX_NX_CENSUS(27);
  REX_NX_CENSUS_IF(!eq && count == 256, 28);
  return blur_nx_resample_copy && count == 256;
}
#define REX_BLUR_NX_COPY256(eq, count) rex_blur_nx_copy256((eq), (count))
#define REX_BLUR_NX_RAMP_SAME(cur, tgt) rex_blur_nx_ramp_same((cur), (tgt))
#define REX_BLUR_NX_UNITY(g, one) rex_blur_nx_unity((g), (one))
#endif
'''
TAG = ' /* blur-nx-mix */'

# (function, guest address, the instruction's text, the statement before, the statement after)
PATCHES = [
    ('sub_8230AD50', 0x8230AFC8, 'fcmpu cr6,f13,f0',
     '\tcr6.compare(ctx.f13.f64, f0.f64);',
     '\tcr6.compare(REX_BLUR_NX_RAMP_SAME(ctx.f13.f64, f0.f64) ? f0.f64 : ctx.f13.f64, f0.f64);' + TAG),
    ('sub_8230AD50', 0x8230AFD0, 'fcmpu cr6,f0,f30',
     '\tcr6.compare(f0.f64, f30.f64);',
     '\tcr6.compare(REX_BLUR_NX_UNITY(f0.f64, f30.f64) ? f30.f64 : f0.f64, f30.f64);' + TAG),
]

# #49: the kernels' `beq cr6,<the copy>` after the ratio compare (the count's register there).
for func, addr, reg, target in (('sub_822D7880', 0x822D78A4, 'ctx.r4', '822D793C'),
                                ('sub_822D9040', 0x822D9068, 'r11', '822D9218'),
                                ('sub_822D79A8', 0x822D79D8, 'ctx.r4', '822D7B20')):
    PATCHES.append((func, addr, 'beq cr6,0x%s' % target.lower(), '\tif (cr6.eq) goto loc_%s;' % target,
                    '\tif (cr6.eq || REX_BLUR_NX_COPY256(cr6.eq, %s.u32)) goto loc_%s;%s' % (reg, target, TAG)))
FUNC = re.compile(r'^DEFINE_REX_FUNC\(sub_([0-9A-F]{8})\) \{$')
COMMENT = re.compile(r'^\t// (\S.*?)\s*$')
LABEL = re.compile(r'^loc_[0-9A-F]{8}:\s*$')


def instructions(lines, start):
    """{address: (comment line index, [statement line indices])} of the function whose DEFINE line is lines[start]."""
    addr = int(FUNC.match(lines[start]).group(1), 16)
    out = {}
    cur = None
    i = start + 1
    while i < len(lines) and lines[i] != '}':
        l = lines[i]
        if COMMENT.match(l):
            cur = addr
            out[cur] = (i, [])
            addr += 4
        elif LABEL.match(l) or l.startswith('\tREX_LOOP_'):
            cur = None
        elif cur is not None and l.strip():
            out[cur][1].append(i)
        i += 1
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--gen', required=True, help='generated folder (*_recomp.N.cpp and *_pch.h)')
    ap.add_argument('--check', action='store_true', help='report only')
    a = ap.parse_args()
    files = sorted(f for f in os.listdir(a.gen) if re.match(r'.*_recomp\.\d+\.cpp$', f))
    pchs = [f for f in os.listdir(a.gen) if f.endswith('_pch.h')]
    if not files or len(pchs) != 1:
        sys.exit('ERROR (post-codegen patch): blur_mix_nx: no *_recomp.N.cpp or no single *_pch.h in %s' % a.gen)
    starts, texts = {}, {}
    for f in files:
        lines = open(os.path.join(a.gen, f), encoding='utf-8').read().split('\n')
        texts[f] = lines
        for i, l in enumerate(lines):
            m = FUNC.match(l)
            if m:
                starts[int(m.group(1), 16)] = (f, i)
    ordered = sorted(starts)
    changed = set()
    counts = {'done before': 0, 'patched': 0}
    for func, addr, insn, old, new in PATCHES:
        fstart = ordered[bisect.bisect_right(ordered, addr) - 1]
        if fstart != int(func[4:], 16):
            sys.exit(f'ERROR (post-codegen patch): blur_mix_nx: {addr:08X} is in sub_{fstart:08X}, not {func}: the '
                     f'guest code moved')
        f, line = starts[fstart]
        lines = texts[f]
        ins = instructions(lines, line)
        if addr not in ins:
            sys.exit(f'ERROR (post-codegen patch): blur_mix_nx: no instruction at {addr:08X} in sub_{fstart:08X}')
        ci, idx = ins[addr]
        got = COMMENT.match(lines[ci]).group(1)
        if got != insn:
            sys.exit(f'ERROR (post-codegen patch): blur_mix_nx: {addr:08X} is "{got}", expected "{insn}"')
        if any(TAG in lines[i] for i in idx):
            counts['done before'] += 1
            continue
        hits = [i for i in idx if lines[i] == old]
        if len(hits) != 1:
            sys.exit(f'ERROR (post-codegen patch): blur_mix_nx: {addr:08X} ("{insn}"): its statements are not the '
                     f'expected form: ' + ' | '.join(lines[i].strip() for i in idx))
        lines[hits[0]] = new
        counts['patched'] += 1
        changed.add(f)
    pch = os.path.join(a.gen, pchs[0])
    text = open(pch, encoding='utf-8').read()
    pch_changed = False
    if MACROS not in text:
        if MARKER in text:
            start = text.index(MARKER)
            end = text.index('#endif\n', start) + len('#endif\n')
            text = text[:start] + MACROS + text[end:]
        else:
            anchor = '#define REX_LOAD_U8'
            if anchor not in text:
                sys.exit('ERROR (post-codegen patch): blur_mix_nx: no REX_LOAD_U8 in %s: the generated pch changed' % pch)
            text = text.replace(anchor, MACROS + anchor, 1)
        pch_changed = True
    if not a.check:
        for f in changed:
            open(os.path.join(a.gen, f), 'w', encoding='utf-8', newline='\n').write('\n'.join(texts[f]))
        if pch_changed:
            open(pch, 'w', encoding='utf-8', newline='\n').write(text)
    print('blur_mix_nx: %d patches applied, %d already in place; %d files changed%s' % (
        counts['patched'], counts['done before'], len(changed), ' (check only)' if a.check else ''))


if __name__ == '__main__':
    main()
