#!/usr/bin/env python3
"""blur-nx (2026-11-13, the user: "force the game to stereo, do as many additional modifications you need"): Blur's own
audio graph mixes in two channels instead of 5.1, behind one key (audio_stereo_mix, app/src/blur_stereo.cpp).

Blur mixes its sounds through a node graph of its own (0x822B0000-0x82320000; XAudio2 only carries the finished 5.1 mix
from a 6-channel ring to the render driver). Every mixer class, patch input/output node and "surround" insert effect
is built with a channel count of its own (5 or 6: no single setting; the speaker config does not reach them, the
channel census showed six channels with sound either way). The node loop sub_8230AD50 mixes each child's input
channels (the u16 at +40 of the mixer) into its output rows (+22) through a gain matrix of row stride +42 (the class's
speaker count), written by the panners for five or six speakers. In stereo:
  - the nine speaker-mixer creators narrow the mixer they build to two output rows (+22 = 2) right after its
    constructor (sub_8230A660; the matrix stride +42 and the panners stay as they are: its rows keep five or six
    speaker gains); MonoToAmbisonics (B-format outputs) is left alone;
  - the patch input/output nodes and the "surround" insert effects are built with two channels (their `li r5,N`);
  - sub_8230AD50 reads at most the channels its child has (the child's +22) and, where a mixer was narrowed, folds
    each gain row into left and right (REX_BLUR_STEREO_FOLD: front + center x c + LFE x l + surround x s, the port's
    own 5.1 fold weights, rex::audio::GetStereoFold), current and target alike, and a row reset clears the whole row;
  - the output path is unchanged: the master ring stays six channels, channels 2-5 now silent, and the port's fold
    then gives the same level (the fold is linear: folding each gain before the mix equals folding the mix).
Not exact (D, the user's call made): the non-linear surround effects (compressor, saturator, meters) run on the folded
pair, and the panners' five-speaker gains reach two speakers through the fold.

Each patch names its function, its guest address and the instruction's text (the generator's `// <instruction>`
comments, one per instruction, give each statement its address): a mismatch stops post_codegen (the guest code moved).
Idempotent (the patched lines carry /* blur-stereo */). Runs before tools/localize_native.py, so the machine copies
take the same statements; tools/post_codegen.py.

  python tools/stereo_mix.py --gen app/generated [--check]
"""
import argparse
import os
import re
import sys

MARKER = '// rexglue ports: blur-nx stereo mix behind audio_stereo_mix (tools/stereo_mix.py)'
MACROS = MARKER + '''
#ifndef REX_BLUR_STEREO_WIDTH
extern "C" bool blur_stereo_mix;  // app/src/blur_stereo.cpp: audio_stereo_mix, latched before the game starts
extern "C" uint32_t BlurStereoFoldBits(float g0, float g1, float g2, float g3, float g4, float g5, uint32_t out,
                                       uint32_t width);
// A class's channel count, two in stereo.
#define REX_BLUR_STEREO_WIDTH(n) (blur_stereo_mix ? int64_t(2) : int64_t(n))
// A mixer node narrowed to fewer output rows (+22) than its matrix has speakers (+42).
#define REX_BLUR_STEREO_ON(node) (blur_stereo_mix && REX_LOAD_D_U16((node), 22) < REX_LOAD_D_U16((node), 42))
// A mixer's input count for one child: at most the child's channels in stereo.
#define REX_BLUR_STEREO_INPUTS(n, child)                                                                         \\
  (blur_stereo_mix && uint64_t(REX_LOAD_D_U16((uint32_t)(child), 22)) < uint64_t(n)                               \\
       ? uint64_t(REX_LOAD_D_U16((uint32_t)(child), 22))                                                          \\
       : uint64_t(n))
// Gain k of the row whose entry 'out' is at 'entry' (0 past the row's width), and the row folded for output 'out'.
#define REX_BLUR_G(entry, out, k, w)                                                                              \\
  ((k) < (w) ? std::bit_cast<float>((uint32_t)REX_LOAD_U32((uint32_t)(entry) - ((uint32_t)(out) << 2) + 4u * (k)))   \\
             : 0.0f)
#define REX_BLUR_STEREO_FOLD(entry, out, w)                                                                       \\
  BlurStereoFoldBits(REX_BLUR_G(entry, out, 0u, (uint32_t)(w)), REX_BLUR_G(entry, out, 1u, (uint32_t)(w)),          \\
                     REX_BLUR_G(entry, out, 2u, (uint32_t)(w)), REX_BLUR_G(entry, out, 3u, (uint32_t)(w)),          \\
                     REX_BLUR_G(entry, out, 4u, (uint32_t)(w)), REX_BLUR_G(entry, out, 5u, (uint32_t)(w)),          \\
                     (uint32_t)(out), (uint32_t)(w))
#endif
'''
TAG = ' /* blur-stereo */'

