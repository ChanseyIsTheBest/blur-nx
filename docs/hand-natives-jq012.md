# Hand natives for the job workers 0-2 (draft, 2026-11-11)

The request: ten of the twenty hottest game functions of the console race profile (2026-11-11 15:18, rebased build
46b3f6d6, report 17 onward), by the gate of [optimising-functions.md](optimising-functions.md#the-hand-track): a native
only where the samples show a cost a rewrite removes. Two are written, eight are not. Files:
`app/src/cpu/hot/blur_hand_natives_jq012.draft.h` (the code), `app/src/cpu/hot/hand_jq012.draft.txt` (the hand.txt lines
and the reasons), this file. Nothing was built or run: every native below is untested and needs the guard (and the sine
a run that reaches it) before it is kept.

| Function | Decision | Share of a core |
|---|---|---|
| sub_82954478 | written (the sine) | 2.3 % |
| sub_82521E68 | written | 2.5 % |
| sub_8251E6F0 | not written (finding for the generator) | 4.9 % |
| sub_8251D238 | not written, flat | 3.8 % |
| sub_824F35D8 | not written, dependent chain and loads | 3.0 % |
| sub_8250A7E8 | not written, callees | 3.0 % |
| sub_82621458 | not written, loads and a call | 2.8 % |
| sub_829D2E80 | not written, table lookups | 2.6 % |
| sub_82525FA8 | not written, callees and mode switches between them | 2.5 % |
| sub_825087A0 | not written, flat, callees | 2.1 % |

Sample counts below are those of the "JQ Worker" threads, from `tools/switch_hot_pcs.py ... "JQ Worker" 16` (3500 samples
in all on those threads).

## Written

### sub_82954478: the game's sine

`f1 = sin(f1)`. The generated function (56 instructions, no call): `|x|` stored at `-24(r1)`, range reduction by pi with
`fctid` and two `fnmsub`, the reduction's integer part stored at `-32(r1)` and reloaded (its parity picks the sign), a
polynomial in x^2 by eight fused multiply-adds (coefficients at `0x82C40200 + 56 ... 112`, 1.0 from `0x82005090`), the sign
and a float scale (`+-1.0` from `+28` / `+32`), and `fsel` to a constant beyond `|x| = 2.2e8`. I checked the arithmetic
in Python against `math.sin` with the constants read from `out/renut_image.bin` (20,000 random angles, maximum error
2.2e-16), including the sign and the `+-0` case, which leaves `f1` as it came.

Samples: 45 of the "JQ Worker" samples (1.3 %). The stores of the twelve scratch float registers and r8-r10 to the context
(`stp d..,[x0,#360...]`, +0x160-+0x198) hold 14, the load of f1 at entry (the caller has just stored it) 6, the rest is
spread over the arithmetic. What the native removes: those context stores (twelve `ctx.f2-f13` and r8-r10), the stack
doubleword that is stored and reloaded (both doublewords are still stored, the reload is not), and, because natives are
inlined into every copy that contains the function, the call and the `f1` round trip through the context at the call
sites (`sub_8250A7E8` calls it four times, `sub_82521E68` twice).

Exact: `f1`. Writes the same four stack doublewords as the original (`-32` reduction, `-24` bits of `|x|`, and unless
`REX_NV_SAVES_ELIDE` the zero save slots at `-16`, `-8`), with `Note` and `Card`. Ends with the flush mode off. Scratch not
reproduced: f2-f13, r8, r9, r10 (the original's r10 ends as the bits of `|x|`; r8/r9 as the parity and the reduction).

Things to check:
- It reads the constants from guest memory (the original uses `REX_RO_U64` for 1.0 only; the others are plain loads).
  `0x82C40908` holds a NaN (`big`): it is moved as 64 bits, never through a float.
- `REX_NV_SAVES_ELIDE`: the original (generated) form stores the locals `f30` and `f31` (zero) at `-16`, `-8`; the native
  follows the macro. If the native's translation unit and the original's `o_` form ever differ on that macro the guard
  will report a difference at those two doublewords.
- Declined: a stack pointer whose 32 bytes wrap or cross the physical boundary, or lie in `0x82000000-0x83000020`.
  No sNaN decline: there is no single widening in this function, the same double operations run in the same order.
- Fused operations: `std::fma` only where the original has `fmadd` / `fnmsub`; `-ffp-contract=off` is on the target.
  `-std::fma(a, b, -c)` is written exactly as the generated `fnmsub`.
- The result is `Pin`ned so that a later `msr fpcr` cannot move above the arithmetic; the mode is switched at the start,
  followed by a `Fence`, with the constants loaded after it (as Katamari's `sub_824E97F0` does).

### sub_82521E68: angle vector from a basis (two sines)

`r3` = destination (aligned vector), `r4` = an object (words at `+880` and `+892` select a record: `[+892] + 7044`
holds a pointer, `+76`, or `+116` when the word at `+880` is >= 2), `r5` = three vectors (`+0`, `+16`, `+32`). The
function saves v124-v127 (`__savevmx_124`: four `stvx128` at `r1-96 ... r1-48`), takes the first word `a` of the record (lvlx128),
builds `-a` as a vector, calls the sine on it, rounds to single, calls the sine on `1.0 - (-a)`, and combines the two
with `vmsum3fp128`, `vmulfp128`, `vnmsubfp`, `vmaddfp128` and the basis vectors into one vector stored at `r3`;
`__restvmx_124` restores v124-v127 and the function returns. The vector arithmetic is copied from the generated
statements (the same macros, `REX_VMADDFP`, `REX_VNMSUBFP`, `simde_mm_dp_ps`), only the data flow is changed.

Samples: 50 ("JQ Worker", 1.4 %): +0x480-+0x4cc, the hand-over of 29 vector registers around the `__restvmx_124` call
(one q-register store to the context and one pair load per register), 14; the two samples after each `bl` of the sine
(`ldr d15,[x19,#344]` and `ldr d0,[x19,#344]`, f1/f13 reloaded from the context that the callee has just stored to), 8;
the lane reload of a stored vector (`stvewx128`: `str q..,[sp,..]` then `ldr w,[x,x,lsl 2]`, +0x28c, +0x15c), 5; 23 spread.
What the native removes: the four calls and their hand-overs, the context round trip of f1 and f13 and of every
temporary, both `stvewx128` + `lfs` pairs (the stored word is taken from the register), and three of the four flush-mode
switches (see below).

Exact: `r1`, `r3` (both unchanged by the original at its end). Everything else it writes to registers is scratch and
not reproduced: r5-r10, f1, f13, v0, v12, v13 (the context copies of r11, r12, f0, v35-v63 are restored by the
original's hand-over, v124-v127 by `__restvmx_124`, so they end as they began). `ctx.lr` is reproduced
(`uint32_t(lr)` unless `REX_LR_MOVES_ELIDE`; hand.txt cannot name it, and the guard does not compare it).

Stores: the final bytes the original leaves, each `Note`d first: the second sine's four stack words at `r1-224`
(the first sine's are overwritten by the second's), the back chain at `r1-192` (unless `REX_BACK_CHAIN_ELIDE`), the two
words at `r1-112` (second sine rounded to single) and `r1-108` (first sine rounded), the four saved vectors at `r1-96`
(the context's v124-v127, byte-swapped as `stvx128` does), the dead saves at `r1-24`, `r1-16` (zero: the generated
function's locals `r30`, `r31`) and `r1-8` (the low word of LR), and last the result vector at `r3`. One `Card` marks the
224 bytes under `r1` (all stack, some of them untouched), one the result.

The flush mode, which is the part I am least sure the guard will agree with. The original switches four times (off for
sine one, on for `vsubfp128`, off for sine two, on for `vmsum3fp128`). The native runs both sines in whatever mode the
host is in and switches on once, before the first vector float operation (inputs are `Pin`ned after it), so the mode is
on at the end exactly as in the original. This is exact only if the sine's double arithmetic gives the same results with
the flush bit on and off, i.e. no operation sees or produces a denormal double. Both inputs are single-precision values
widened to double (`SingleToDoubleAnyMode` of the word read, `SingleToDoublePure` of `vsubfp128`'s result, so `|x|` is
zero, or >= 2^-149, or inf/NaN). The reduction is `|x| - 3.14159...*n - 1.2e-10*n`: with `n = 0` the angle is `|x|`
itself (squared >= 2^-298, normal), otherwise a difference of doubles >= 1.5 whose granularity keeps it zero or >= 2^-112.
Every term of the polynomial is therefore zero or >= 2^-300. I believe it, but nothing was run: if the main session
wants zero risk it can restore the switches by calling `disableFlushMode()` before each `SinEval` and
`enableFlushMode()` after (and adding `Fence`/`Pin`), at the cost of the saving.

Declines (nothing written): `r1` not 16-aligned (the original's lane-by-address `stvewx128` and its `lvlx128` of the
frame assume it; the native uses lane 3 and offset 0); a frame, source or destination that wraps 4 GB or crosses the
physical-view boundary; any source (the object's words, the pointer word, the record block, the three vectors at
`r5`, the constant block at `0x82027BF0`) overlapping the 224 bytes under `r1`, because the original reads them after
it has written the frame. The destination may overlap anything: it is written last, in the original too.

Things to check:
- `lvlx128 v49,r0,r6` / `lvlx128 v41,r0,r5` at `r1+80` / `r1+84` are replaced by a splat of the word that the
  original has just stored (`w80c`, `w84`). I rely on lvlx putting the word at the effective address in element 0 and on
  `vspltw128 ...,0` splatting it; the other three words of those vectors are not used.
- `vupkd3d128 v47,v127,0` is replaced by `splat(0x3F800000)`: v127 was just set to zero, and both implementations of
  `REX_VUPKD3D_COLOR` give `byte | 0x3F800000` per lane.
- `vslw128 v58 = 1 << 31` is a constant splat.
- The stack words below the frame (`r1-224 ... r1-193`) are the sine's red zone; they are written from the second
  sine's values only (the guard compares final memory).
- `s.Hand(..., 0)`: no size counted.

## Not written

### sub_8251E6F0 (4.9 %): the largest, and a finding for the generator

820 guest instructions, one callee (`sub_82953880`, a register-restore tail), 99 samples. The function inverts and
multiplies small vector-heavy structures (67 `lvx128`, 52 `vmrghw128`, 42 `vmsum3fp128`, 12 `stvewx128`). The removable
cost is here, and I did not write a native for it: of the 99 samples, the biggest are loads right after a spilled vector.
`+0x2188` (13 samples, and 21 in its 64-byte bucket) is `ldr w15,[x16,x15,lsl #2]` immediately after `str q31,[sp,#1024]`;
`+0x2428` (4), `+0x1bc8` (4) and the `str q..,[sp,#1088...]` stores (+0x2150-+0x216c, 9) are the same pattern. That is the
generated form of `stvewx128`: `REX_STORE_U32(ea, v.u32[3 - ((ea & 0xF) >> 2)])`, a variable index into a vector, which the
compiler turns into a 16-byte spill and a 4-byte reload (a store the A57 cannot forward to the narrower load). At least
a quarter of the function's samples are there; the same pattern appears in `sub_82521E68` (5 samples),
`sub_82525FA8`, `sub_8250A7E8` and `sub_825087A0`. A native for this function would have to be the whole function
rewritten statement by statement with only that changed. That is what the machine copy already is, so the fix belongs in
`tools/localize_native.py` / the code generator (select the lane with a `switch` on `(ea >> 2) & 3`, or a NEON
`vgetq_lane`, so the stored value never goes through memory), where it helps every function. The other big bucket
(`+0x2200`, 12 samples, 10 on `fadd d13,d11,d0` after the `lfs` chain) is a dependent chain of loads and float adds.

### sub_8251D238 (3.8 %): flat

An LU-style elimination (partial pivoting, scaled; `r5` = n, matrix stride `n + 1`) on a small float matrix in four nested
loops. 73 samples, no bucket above 11 % (8 of 73), the top twelve hold 49: address arithmetic, byte-swapped element loads
and stores (each store with a card byte), `fnmsub` chains and the pivot search. The loop bodies recompute row addresses
from scratch and card-mark per element, but the sampled instructions are loads, the `fnmsub`/`fcvt` chain and the store
that waits on it (`+0x4f0`, 5), not a stall one of the listed kinds. A rewrite would be a new implementation of a
numerically sensitive algorithm (single rounding at every `fadds`/`fmuls`/`fnmsubs`, fsel-based pivot choices) with no
single removable cost. Left alone.

### sub_824F35D8 (3.0 %): a dependent chain and object loads

A batch of plane-against-box tests: twelve `vmsum` dot products against loaded planes, compares, then the branch on the
combined mask. 45 samples; 11 are on `ldr w8,[x13,#1848]` after `b.eq` at +0x658, which is the instruction after the
vector-to-integer move and the data-dependent branch (a dependent chain of 10 vector operations and a mispredict, then an
object-field load), 6 on the `faddp` of the dot products. No context or stack traffic to remove; the `faddp`/`zip2`
sequence is the generated `vmsum` and a transposed version would only change latency. Cache misses and a dependent chain.

### sub_8250A7E8 (3.0 %): callees

982 guest instructions and 23 callees (eight calls of `sub_8251F630`, five of `sub_8251CF50`, four of the sine, four of
`sub_8251E630`, `sub_82521E68` twice, ...). The largest bucket is 5 of 56 samples. A native cannot call guest functions
under the guard, and the time is in what it calls (including the two natives above, which it benefits from as a caller).

### sub_82621458 (2.8 %): object loads and a call

298 guest instructions, one real callee (`sub_82630450`), a `__savevmx_120` / `__restvmx_120` pair. 58 samples, 8 (14 %)
at +0x280 and 7 at +0x480: the loads of object fields (`ldr w5,[x10,#20]`, `ldr w1,[x3]`, pointer chasing) and integer-register
stores to the context around them. Cache misses plus a real call; no stall a rewrite removes.

### sub_829D2E80 (2.6 %): a table-driven decoder

A Huffman/LZ style decoder: bit buffer refills from a byte stream, `lhax` walks of code tables, `lbzx` length tables, byte
stores into the output with a card mark each, a match-copy loop. 66 samples, 22 of them on two table loads (`ldrb` at
+0x148 7 and 4, `ldrh` at +0x508 7) that wait on the shifted bit buffer of the previous symbol. Dependent lookups and
cache misses; the byte-store loop could mark cards per run but those are 5-8 samples.

### sub_82525FA8 (2.5 %): callees and mode switches between them

204 guest instructions, four real callees (`sub_824FEBD0`, `sub_82520F78`, `sub_821626C8`, `sub_826803B0`) and a
`__savevmx_123` / `__restvmx_123` pair. 50 samples: the largest bucket (8, at +0xd80) is the `msr fpcr` of the copy's
flush-mode switches, which is the kind of cost the doc lists as removable, but the switches sit between calls whose
callees switch the mode themselves, so they cannot be removed inside this function; the rest is the hand-over code around the
calls and the vector arithmetic. A native cannot call the four callees under the guard. The callees are the candidates.

### sub_825087A0 (2.1 %): flat, callees

1872 guest instructions, nine callees, 42 samples, the largest bucket 5 %. A flat profile; left alone.

## For the main session

- Compile order: `blur_jq012` helpers use `std::fma`, `::rex::ppc::fctid / fctidz / GeZeroAnyMode / SingleToDoubleAnyMode /
  DoubleToSingleAnyMode / RoundToSingleAnyMode / SingleToDoublePure` (diet_ops.h), `REX_BSWAP128`, `REX_RAW_ADDR`,
  `REX_SHUFFLE_TABLE`, `VectorMaskL`, `REX_VMADDFP`, `REX_VNMSUBFP` and the `simde_mm_*` calls, all as the generated code
  and Katamari's natives use them. `Pin` takes a `simde__m128` (a NEON register type on the Switch, `__m128` on x86).
- Keys: `kN82954478` and `kN82521E68` are in the enum already; `blur_hand_keys.cpp` has them. The other eight keys stay
  unused.
- Check order: the sine first (pure, 56 instructions); `sub_82521E68` needs a guarded run that reaches it with
  `banjo_cpu_verify` (it has only copy-internal calls once the sine and save/restore are inlined, so the guard can repeat
  it). Watch for a difference at `r1-8`, `r1-24`, `r1-16` (the macro switches) first, then `r1-112` / `r1-108`.
- If the flush-mode reduction of `sub_82521E68` is rejected, keep the native with the four switches (see above); the
  remaining savings are the calls and the hand-overs, which are most of its samples.
