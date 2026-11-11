# Hand natives, batch 2 (draft, 2026-11-11)

The request was 30 natives from the 60 functions of `out/console/hand_candidates_60.txt` (ranks 21-80 of the console
race profile), callers of those functions counting too. **Six are written, not thirty.** This file says which, what each
removes, and why the rest are not done; the shortfall is mine (time spent reading functions that turned out to be
virtual-call thunks, callers with many real callees, or long vector bodies I could not reproduce exactly).
Files: `app/src/cpu/hot/blur_hand_natives_batch2.draft.h`, `app/src/cpu/hot/hand_batch2.draft.txt`, this file. Nothing
was built or run; every native needs the guard before it is kept. Keys to add: `kN82954F28 kN822E48F8 kN82520D78
kN8230CBB8 kN82244170 kN822E9F20`. Helpers are in `namespace blur_b2` (independent of `blur_hand_natives_jq012.h`).

## How the 60 were sorted

A one-pass index of the generated code gave each function's direct callees and whether it makes an indirect call. Of the
60: 13 make an indirect (virtual) call (822EB480, 822EB4C0, 822CBAD0, 822CBA00, 822CBB80, 82306F88, 82451530, 82733868,
82522340, 8227CCE8, 82504618, 822EB500, 822EC708); 44 have direct callees (no native can call a guest function under the
guard, so only a native that inlines every callee on its path would do); 15 are pure leaves. I read the leaves in this
order of promise and wrote the six that are plain enough to reproduce exactly. The sine's callers (82500AC0, and
822E6E98 which has two more callees) and 825217B8 (caller of sub_82520D78) are the caller natives I would write next; I did
not.

## Written

All six keep the generated statements' arithmetic (same macros: `REX_FDIVS`, `REX_FMULS`, `REX_VMADDFP`, `std::fma` only
where the original has an FMA instruction, `rex::ppc::fctid / fctiwz / GeZeroAnyMode / SingleToDoubleAnyMode /
CompareAnyMode`), set the flush mode as the original ends it with a `Fence`/`Pin` after the switch, `Note` before every
write, and decline (return false, nothing written) on any range that wraps or crosses the physical-view boundary or that
overlaps in a way the original handles differently.

### sub_82954F28 (whole function; exact=f1)
A tangent-like function: `fctid`/`fcfid` reduction, two `fnmsub`, two polynomials and a divide, an exponent adjustment on the
bits of the double, two `fsel`. Samples (1.17 % of a core, JQ Worker 3 and 4): the original writes f0 to `r1-16` three times
and reloads halfwords (`lhz`) of it, stores `fctiwz` of the integer part and reloads the low word (`lwz`), and writes
f2-f13/r8-r10 to the context. The native does the exponent step on the integer image of the double, keeps everything in
registers, and writes the single doubleword the original leaves at `r1-16` (the `fctiwz` image if integer part + 1 == 0,
else the adjusted double). Declines: `r1-16` wrapping or inside `0x82000000-0x83000010`. Doubts: (1) the `extsh` and
`rlwinm ...,4,0,27` steps are done in 32-bit unsigned arithmetic on the low word (only that word reaches the `sth`);
(2) constants are read from guest memory at `0x82C40350 + k` (the Switch build of the original folds only `1.0`-type
`REX_RO_*` loads); (3) no hand-checked run: the guard must reach it (it is called through the virtual thunk
sub_822EB500, so reach depends on the scene).

### sub_822E48F8 (whole function; exact= none)
A linear gain ramp: `dst[i] = src[i] * (f1 + i*(f2-f1)/n)` in single precision, unrolled by four with a tail; with
`dst != src` first eight `dcbz` of 32 bytes. Samples (1.42 %, JQ Worker 3 78 %): every element index goes through the stack
(`std`, `lfd`, `fcfid`: four failed store forwardings per four elements). The native converts directly. Stack words: the
original leaves the last group's indices at `r1-64/-56/-48/-40` and the last tail index at `-40`; the native writes exactly
those (and rereads the bytes of a doubleword the original never writes, so nothing else changes). Declines: negative `n`,
wraps, source overlapping the destination or its dcbz zones, stack words overlapping the buffers. Doubts: the
operand order of the single-precision multiply is `(ramp, src)` in all four lanes and the tail (checked in the asm); the
index of the first element of an unrolled group is converted by `fcfid` of an unsigned 32-bit value: done with
`double(int64_t(i))`.

### sub_82520D78 (whole; exact= none)
One vector of a product of three splatted scalars, three paths by `r5` and the sign of a float. The native computes the
three `lvlx128` splats once, one mode switch, one card. Samples 0.56 % (Game Master 22 %, JQ Worker 4 33 %, 1 22 %).
This is a small saving (the copy keeps these values in locals already); it is included because the function is a
straight leaf with three exact paths. Doubts: `CompareAnyMode(cr, f13, 0.0)` stands for the original's `fcmpu` against the
constant at `0x82000BA0` (read as `0.0`); the multiply operand order is `((rec328 * o6008) * third)` in all paths.

### sub_8230CBB8 (whole; exact= none)
A 1024-byte crossfade `dst = B * ramp + A` over 8 blocks of 128 bytes, ramp gains `v13 * lane offset + start`; with `dst`
different from A and B, eight `dcbz`. The copy byte-swaps and card-marks each of the 64 vector stores. The native does one card,
one mode switch, same `REX_VMADDFP` operations in the same order (the ramp values use the old gains, then the gains are
advanced by 32.0). Doubts: the register mapping of the 16 loaded vectors to the 8 stores was derived from the `lvx128`
offsets in the asm (A at +0..+112, B likewise, results at dst +0..+112); the gain constants are the nine `REX_RO_V128`
vectors of the generated code. If the guard shows a difference, compare the ramp lane order first.

### sub_82244170 (whole; exact= none)
`lvlx128` + `lvrx128` + `vor128` is the unaligned 16-byte load; the function copies `r6` such vectors from `r5` to the aligned
run at `r3 + 16*(r4+120)` and ors `r7` into the doubleword at `r3`. The native is a `memcpy` and one doubleword or.
Declines: more than 2^20 vectors, wraps, source/destination overlap, run overlapping the doubleword. Doubt: the original
reads one block beyond the source run (the `lvrx` block at the end); the native does not (memory safety only).

### sub_822E9F20 (whole; exact= none)
The sum of two 1024-byte float buffers, 64 `vaddfp128`, with eight `dcbz` first when `dst` is neither input. One card, tight
loop. Declines as in 8230CBB8.

## Not written

Per function, one line each, is in `hand_batch2.draft.txt`. The honest summary: 13 indirect-call thunks (nothing to inline),
44 with real callees (only worth a native if the whole callee set on a path can be inlined, which I did not do), and
the leaves I read but did not reproduce: sub_822E29A8 (a vsldoi/vmsum4fp128 filter ending in a 16-way vsel tail),
sub_822D7DF0 (int16 to float with a vector body of `vupkhsb128`/`vcsxwfp`/`vperm` whose exact lane semantics I did not
verify), sub_826ADB30 (closest point on a triangle, all in registers already), sub_8251E438 (ring-buffer bookkeeping),
sub_822E8418 (a 100-statement vector body with reciprocal estimates and compare/select, 176 instructions). The leaves
sub_82522568, sub_8216AE98 and sub_826A7858 were not read.