# (kind, function, guest address, the instruction's text)
WIDTHS = [  # `li r5,N`: the class's channel count
    # patch input / output nodes (sub_8230A440 / sub_8230B0E0 with 5 or 6)
    ('width', 'sub_82301420', 0x8230145C, 'li r5,5'),   # SurroundAudioInput
    ('width', 'sub_82303E08', 0x82303E44, 'li r5,6'),   # SurroundLFEAudioInput
    ('width', 'sub_823015B0', 0x823015E8, 'li r5,5'),   # SurroundAudioOutput
    ('width', 'sub_82303F98', 0x82303FD0, 'li r5,6'),   # SurroundLFEAudioOutput
    # the "surround" insert effects (each creator passes its width)
    ('width', None, 0x822FC7C0, 'li r5,5'),   # BitCrusher
    ('width', None, 0x822FCAE8, 'li r5,5'),   # RingModulator
    ('width', None, 0x822F2748, 'li r5,5'),   # HighPass
    ('width', None, 0x822FE2F0, 'li r5,5'),   # ResonantLowPass
    ('width', None, 0x822FE4F8, 'li r5,6'),   # RMSLevel
    ('width', None, 0x822FDADC, 'li r5,5'),   # Flanger
    ('width', None, 0x823043B8, 'li r5,5'),   # LowPass
    ('width', None, 0x822FE650, 'li r5,5'),   # Saturator
    ('width', None, 0x8230172C, 'li r5,5'),   # Compressor
    ('width', None, 0x822FDFB0, 'li r5,5'),   # 24 dB ResonantLowPass
]
NARROW = [  # `bl 0x8230a660`, the mixer constructor: the mixer it built narrowed to two output rows
    ('narrow', None, 0x822FB544, 'bl 0x8230a660'),   # MonoToSurround 1->5
    ('narrow', None, 0x822FAF04, 'bl 0x8230a660'),   # MonoToSurroundLFE 1->6
    ('narrow', None, 0x823003B0, 'bl 0x8230a660'),   # StereoToQuad 2->4
    ('narrow', None, 0x82300C7C, 'bl 0x8230a660'),   # StereoToSurround 2->5
    ('narrow', None, 0x82302B04, 'bl 0x8230a660'),   # QuadToSurround 4->5
    ('narrow', None, 0x822FEBF8, 'bl 0x8230a660'),   # SurroundToSurround 5->5
    ('narrow', None, 0x822FE898, 'bl 0x8230a660'),   # SurroundToSurroundLFE 5->6
    ('narrow', None, 0x82304190, 'bl 0x8230a660'),   # SurroundLFEToSurroundLFE 6->6
    ('narrow', None, 0x822FEEDC, 'bl 0x8230a660'),   # AmbisonicsToSurround 5->5
]
NODE_LOOP = [  # sub_8230AD50
    ('inputs', 'sub_8230AD50', 0x8230AF20, 'lhz r10,40(r31)'),
    ('inputs', 'sub_8230AD50', 0x8230B058, 'lhz r11,40(r31)'),
    ('reset', 'sub_8230AD50', 0x8230AF70, 'stfsx f31,r7,r23'),
    ('reset', 'sub_8230AD50', 0x8230AF9C, 'stfsx f31,r7,r22'),
    ('fold_target', 'sub_8230AD50', 0x8230AFBC, 'lfsx f0,r7,r22'),
    ('fold_current', 'sub_8230AD50', 0x8230AFC0, 'lfsx f13,r7,r23'),
]
PATCHES = WIDTHS + NARROW + NODE_LOOP

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


def patch_lines(kind, lines, idx, addr):
    """The new text of the statement lines idx (in place); False when the statements are not the expected ones."""
    texts = [lines[i] for i in idx]
    if any(TAG in t for t in texts):
        return 'done'
    if kind == 'width':
        for i in idx:
            m = re.match(r'^\t((?:ctx\.|ll_)?r5)\.s64 = (\d+);$', lines[i])
            if m:
                lines[i] = f'\t{m.group(1)}.s64 = REX_BLUR_STEREO_WIDTH({m.group(2)});{TAG}'
                return True
        return False
    if kind == 'narrow':
        for i in idx:
            if re.search(r'(?:__imp__)?sub_8230A660\(ctx, base\);', lines[i]):
                body = lines[i].strip()
                lines[i] = (f'\t{{ const uint32_t blur_node_ = ctx.r3.u32; {body} '
                            f'if (blur_stereo_mix) REX_STORE_D_U16(blur_node_, 22, 2); }}{TAG}')
                return True
        return False
    if kind == 'inputs':
        for i in idx:
            m = re.match(r'^\t(\S+)\.u64 = REX_LOAD_D_U16\(r31\.u32, 40\);$', lines[i])
            if m:
                lines[i] = (f'\t{m.group(1)}.u64 = REX_BLUR_STEREO_INPUTS(REX_LOAD_D_U16(r31.u32, 40), '
                            f'REX_LOAD_D_U32(r26.u32, 24) + REX_LOAD_D_U32(r24.u32, 0));{TAG}')
                return True
        return False
    if kind == 'reset':
        for i in idx:
            m = re.match(r'^\t(REX_STORE_U32(?:_C)?)\(ctx\.r7\.u32 \+ (r2[23])\.u32, temp\.u32\);$', lines[i])
            if m:
                store, matrix = m.group(1), m.group(2)
                lines[i] = (lines[i] + f'\n\tif (REX_BLUR_STEREO_ON(r31.u32)) {{ for (uint32_t blur_k_ = 2; '
                            f'blur_k_ < REX_LOAD_D_U16(r31.u32, 42); ++blur_k_) {store}(ctx.r7.u32 - '
                            f'(r30.u32 << 2) + (blur_k_ << 2) + {matrix}.u32, temp.u32); }}{TAG}')
                return True
        return False
    if kind == 'fold_target':
        for i in idx:
            if lines[i] == '\tfb0.u32 = REX_LOAD_U32(ctx.r7.u32 + r22.u32);':
                lines[i] = ('\tfb0.u32 = REX_BLUR_STEREO_ON(r31.u32) ? REX_BLUR_STEREO_FOLD(ctx.r7.u32 + r22.u32, r30.u32, '
                            f'REX_LOAD_D_U16(r31.u32, 42)) : REX_LOAD_U32(ctx.r7.u32 + r22.u32);{TAG}')
                return True
        return False
    if kind == 'fold_current':
        for i in idx:
            if lines[i] == '\tctx.f13.f64 = double(REX_LOAD_F32(ctx.r7.u32 + r23.u32));':
                lines[i] = ('\tctx.f13.f64 = double(REX_BLUR_STEREO_ON(r31.u32) ? std::bit_cast<float>(REX_BLUR_STEREO_FOLD('
                            'ctx.r7.u32 + r23.u32, r30.u32, REX_LOAD_D_U16(r31.u32, 42))) : REX_LOAD_F32(ctx.r7.u32 + '
                            f'r23.u32));{TAG}')
                return True
        return False
    raise ValueError(kind)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--gen', required=True, help='generated folder (*_recomp.N.cpp and *_pch.h)')
    ap.add_argument('--check', action='store_true', help='report only')
    a = ap.parse_args()
    files = sorted(f for f in os.listdir(a.gen) if re.match(r'.*_recomp\.\d+\.cpp$', f))
    pchs = [f for f in os.listdir(a.gen) if f.endswith('_pch.h')]
    if not files or len(pchs) != 1:
        sys.exit('ERROR (post-codegen patch): stereo_mix: no *_recomp.N.cpp or no single *_pch.h in %s' % a.gen)
    # The function that holds each address: the largest function start at or below it, found by a scan of every file.
    starts = {}  # function address -> (file, line)
    texts = {}
    for f in files:
        lines = open(os.path.join(a.gen, f), encoding='utf-8').read().split('\n')
        texts[f] = lines
        for i, l in enumerate(lines):
            m = FUNC.match(l)
            if m:
                starts[int(m.group(1), 16)] = (f, i)
    ordered = sorted(starts)
    import bisect
    changed = set()
    counts = {'done before': 0, 'patched': 0}
    for kind, func, addr, insn in PATCHES:
        k = bisect.bisect_right(ordered, addr) - 1
        fstart = ordered[k]
        if func is not None and fstart != int(func[4:], 16):
            sys.exit(f'ERROR (post-codegen patch): stereo_mix: {addr:08X} is in sub_{fstart:08X}, not {func}: the guest '
                     f'code moved')
        f, line = starts[fstart]
        lines = texts[f]
        ins = instructions(lines, line)
        if addr not in ins:
            sys.exit(f'ERROR (post-codegen patch): stereo_mix: no instruction at {addr:08X} in sub_{fstart:08X}')
        ci, idx = ins[addr]
        got = COMMENT.match(lines[ci]).group(1)
        if got != insn:
            sys.exit(f'ERROR (post-codegen patch): stereo_mix: {addr:08X} is "{got}", expected "{insn}"')
        r = patch_lines(kind, lines, idx, addr)
        if r is False:
            sys.exit(f'ERROR (post-codegen patch): stereo_mix: {addr:08X} ({kind}, "{insn}"): its statements are not '
                     f'the expected form: ' + ' | '.join(lines[i].strip() for i in idx))
        counts['done before' if r == 'done' else 'patched'] += 1
        if r is True:
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
                sys.exit('ERROR (post-codegen patch): stereo_mix: no REX_LOAD_U8 in %s: the generated pch changed' % pch)
            text = text.replace(anchor, MACROS + anchor, 1)
        pch_changed = True
    if not a.check:
        for f in changed:
            open(os.path.join(a.gen, f), 'w', encoding='utf-8', newline='\n').write('\n'.join(texts[f]))
        if pch_changed:
            open(pch, 'w', encoding='utf-8', newline='\n').write(text)
    print('stereo_mix: %d patches applied, %d already in place (%d widths, %d mixers narrowed, %d in the node loop); '
          '%d files changed%s' % (counts['patched'], counts['done before'], len(WIDTHS), len(NARROW), len(NODE_LOOP),
                                  len(changed), ' (check only)' if a.check else ''))


if __name__ == '__main__':
    main()
