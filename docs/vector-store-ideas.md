# Vector-store ideas for Blur's hot functions (machine copies in place)

Written 2026-11-11 for the rework in which Sonic Racing's `localize_native` transform moves into post-codegen and is
applied to the generated function itself (no second copy, no hook, the name and the `ctx` interface kept, GCC free to
inline, the guard only in verify builds with the original text kept as the reference, the hot set from PGO counts, no
size cutoff). Every idea below was found in this tree's code (`app/generated/renut_recomp.*.cpp`,
`app/generated/hot/n_<ADDR>.h`, `out/console/race_by_optimisation_copies.csv`, `app/src/cpu/hot/hand.txt` and the hand
natives), not in general advice. Nothing was built, run or edited to write this; the numbers come from reading the
generated text and the profile. Ideas are VS-01 .. VS-42 ("vector store"); where one extends an existing number of
SASRT's lists (`machine-copies-1110.md`, `machine-copies-deferred.md`) it says which.

## 0. How to read the numbers

- The console race (Switch, 3 x A57 at 1020 MHz, 11.9 fps = 84.0 ms a frame): 100 % of one core = 1,905 samples (372
  samples are 19.53 %), so **one sample = 0.0441 ms of CPU per frame**. JQ Worker 3 + 4 together carry about 2,049 of
  the 6,105 samples (thread shares of the CSV applied to each function) = **90 ms a frame**, which matches the 89.5 /
  97.2 ms of the brief.
- The JQ 3/4 top, in samples attributed to the two threads (ms a frame in brackets): `sub_8230AD50` 368 (16.2),
  `sub_822BEDD0` 228 (10.1), `sub_822E17C8` 159 (7.0), `sub_8230EDE0` 136 (6.0), host `memcpy` 78 (3.4), host `memset`
  60 (2.6), `sub_822EB500` 49 (2.2), `sub_822EB480` 36 (1.6), `sub_822D92D8` 36 (1.6), `sub_8230AAD0` 34 (1.5),
  `sub_8230B778` 31 (1.4), `sub_82953D90` 30 (1.3), `sub_82306F88` 30 (1.3). These roots are copies that absorbed their
  callees, so their samples include the vector kernels the closure pasted in: `sub_822EA3C0` (copy), `sub_822E9F20`
  (add), `sub_822EA170` (multiply, a twin of the add), `sub_822EA500` (`dst = s*b + a`), `sub_8230CBB8` (crossfade),
  `sub_822E46F8` (256-byte scale), `sub_822E4698` / `_8230A4D8` (zero), `sub_822D92D8` (int16 -> float), `sub_822D9040` /
  `sub_822D79A8` (table gathers), `sub_822E4FE8` / `sub_822E53C8` (vector filters).
- **Gains are estimates** (a range of ms a frame on JQ 3 + 4, then a confidence). The CSV has no per-instruction data
  (that needs `tools/switch_hot_pcs.py` with the build's ELF), so where a gain depends on how many samples sit on the
  store lines I say so. Where the work is memory-bound (the blocks are 1 KB; that the DSP chain's working set is larger
  than the A57 cluster's 2 MB L2 is my hypothesis, not a measurement) I say "bound: memory" and the idea is about bytes, not
  instructions. What is measured: the samples of `memset` are in its `dc zva` loop and of `memcpy` in its 64-byte loop,
  which is bandwidth, not instruction count.
- Micro-architecture facts (TBL cost, `rev32` cost, `dc zva` block size 64 B) are the project's own notes
  (`tools/vec_byteswap.py` docstring, the memset profile) or the Cortex-A57 manuals as I remember them: **every one needs
  a micro-benchmark on the console before an idea that rests on it is taken** (there is `sdk/tests/arm_forms_check.cpp`
  and the start-up forms self-check to host one).
- "Hooked" below means today's copies behind `BANJO_HOT_HOOK_EX`; "in place" means the rework.

## 1. Census (what the generated tree holds; counted by regular expressions over `renut_recomp.*.cpp`)

| What | Count | Hot examples |
|---|---|---|
| `dcbzl` (`memset(..., 0, 128)`) / `dcbz` (`memset(..., 0, 32)`) | 334 / 67 in 39 functions | `sub_8230EDE0` 64, `sub_8230BC58` 80, `sub_8230B8B8` 32, `sub_8230A4D8` 16, `sub_822DDB20` 16, 14 more with 8 `dcbzl` and 8 with 8 `dcbz` |
| vector stores with a card (`REX_CARD(ea), simde_mm_store_si128(...)`) | 26,634 | `sub_822EA3C0` / `_822EA500` / `_822E9F20` 16 each, `sub_82306F88` 46 |
| `stvlx` / `stvrx` (`REX_STVLX_C` / `_STVRX_C`) | 628 / 307 | `sub_829D2E80` 2+2, `sub_82451530` 4+4, `sub_82193598` 16+16 |
| `stvewx` / `stvehx` stored lane (`REX_LANE_U32` / `_U16`) | 6,678 | `sub_82306F88` 17, `sub_8251E6F0` 12, `sub_825087A0` 13 |
| `lvlx128` followed by a splat of word 0 (`shuffle_epi32(.., 0xFF)` of the same register) | 7,844 | `sub_825087A0` 100, `sub_82505EF8` 26, `sub_8250A7E8` 19, `sub_827E40D0` 19 |
| `lvrx` (`VectorMaskR[`) / `lvlx` (`VectorMaskL[`) table shuffles | 1,138 / 10,335 | `sub_822D92D8` 5, `sub_82244170` (hand native), `sub_829D2E80` 3 |
| `lvx` followed at once by an `stvx` of the same register (a struct copy) | 6,058 in 1,425 functions | `sub_826803B0` 27, `sub_82523550` 20, `sub_828E57D0` 30 |
| `__savevmx_N` functions / their store-card marks | 82 / 2,251 | `sub_8250A7E8` calls `__savevmx_119`, `_124` and the restores |
| `simde_mm_mul_ps(simde_mm_cvtepi32_ps(..), 2^-n)` (a `vcfsx` with a scale) | 282 | `sub_822D92D8` 8 a loop pass |

## 2. Ranked table (by expected console gain on JQ Worker 3 / 4, the critical threads)

"Exact" = bit-identical to the PowerPC whatever the data; "exact*" = exact for every single-threaded run and every
run in which the other threads do what the original also allows, with the reason in the entry; "decision" = not
bit-identical in guest memory or needs the user. "Other" = the gain on the other threads (JQ 0-2, Game Master), where all
three cores are also full.

| # | ID | Idea | Targets | JQ 3+4 gain (ms/frame) | Conf. | Exact | Bound | Other threads |
|---|---|---|---|---|---|---|---|---|
| 1 | VS-03 + VS-04 | Dead `dcbz` before a full overwrite, and zero-forwarding of the loads of a just-zeroed range | candidates `sub_822E4810`, `_822E98B0`, `_8230C748`, `_822DDB20` (8-16 `dcbz`/`dcbzl` each; proof per site) and the accumulators of `sub_8230AD50`'s zeroed rows | 2 - 6 | low-med | exact | memory (removes bytes) | small |
| 2 | VS-01 + VS-02 + VS-05 | `dcbzl` runs and loops as one range zero, `dc zva` inline | `sub_8230EDE0` (64), `sub_8230AD50` (loop of 8), `sub_822E17C8` -> `sub_822E4698`, host `memset` 2.6 | 1.5 - 4 | med | exact | memory (instructions only) | `sub_8230BC58`, `sub_8230B8B8` (XThread) |
| 3 | VS-06 | 256-byte tile fusion of consecutive 1 KB kernels (copy, add, mix over the same block) | the call chain of `sub_8230AD50` / `sub_8230B778` | 1 - 4 | low | exact* | memory (cache blocking) | - |
| 4 | VS-08 + VS-09 + VS-10 | Prefetch both 64 B halves of a 128 B guest line, typed hints, next-block lookahead | every 1 KB kernel's `dcbt` prologue | 1 - 4 | low-med | exact (hint) | memory | `sub_8250A7E8` family |
| 5 | VS-28 | Alignment-specialised kernels (`& ~0xF` and address arithmetic gone) | `sub_822EA500`, `_822EA3C0`, `_822E9F20`, `_822EA170`, `_8230CBB8`, `_822EE628`, `_822E46F8` | 1 - 3 | med | exact | instructions | - |
| 6 | VS-18 | Element-order domain: `rev32` instead of the 128-bit reversal in lane-wise chains | the same kernels | 1 - 2.5 | low-med | exact | instructions | `sub_82306F88`, filters |
| 7 | VS-11 .. VS-17 | Store cards: view gate, check-before-clear, stack bases, ranges, hoist, pair, shared runs | every store on JQ 3/4 (`_C` on struct fields, 16 per kernel body, `__savevmx`) | 1 - 2.5 | med | exact* (cards) | instructions + shared card lines | `sub_826803B0`, `sub_8250A7E8` |
| 8 | VS-38 | Alias-checked streaming order instead of 16 loads then 16 stores | the 256-byte blocks | 0.5 - 2 | low-med | exact* | instructions (q-register spills) | - |
| 9 | VS-40 + VS-41 | One templated kernel for the family, the native as the function's own fast-path prologue | the six 1 KB kernels and their clones | 0.5 - 2 | med | exact | instructions + coverage | - |
| 10 | VS-07 | A constant 1 KB copy / zero inline, not a libc call | the natives' `std::memcpy` / `std::memset`, host `memcpy` 3.4 | 0.5 - 1.5 | low | exact | memory | - |
| 11 | VS-36 | Natives write back only the registers the call site reads | `n_822EA500` (14 vectors + 8 GPRs), `n_822EE628` | 0.4 - 1 | med | exact | instructions | - |
| 12 | VS-19 | Memory-order domain for the width-changing conversions | `sub_822D92D8` (1.6), `_822D9040`, `_822D79A8` | 0.5 - 1 | med | exact | instructions | - |
| 13 | VS-25 + VS-26 | `lvlx` + splat of word 0 as one 4-byte load; word-demand `lvlx` gathers | `sub_8230CBB8`, `_822D9040`, `_822D79A8` here; 7,844 sites in the tree | 0.5 - 1 | med | exact | instructions | 2 - 4 (`sub_825087A0`, `_8250A7E8`, `_8251E6F0`) |
| 14 | VS-27 | `vcfsx`'s power-of-two scale folded into `scvtf #n` | `sub_822D92D8` | 0.3 - 0.6 | high | exact | instructions | 282 sites |
| 15 | VS-20 | Explicit raw copies where the swaps cancel | `sub_822EA3C0`'s body, struct copies | 0.3 - 0.8 | med | exact | instructions | `sub_826803B0` and 6,000 pairs |
| 16 | VS-29 + VS-30 + VS-31 | `stvewx`: static lane, lane of a splat, forwarding to the scalar load | `sub_82306F88` (17 lanes, 5 x `ctx.r4 = r1 + 80`) | 0.3 - 0.8 | med | exact | instructions | 1 - 2 (`sub_8251E6F0`, `sub_825087A0`) |
| 17 | VS-22 + VS-23 + VS-24 | Unaligned pairs: `lvlx/lvrx/vor` as one load, a copy without swaps, `stvlx/stvrx` pairs without the run-time test | `sub_82244170`, `sub_829D2E80`, `sub_82236E78` | 0.1 - 0.5 | med | exact | instructions | 0.5 - 1 |
| 18 | VS-32 .. VS-35 | Frame spills: elide, raw, only the used registers, per-site LiveOut | `sub_8250A7E8` (29 vector hand-overs), `sub_82521E68` | 0 - 0.3 | med | decision (stack bytes) | instructions | 2 - 4 |
| 19 | VS-21 | `lvx` / `stvx` runs as a `memcpy` | `sub_826803B0`, `sub_82523550` | ~0 | med | exact* | instructions | 0.5 - 1 |
| 20 | VS-37, VS-39, VS-42 | Keys fold to constants, adjacent-pair addresses, twin folding | all | 0 - 0.5 | med | exact | code size / I-cache | - |

---

## A. Zero stores and bytes moved

### VS-01  DZ-RUN: a run of `dcbzl` as one range zero

1. **Pattern.** Eight (or sixteen, thirty-two, ...) `dcbzl` at base + 0, 128, ..., 896 (+ 1024 ...). Real code,
   `app/generated/hot/n_8230EDE0.h`, root of the 64 `dcbzl` of the family (JQ 3 61 %, 4 38 %, 6.0 ms):
   ```
   ea = (r30.u32) & ~127;
   if (s.Note((u32)(ea), 128)) REX_CARD(ea), memset((void*)REX_RAW_ADDR(ea), 0, 128);
   r11.s64 = 128;
   ea = (r11.u32 + r30.u32) & ~127;
   if (s.Note((u32)(ea), 128)) REX_CARD(ea), memset((void*)REX_RAW_ADDR(ea), 0, 128);
   r22.s64 = 256;  ...  l_r5.s64 = 896;   (then the same at r30 + 1024)
   ```
   Same shape in `sub_8230BC58` (80), `_8230B8B8` (32), `_8230A4D8` (16), `_822DDB20` (16), `_822E4698`, `_822E4810`,
   `_822E61E0`, `_822E98B0`, `_8230AD50` and 10 more with eight each (tree-wide: 334 `dcbzl` in 39 functions). The hand note
   of `822E4698` (hand.txt): "91 % on the dcbzl zero stores (each line read before it is overwritten)".
2. **Transform.** A text pass over the generated tree finds `ea = (K_i + base) & ~127;` + `memset(.., 0, 128)` pairs where
   the K_i are the literals 0, 128, ... (the `li` constants are on the lines above; `ro_constants`-style tracking already
   knows them) and the pairs are adjacent (only `li` / `ea =` lines between). Since each K_i is a multiple of 128,
   `(K_i + base) & ~127 = (base & ~127) + K_i`: one contiguous range. It becomes
   ```
   REX_ZERO_LINES(base_u32, n);   // zero [ (base & ~127), + 128 n ), card the range once (VS-14)
   ```
   with the `li` register writes kept (they are live-out until VS-35 says otherwise).
3. **Where.** A `post_codegen` pass (new `tools/dcbzl_runs.py`, before `store_cards.py`, which then marks the range) plus
   an SDK header function `rex::ppc::ZeroLines` (`sdk/include/rex/ppc/memcalls.h` is the right neighbour).
4. **In place / hooked.** In place: the text is rewritten in the generated function, so the pass needs nothing from the
   copy; the guard's reference text keeps the eight statements. Hooked: the copies are made from the processed tree, so
   they get it too (and `n_8230EDE0.h` shrinks by 56 lines of `memset`).
5. **Exact.** The bytes written are the same zeros; between the first and the last `dcbzl` of the run there is no load
   and no store (the pass requires it), so the order of the eight is not observable by this thread; another thread can
   never rely on the order of two `dcbzl` (no ordering exists on the PowerPC either).
6. **Gain.** 3-8 ms of code is zeroing in this family if the 91 % of `822E4698` is typical; what remains afterwards is
   the memory cost of the 1 KB lines (bound: memory). By itself 0.5-1.5 ms; the point is that it makes VS-02, VS-03 and
   VS-05 recognisable.
7. **Risk / verify.** A mis-fused run that is not contiguous: the pass refuses unless the K_i are consecutive multiples
   of 128. Verify with the guard (the Logged form records the range once; the original text records eight) and the
   store-card verify mode (`banjo_renut_store_cards_verify`).

### VS-02  DZ-LOOP: a counted loop whose body is a `dcbzl` run

1. **Pattern.** `app/generated/hot/n_8230AD50.h`, the top function of the whole profile (372 samples, 19.5 % of a core,
   16.2 ms on JQ 3+4), `loc_8230ADC4`:
   ```
   r11.u64 = ((uint32_t)(ll_r10.u32 << 10));  r11.u64 = r11.u64 + r21.u64;
   ea = (r11.u32) & ~127;   ... memset(.., 0, 128)   (eight times, K = 0 .. 896)
   ll_r8.u64 = REX_LOAD_D_U16(r31.u32, 22);          // the bound, read again each pass
   ll_r10.s64 = ll_r10.s64 + 1;  cr6.compare<uint32_t>(ll_r10.u32, ll_r8.u32, xer);  if (cr6.lt) goto loc_8230ADC4;
   ```
   One pass zeroes the 1,024 bytes at `r21 + (i << 10)`; the loop zeroes `[r21, r21 + 1024 n)`, n = the halfword at
   `r31 + 22`. `sub_822E4698` (a native exists) is the same loop as a function; `8230AD50`'s own copy of it is **not**
   covered by any native (hand.txt has `8230A4D8`, not `8230AD50`).
2. **Transform.** Collapse the loop to a single `REX_ZERO_LINES(r21, n << 3)` when (a) the body is a VS-01 run plus the
   counter and the bound reload, (b) at entry the descriptor halfword (`r31 + 22`) lies outside `[r21, r21 + 1024 n0)` with
   n0 the first value (a run-time check, three compares), (c) the range does not wrap or cross the host's view boundary
   (`detail::Straight`). Otherwise the original loop.
3. **Where.** `post_codegen` (after loop_locals) for the pattern; the check in the SDK header.
4. **In place / hooked.** In place: the fast path and the loop are one function (the fast path is `[[likely]]`). Hooked:
   the same text is in the copy.
5. **Exact.** The zero bytes are the same. The bound is read once instead of every pass: the original reads it through a
   plain load (no volatile, the loop has a compiler barrier only so that polling loops survive), so a store by another
   thread to `r31 + 22` in the middle of the loop is a data race whose outcome the original does not define either; if the
   user wants the strictest reading the loop can stay and only VS-01 applies.
6. **Gain.** The loop is the first thing `8230AD50` does for every row of its job. If the zero stores are 30-50 % of the
   function (plausible: the 8 x `stp q` pairs per line the hand note describes), that is 5-8 ms; the estimate is low
   confidence until `switch_hot_pcs.py` is run on this function. The memory bound stays (VS-03 removes it).
7. **Risk / verify.** `tools/switch_hot_pcs.py` on `sub_8230AD50` first. The guard compares the final memory; add a
   test with the descriptor inside the range (the check must refuse).

### VS-03  DZ-DEAD: a `dcbz` / `dcbzl` whose range the function overwrites completely before reading it

1. **Pattern.** The 1 KB vector kernels zero the destination and then store all of it. `n_822EA500.h` (and
   `_822EA3C0`, `_822E9F20`, `_822EA170`, `_822EE628`, `_8230CBB8`, `_822E46F8`-style):
   ```
   ea = (l_r3.u32) & ~31;  REX_CARD(ea), memset((void*)REX_RAW_ADDR(ea), 0, 32);        // dcbz r0,r3
   ea = (r27.u32 + l_r3.u32) & ~31;  ... memset(.., 0, 32);                              // dcbz r28,r3  (K = 128, 256, ... 896)
   ... 4 x ( 16 x lvx128 a/b ; 16 x vmaddfp ; 16 x stvx128 to r10 + K ) ...
   ```
   The 32-byte pieces at 128-byte spacing lie inside the 1,024 bytes the four blocks store ([dst & ~15, + 1024)); only
   when bit 4 of `r3` is set does the first piece start 16 bytes below the stores (the hand natives' `lead`). The
   guard in the original code is `(r4 - r3) != 0 && (r5 - r3) != 0` (zero only when dst is neither source). The hand
   natives (`n_822EA3C0`, `n_822EA500`, `n_822EE628`) already do this by hand ("the zeroing only where it outlives the
   stores"). The pass is for the **same shape without a native**: candidates `sub_822E4810`, `_822E98B0`, `_8230C748`,
   `_822DDB20` and each 1 KB kernel the next batch finds; whether each one overwrites what it zeroed has to be read per
   site (`sub_822E61E0`'s eight `dcbzl` sit in a scalar-float function and are probably accumulators, not overwritten).
2. **Transform.** For a `dcbz`/`dcbzl` run, find the stores that follow in the same function with no read of the range
   in between (tracked symbolically as base register + literal offset, the form all of these have). Where the stores cover
   the range, drop the zeroing; where they cover all but a known sliver (the `lead` 16 bytes), keep a 16-byte zero of the
   sliver. The loads in between read sources: add a run-time check that no source lies in the zeroed range (the same
   `apart()` the natives use: three subtractions); on failure run the original text.
3. **Where.** `post_codegen` as a function-level specialisation (`if (disjoint) { fast } else { original text }`); needs
   the offsets lines (`li`) - the same tracker as `ro_constants_cfg.py`.
4. **In place / hooked.** In place: this is the form; the fallback is the original text. Hooked: works in the copy; but a
   hooked copy behind a hand native can only gain where there is no native.
5. **Exact.** Final memory equal: every zeroed byte is later overwritten (or kept), and no load or other store sees the
   zero in between (checked statically for this thread, the disjointness check proves the sources). Another thread that
   reads the destination while the kernel runs sees zeros-then-data in the original and data in the new form: a
   half-written buffer is unspecified on the PowerPC as well (no barrier orders the stores).
6. **Gain.** The kernels do this before every 1 KB write: 1 KB of zeros (8 x 32 B of the 128 B lines, but each touches
   a 128 B guest line = two A57 lines that must be allocated) that the 1 KB of stores then overwrite. The natives' own
   notes put these three kernels at 7.6 + 5.5 + 4.8 % of a core before; removing the zeroing is part of why they got lighter.
   For the families without a native the saving is the whole zero (VS-01 shows 8230EDE0's is one of the biggest lines).
   2-6 ms in total is plausible but depends on how many of the 24 other `dcbzl` functions are fully overwritten, which has
   to be read per site.
7. **Risk / verify.** The analysis refuses anything it cannot prove (a call or an indirect call between zero and stores).
   Guard (memory compare), `sdk/tests` for the checker, and a unit test with dst partially overlapping a source.

### VS-04  DZ-FWD: loads of a just-zeroed range read a zero register

1. **Pattern.** A buffer is zeroed (`dcbzl`) and then accumulated into: `dst = dst + x` (822EE628-style adds,
   `8230EDE0` after its 64 `dcbzl`). The first add reads the zeros it just wrote.
2. **Transform.** Within the function (the in-place closure holds the kernels inlined), keep a one-entry "zero range" per
   thread of the code (a compile-time symbolic range, or a run-time `zr_lo, zr_hi` pair set by VS-01 and cleared by any
   store that overlaps or any call): a load whose 16-byte slot lies inside the zero range becomes `vzero`, and the `bswap`
   of it disappears. The computation `0.0f + b` is **still executed** (as `vaddq(zero, b)`), so signed zero and the flush
   mode stay as the original. With VS-03 the zero store is then dead and goes.
3. **Where.** The in-place transform (needs the closure: the accumulate is in a callee). `post_codegen` can only do it
   once the callee text is part of the function (always_inline member), i.e. it belongs with `localize_native`'s closure
   step.
4. **In place / hooked.** In place yes (the closure is in the function); hooked the same, since a copy is a closure.
5. **Exact.** Yes, because the add is kept. (Skipping the add, "first writer stores b", would differ for b = -0.0
   -> +0.0 in the original, for denormals under the flush mode and for NaN payload quieting: **not** taken, listed as
   rejected in section 8.)
6. **Gain.** Saves the load of the zeros and its bswap per vector, 1 KB per accumulate: the same order as VS-03, and only
   after it. Low confidence: needs a census of which kernels read the zeroed rows (the call chain of `8230AD50` after its
   zero loop).
7. **Risk / verify.** The bookkeeping must be conservative (any store of any width that may overlap, any call). Verify
   with the guard and `banjo_forms_selfcheck`'s style of a fixed edge-input table (-0.0, denormal, NaN) for the add.

### VS-05  ZVA: `dc zva` as the primitive of every aligned 128-byte zero

1. **Pattern.** Every `dcbzl` site, runs or not (334 sites). The hand note (`822E4698`) describes the generated
   `memset(.., 0, 128)` as "stp q pairs: every line read into the cache before it is overwritten". Host `memset` in the
   profile (118 samples, 60 on JQ 3/4 = 2.6 ms) is newlib's `dc zva` path, so `dc zva` itself works in EL0 on the console.
2. **Transform.**
   ```
   #define REX_DCBZL(ea)  rex::ppc::ZeroLine128(REX_RAW_ADDR((ea) & ~127))
   inline void ZeroLine128(uint8_t* p) { asm volatile("dc zva, %0\n\tdc zva, %1" :: "r"(p), "r"(p + 64) : "memory"); }
   ```
   with the block size taken from `DCZID_EL0` once at start-up and checked by the forms self-check (if the block is not
   64 bytes the macro falls back to `memset`). A 128-byte guest line is two 64-byte blocks, both aligned.
3. **Where.** SDK header (`rex/ppc/memcalls.h`) + `store_cards.py`'s `DCBZ` regex (it matches `memset(.. 0, (32|128))`).
4. **In place / hooked.** Same in both.
5. **Exact.** Zeros are zeros; `dc zva` is a store of zeros of the block; the exclusive monitor and ordering rules of the
   guest's `dcbzl` are not stronger than those of a `memset`. The 32-byte `dcbz` is **not** converted (the block would
   zero 64 bytes, which is more than the guest writes).
6. **Gain.** Instructions: 8 x 16 B stores -> 2 `dc zva` per line. The line is allocated without a read from the
   outer cache, which is what the 91 % was waiting on (if that is the cause). Bound: memory afterwards.
7. **Risk / verify.** The self-check reads `DCZID_EL0` and zeroes a scratch page both ways; one console run to see that
   `dc zva` into the guest window (cacheable, write-back) is not slower than the `memset` path on the A57.

### VS-06  TILE: fuse consecutive 1 KB kernels over the same block, 256 bytes at a time

1. **Pattern.** `sub_8230AD50` calls, in order, `sub_8230A4D8` (zero / copy), `sub_822EA3C0` (copy), `sub_822E9F20` (add),
   `sub_822EA500` (`s*b + a`), `sub_8230CBB8` (crossfade) (`n_8230AD50.h`, the `f_sub_*` members), each a loop of four
   256-byte blocks over a 1 KB range; the output of one is the input of the next. `sub_8230B778` has the same members
   (`8230A4D8`, `822EA3C0`, `822BD548`). Each pass reads and writes the whole 1 KB, so the second pass reads data the
   first wrote 1 KB earlier (L1 hit) but the third reads it after a few more KB, and every pass pays the line allocation.
2. **Transform.** When two kernels in a row have the same dst / src ranges (checked at run time as equality, the common
   in-place case: `dst == a` in the accumulates), run block k of kernel 2 right after block k of kernel 1. Registers
   hand over per block (v0-v13 live-outs are the last block's, so the fused form must reproduce them: they are the
   last block's values anyway).
3. **Where.** The in-place transform, after VS-40 (the kernels are one template; fusing two instances of
   `K1K<Op>` is a library call, not a text pass).
4. **In place / hooked.** In place with the kernels inlined (`always_inline` members). Hooked copies already inline
   the same members.
5. **Exact*.** With dst == src ranges identical and no partial overlap the per-block order changes nothing about the
   values (each 16-byte vector is read after the kernel before it wrote it). The memory order of stores seen by other
   threads changes (they see a different interleaving of the two kernels' stores): both are allowed by the guest memory
   model, which orders nothing here.
6. **Gain.** Cache blocking: the second kernel's reads hit L1 instead of L2, and one pass of zero/line allocation is
   shared. If the working set is bigger than L2 (hypothesis), up to a third of the bytes moved; honest range 1-4 ms.
7. **Risk / verify.** The bookkeeping of live-out vectors per kernel; the guard compares registers listed in
   `exact=`. Only for sequences the census (a PGO edge count: call B follows call A with the same pointer) finds.

### VS-07  K1K-INLINE: the natives' constant 1 KB `memcpy` / `memset` as inline code

1. **Pattern.** `app/src/cpu/hot/blur_hand_natives_jq34.h`: `n_822EA3C0` calls `std::memcpy(Raw(base, dst), Raw(base, src), 1024)`
   and then `std::memset(.., 0, 16)`; `n_822E4698` calls `std::memset(Raw(base, start), 0, len)`. Host `memcpy` is
   153 samples (8.0 % of a core; 3.4 ms on JQ 3/4), `memset` 118 (6.2 %); the brief: "memset/memcpy added 6.3 ms/frame".
2. **Transform.** For a size known at compile time (1,024) use an inline pipelined loop, 16 `ldp q` / `stp q` pairs
   with `prfm pldl1strm` for the source 256 bytes ahead, instead of the library call: no size dispatch, no alignment
   head, the destination known 16-aligned (VS-28).
   ```
   template<size_t N> [[gnu::always_inline]] void Copy16(uint8_t* d, const uint8_t* s) { ... ldp/stp q ... }
   ```
5. **Exact.** A copy is a copy (non-overlap checked by the native). **Gain.** Only the call overhead and the library's
   dispatch: 0.5-1.5 ms; the 64-byte loop of newlib's `memcpy` is as good as this for large copies. **Memory-bound:
   this does not reduce the bytes.** **Where.** SDK header `rex/ppc/memcalls.h` (`memcalls_test.cpp` covers the size
   rules). **In place / hooked.** Same. **Verify.** `sdk/tests/memcalls_test.cpp` plus a micro-benchmark against newlib's
   `memcpy` on 1 KB aligned blocks (my doubt: it may be equal).

---

## B. Prefetch (the 53 % / 44 % load samples of the kernels are mostly waiting for lines)

### VS-08  PF-HALF: a guest `dcbt` covers 128 bytes, the A57 line is 64

1. **Pattern.** `app/generated/renut_pch.h:145`: `#define REX_PREFETCH(x, write) __builtin_prefetch((const void*)REX_RAW_ADDR(x), (write))`,
   one `prfm pldl1keep` per `dcbt`. The kernels issue 8 per source: `n_822EA500.h`
   ```
   REX_PREFETCH(l_r4.u32, 0);  r27.s64 = 128;  REX_PREFETCH(r27.u32 + l_r4.u32, 0);  l_r10.s64 = 256; ... 896
   ```
   so 8 prefetches cover 8 x 64 B of a 1,024 B source: **half of it**.
2. **Transform.** `REX_PREFETCH(x, w)` -> `REX_PREFETCH128(x, w)`: `prfm` on `x & ~63` and `(x & ~63) + 64`... For a 128 B-aligned
   guest line both halves; for the unaligned `dcbt` the line the hardware would fetch is the 128-aligned one, so
   `(x & ~127)` and `+64`. The 16 prefetches of a two-source kernel become 16 `prfm` with immediate offsets from one
   base register (the `li` constants and adds in the prologue go away).
3. **Where.** `pch` macro (`tools/pch_neon_shuffles.py`-style patch of `renut_pch.h`) + a pass that merges consecutive
   `REX_PREFETCH` with literal strides into `REX_PREFETCH_RANGE(base, 1024)`.
4. **In place / hooked.** Same in both.
5. **Exact.** A hint, no architectural effect (a prefetch cannot fault).
6. **Gain.** The load samples of the kernels were 53 % (`822EA500`), 44 % (`822EA3C0`), 58 % (`822EE628`): the loads that
   miss are the ones in the half-lines nothing prefetched, if the hardware prefetcher does not cover them. Memory-bound:
   up to a few ms if misses dominate, near 0 if the A57's stride prefetcher already streams these; **measure with
   the PMU counters of L1D refills before and after** (the console profile tools do not give it).
7. **Risk.** More instructions (16 vs 8 `prfm`) and more pollution if the second half is never read; the kernels read all
   1 KB, so it is read.

### VS-09  PF-HINT: streaming and store prefetches

1. **Pattern.** `REX_PREFETCH(.., 0)` for sources read once (`dcbt`), nothing for the destinations (`dcbz` is the guest's
   "I will write this line", which the generator turns into a zero store, not a hint).
2. **Transform.** Sources of one-shot streams -> `prfm pldl1strm`; destination lines about to be written fully (VS-03 removed
   the zeroing, so nothing allocates them) -> `prfm pstl1strm` (or `pstl1keep` when the next kernel reads them, VS-06).
3. **Where / in place.** The same macro and pass as VS-08.
4. **Exact.** A hint. **Gain.** After VS-03: the destination line has no zero store to allocate it, so a store prefetch is
   how it avoids a write-allocate stall: 0.5-1.5 ms. **Risk.** `pstl1strm` on the A57 may behave as `pstl1keep`; harmless.

### VS-10  PF-AHEAD: prefetch the next 1 KB block while working on this one

1. **Pattern.** The DSP chain works block after block (`8230AD50`'s loops at `loc_8230AF34` / `loc_8230AF48` step a
   channel and a block through the same source row; `822EE628` loops over a table of (dst, a, b) triples read one at a
   time: `n_822EE628`'s comment "the entries are read when the original reads them"). Each kernel prefetches only its own
   1 KB, in its prologue, and then needs it at once.
2. **Transform.** For a table-driven or counted loop, prefetch the **next** entry's (or block's) sources at the top of
   this one (SASRT's idea 52 does this for linked pointers; this is the table / stride form): the table is a vector of
   16-byte entries, so entry i+1's `a` and `b` are loadable (two words) and prefetchable a full kernel earlier.
3. **Where.** `localize_native` / in place: the loop recogniser of idea 52 (`loop_info`), extended from "self-load
   chase" to "load from table + 16 i". The entry's loads are plain loads of data, so loading entry i+1 early reads a value
   a concurrent writer could change: harmless for a hint address (it only chooses what to prefetch).
4. **Exact.** A hint. **Gain.** The first-touch miss of every kernel call is the biggest stall (the prefetches of the
   prologue come one instruction ahead of the loads): 1-3 ms plausible, low confidence (memory-bound). **Verify.** L1D
   refill counters; the guard (no change in values).

---

## C. Store cards (`REX_CARD`, 26,634 vector-store sites)

The cards: `*(u8*)(base + (ea >> 12)) = 0` per store (`renut_pch.h`, `tools/store_cards.py`); in the copies
`banjo_hot_prelude.h` keeps the page of the current run and marks when the run moves (SASRT's idea 39). What reads
them: Renut (`app/src/renut_engine/gpu/render/draw.cpp:1519-1580`, `textures.cpp:302`) for converted vertex / index
copies and textures. `PhysicalOf()` there shows which pages it can ever ask about: physical views `0xA0000000`,
`0xC0000000`, `0xE0000000` and `0x7F000000 - 0x7FFFFFFF`; "`UINT32_MAX` for anything else". The hand notes measured
the mark at 17 % of `822EA3C0`, 5 % of `822EA500`, 6 % of `822EE628` (the profile before the copies; whether the
prelude's card runs were already in that build I did not check).

### VS-11  CARD-VIEW: a mark only where Renut can look

1. **Pattern.** Every `_C` store: struct fields (`BANJO_HOT_STORE_D_U32_C(r11.u32, 0, l_r10.u32)` in `8230EDE0`), every
   vector store (`REX_CARD(ea), simde_mm_store_si128(...)`), `Card(base, w0, wlen)` in the natives. The JQ audio blocks
   and most object state live in the heap and stack, not in the four views.
2. **Transform.**
   ```
   #define REX_CARD(ea)  ((void)(REX_CARD_WATCHED(ea) ? (*(u8*)(base + ((u32)(ea) >> 12)) = 0) : 0))
   // watched: (u32)(ea) >= 0xA0000000u || ((u32)(ea) - 0x7F000000u) < 0x01000000u
   ```
   (two unsigned compares, or one 64-bit mask test on `ea >> 24`). The natives' `Card()` gets the same gate on its first
   address.
3. **Where.** `tools/store_cards.py`'s `MACROS` and `rex/system/xmemory.h` (`MarkStoreCardsAt`).
4. **In place / hooked.** Same in both.
5. **Exact*.** Exact **for every consumer that exists**: the cards are read only by Renut through `PhysicalOf`; a card
   that nobody reads cannot be observed (the cards live in the first MB of the guest window, which no guest code reads).
   It is a decision if the user wants cards to stay a generic facility: any future reader of other views must be added
   to the gate. The PC verify mode (`banjo_renut_store_cards_verify`) already counts "bytes the host writers marked ...
   [1] elsewhere": with the gate on, a run that reports a changed-under-clean-cards copy proves the gate wrong.
6. **Gain.** The mark is a shift, an add and a byte store (plus, in the copies, the run compare); the gate is two compares
   and a predicted branch: it saves **the store**, which is what bounces the card line between cores 3 and 4 (VS-12)
   and fills the store buffer. 0.5-1.5 ms across the vector kernels and struct fields.
7. **Risk / verify.** PC verify run with the gate (nothing may be reported as "marked elsewhere that matters"); the forms
   self-check cannot see it (no instruction form).

### VS-12  CARD-COND: check the card before clearing it

1. **Pattern.** Cards are shared bytes: one 64-byte line holds 64 cards = 256 KB of guest pages. JQ 3 and JQ 4 write
   buffers of the same job pool, so their marks land on the same card lines. Each mark is an unconditional store of 0, usually
   onto a byte that is already 0 (Renut sets cards back to 1 only for the pages it checks).
2. **Transform.** `if (*c) *c = 0;` : a load of a line that stays Shared in both cores instead of a store that takes it
   Exclusive each time. `REX_CARD(ea) -> ((void)(*(u8*)(base + (ea >> 12)) ? (*(u8*)(base + (ea >> 12)) = 0) : 0))`.
3. **Where.** `store_cards.py` macros and the prelude's run flush (`REX_CARD_FLUSH`).
4. **Exact*.** Same final state of the cards. Ordering: Renut reads the cards at a kick and sets them back to 1 after
   proving a copy; the original has the same window (a mark store may become visible after Renut's set); the load
   can be satisfied from a stale line, but only until the next coherence message, and Renut's "never answer for an interval
   in which they were cleared" (draw.cpp:1516-1519) rule is built for exactly that delay. Add the guard's
   `banjo_renut_store_cards_verify` to the gate.
5. **Gain.** Only if the lines really ping-pong (two writers on the same line): 0.3-1 ms, medium confidence; zero if
   the writers' pages are far apart. **Cheap to try** (one macro).

### VS-13  CARD-STACK: no mark for a store through a stack-derived base that is not `r1`

1. **Pattern.** `tools/store_cards.py` leaves out stores whose base is `r1` or its local mirrors (`R1 = ...`), so a store
   through `r12 = r1 - 152` (`__savevmx_N`), `r11 = r1 + 208` or `r4 = r1 + 80` is marked. Real code:
   - `n_8250A7E8.h`, `f___savevmx_119`: nine stores `if (s.Note(..)) REX_CARD(ea), simde_mm_store_si128(.. v119 ..)` at
     `l_r12 - 144 ...`, `l_r12 = l_r1 - 192` set just before the call;
   - tree-wide the 82 `__savevmx_N` functions hold 2,251 marks of stack stores;
   - `sub_826803B0` (49 samples): `r11.s64 = r1l.s64 + 208; ... ea = (r11.u32) & ~0xF; REX_CARD(ea), simde_mm_store_si128(...)`
     (copies of four vectors into the frame), and `sub_82306F88`: `ctx.r4.s64 = r1l.s64 + 80; ea = (ctx.r4.u32) & ~0x3;
     REX_STORE_U32_C(ea, REX_LANE_U32(v48, ...))` five times.
2. **Transform.** Forward-propagate "is r1 + constant" through `addi rX,r1,N` / `mr` / `la` within a function (the same
   pass `r1_local.py` / `lr_preserve.py` already has an r1 model) and use the plain store form for them.
3. **Where.** `store_cards.py` (take the register-copy tracking of `r1_local.py`).
4. **Exact*.** Same assumption the tool already makes for `r1`-based stores ("the stack is never vertex or index data");
   the `__savevmx` case needs the call-site fact (`r12 = r1 + c`), which the call sites show (`r12.s64 = l_r1.s64 + -152;` just before the call
   in `n_8250A7E8.h`; the pass must check every one of the 1,794 `__savevmx_` / `__restvmx_` mentions, not assume).
5. **Gain.** 2,251 marks in `__savevmx` and the frame copies: for `8250A7E8`/`8251E6F0` (JQ 0-2/4) 1-2 ms, for JQ 3/4
   (82306F88, 826803B0's 4 share) 0.2-0.4 ms.
6. **Verify.** PC store-card verify (a stack write counted as "marked") and the guard.

### VS-14  CARD-RANGE: two marks per unrolled body instead of one compare per store

1. **Pattern.** `n_822EA500.h` loop body: 16 stores to `(l_r10 + r) & ~0xF` with the card run's
   `((ea >> 12) != rex_card_run ? ... )` compare and branch each. The offsets are literal multiples of 16 within a 256-byte
   body, so the body touches at most two pages.
2. **Transform.** At the top of a run of stores with the same base register and literal offsets in [lo, hi): mark the page
   of `base + lo` and, if different, of `base + hi - 1` once, and use the plain store form for the run (13 compares fewer
   per body).
3. **Where.** `store_cards.py` for the plain form; the in-place transform for the run state (it extends SASRT's idea 39).
4. **Exact*.** Marks come before the stores of the run instead of after each: the idea-39 invariant (every mark before
   the function's next synchronisation) holds; the natives already do `Card()` for the whole range before the stores.
5. **Gain.** The marks were 17 % of `822EA3C0`'s samples with the old per-store form, 5 % with runs for `822EA500`:
   0.3-0.8 ms on the copies that run without natives (everything but the five).

### VS-15  CARD-HOIST: the marks of an affine loop before the loop

1. **Pattern.** A counted loop whose stores are `base_k + 256 i + const` (`8230CBB8`, `822EA3C0`: `ctr = 4`, `r10 += 256`
   per pass).
2. **Transform.** One `REX_CARD_RANGE(lo, hi)` before the loop, plain stores inside (VS-14 for the whole loop).
3. **Exact*.** As VS-14 (marks earlier, not later). **Gain.** 0.2-0.5 ms; it is the loop form of VS-14.
4. **Where / in place.** The in-place loop recogniser (`loop_info`, idea 60's counted-loop test).

### VS-16  CARD-PAIR: the merged stvlx/stvrx marks one page when both stay in it

1. **Pattern.** `n_829D2E80.h` line ~374 (the pair merge of idea 57):
   ```
   if (s.Note((u32)(ea), 16u)) { simde_mm_storeu_si128((simde__m128i*)REX_RAW_ADDR(ea), REX_BSWAP128(...v63.u8)));
                                  REX_CARD(ea); REX_CARD(ea + 16u); }
   ```
   marks `ea` and `ea + 16`: two stores to one page's card nearly every time.
2. **Transform.** The original pair marks `ea` (the stvlx) and `ea + 16` (the stvrx), so the merged form marks the same two
   bytes; to stay identical keep both, but let them go through the run state (`rex_card_run`) like every other mark:
   when `ea + 16` is on the same page the second is a compare and no store. Under the prelude's runs the merged text
   already does this (it expands `REX_CARD` twice); the idea is only to make the hand-native helpers
   (`detail::StoreWordsOfVector`, which "marks at once") do the same.
3. **Exact.** Same card bytes. **Gain.** Tiny (under 0.05 ms); listed because the helper marks four cards for four
   `stvewx` of one 16-byte block. **Where.** `banjo_hand_natives.h` (`detail::StoreWordsOfVector`).

### VS-17  CARD-SHARE: one card run across the inlined members

1. **Pattern.** `n_8230AD50.h` line 210: `{ ctx.r3 = l_r3; ctx.r4 = l_r4; ctx.r10 = l_r10; REX_CARD_FLUSH(); f_sub_822BD548(ctx, base, s); ... }`
   and every other call to an `always_inline` member: the flush is a store of 0 to the pending page, because the member
   declares its own `REX_CARD_RUN_DECL` and flushes its own on return (`f___savevmx_119`: `{ ctx.r11 = l_r11; REX_CARD_FLUSH(); return; }`).
2. **Transform.** An inlined member that contains no fence, atomic or real call (the transitive test `atomic_stop` /
   `plain_calls` already has) takes the caller's `rex_card_run` by reference and neither flushes nor declares one.
3. **Exact*.** The idea-39 invariant (marks before the next synchronisation) holds: the pending page is flushed before
   the first synchronising operation anywhere in the call tree, as before. **Gain.** One conditional store per member call
   (several per row): 0.1-0.3 ms. **Where.** `localize_native.localize()`.

---

## D. Byte order, domains, unaligned access

### VS-18  DOMAIN-W32: lane-wise chains in the element-order domain (`rev32`, not the 128-bit reversal)

1. **Pattern.** The context keeps each VMX register byte-reversed over all 16 bytes; every `lvx`/`stvx` does
   `RexByteReverse128` (`vrev64q_u8` + `vextq_u8`, folded by GCC to one `TBL` with a pool constant; the project note says
   TBL is 5 cycles and 2 micro-ops on the A57). The kernels do 32 loads + 16 stores of it per 256 bytes, then lane-wise
   float ops that do not care about lane order. `n_822EA500.h`:
   ```
   simde_mm_store_si128((simde__m128i*)l_v13.u8, REX_BSWAP128(simde_mm_load_si128((simde__m128i*)REX_RAW_ADDR(ea))));   // x30
   simde_mm_store_ps(v20.f32, REX_VMADDFP(simde_mm_load_ps(l_v0.f32), simde_mm_load_ps(v28.f32), simde_mm_load_ps(l_v13.f32)));
   REX_CARD(ea), simde_mm_store_si128((simde__m128i*)REX_RAW_ADDR(ea), REX_BSWAP128(simde_mm_load_si128((simde__m128i*)v23.u8)));  // x16
   ```
   `v0` (the scalar `s`) is a splat, so lane order is irrelevant for it.
2. **Transform.** In a region (loop body or function) where every vector value is loaded from memory, used only by
   lane-wise ops (`vaddfp`, `vsubfp`, `vmulfp`, `vmaddfp`, `vnmsubfp`, `vand/vor/vxor`, `vsel`, compares, `vmaxfp`) with
   other values of the same kind or with splats, and stored to memory, keep the values in "element order" (each 32-bit
   lane in memory order, bytes of each lane swapped, **no** lane reversal): load = `ldr q` + `rev32 v.16b`, store =
   `rev32` + `str q`. A value that leaves the region (to `ctx.vN`, to a lane op, to `vmrghw`, a splat of a lane) is
   converted back by the 128-bit reversal once, as today. `ctx` holds the canonical image at every region exit.
   ```
   REX_LD_E32(ea)  = vrev32q_u8(vld1q_u8(p))        REX_ST_E32(ea, v) = vst1q_u8(p, vrev32q_u8(v))
   ```
3. **Where.** A new post_codegen pass `tools/vec_domain.py` (after `vec_byteswap.py`, before `localize_native`), a tagging
   dataflow on straight-line / loop bodies; the macros in `renut_pch.h`.
4. **In place / hooked.** In place: a region lives inside the function; the exit conversions are in the same function (no
   `ctx` image change). Hooked: the same text in the copies. The natives (`n_822EA500`) can use the same macros by hand
   today.
5. **Exact.** Lane-wise IEEE operations are lane-independent, so computing them on the permuted register gives each lane
   the same bits (NaN payloads, signed zeros, flush-to-zero included: the flush mode is per operation, not per lane).
   Mixed domains are forbidden by the tagger; a splat is domain-free. The fused-madd port (idea 72) and `vmaddfp`
   rounding are per lane too.
6. **Gain.** 48 byte-reversals per 256-byte body (`REX_BSWAP128` count in `sub_822EA500`): 2 uops -> 1 and no constant register (it
   also frees a q register of the 32). Instruction-level only, and these loops are 44-58 % load samples (memory): 1-2.5
   ms, low-medium confidence; on hot code that is not memory-bound (`82306F88`, the filters) the full benefit.
7. **Risk / verify.** A micro-benchmark first (TBL vs REV32 on a loop of 256 B bodies on the console). Verify with the
   guard, the `sdk/tests/vec_byteswap_test.cpp` style test for the new macros, and a fuzz of the tagger on the
   generated tree (compile both forms and compare outputs on random data on the PC: the shape of SASRT's
   `sasrt_test_vmx_fuzz` harness).

### VS-19  DOMAIN-MEM: memory-element-order domain for width-changing conversions

1. **Pattern.** `sub_822D92D8` (int16 -> float x 2^-15, JQ 3 69 %, 36 samples, 1.6 ms): per 32 elements, from a real
   `app/generated/renut_recomp.*.cpp` dump
   ```
   v63 = lvlx-mask(r9) ; v62 = lvrx-mask(r4 + r9) ; v61 = v63 | v62                 // an unaligned 16-byte load
   v58 = cvtepi16_epi32(unpackhi_epi64(v61, v61)) ; v54 = cvtepi16_epi32(v61)      // vupkhsh / vupklsh
   v50 = mul_ps(cvtepi32_ps(v58), set1_epi32(0x38000000)) ... 8 of these ; 8 x REX_BSWAP128 + REX_CARD + store
   ```
   The head and tail loops use `std` / `lfd` / `fcfid` through the stack (hand note: failed store forwarding).
2. **Transform.** In memory order the whole thing is: `ld1 {8 x s16}` -> `rev16` -> `sxtl` / `sxtl2` -> `scvtf #15`
   (VS-27) -> `rev32` -> `st1`: no mask-table row, no `vor`, no 128-bit reversal. The tagger of VS-18 extended with the
   widening ops (`vupkhsh`, `vupklsh`, `vpkswss` etc. swap the roles of high and low halves in the two domains: a table
   in the pass).
3. **Where / in place / hooked.** `tools/vec_domain.py` as VS-18.
4. **Exact.** int16 -> int32 is exact; `scvtf #15` is VS-27; the byte reversals are permutations.
5. **Gain.** Per loop pass: 6 table loads + 3 `vor` + 8 TBL + 8 `unpack`/`cvt` -> 2 `ld1` + 2 `rev16` + 8 `sxtl` + 8 `scvtf` + 8
   `rev32`: about 40 % fewer instructions. On 1.6 ms: 0.5-1 ms; same shape in `sub_822D9040`/`_822D79A8` (hand natives
   exist; the pass would make them redundant).
6. **Risk / verify.** The mapping table for the unpack/pack ops; `sdk/tests` model test per op as `vmx_small_helpers_test`.

### VS-20  RAW-COPY: a vector that is only loaded and stored is moved without any swap

1. **Pattern.** 6,058 `lvx` + `stvx` of the same register back to back in 1,425 functions; the hot copy kernel
   `sub_822EA3C0`:
   ```
   simde_mm_store_si128((simde__m128i*)v63.u8, REX_BSWAP128(simde_mm_load_si128((simde__m128i*)REX_RAW_ADDR(ea))));  // x16
   REX_CARD(ea), simde_mm_store_si128((simde__m128i*)REX_RAW_ADDR(ea), REX_BSWAP128(simde_mm_load_si128((simde__m128i*)v63.u8)));  // x16
   ```
   (16 loads, then 16 stores, each block). The doc of `vec_byteswap.py` says GCC folds `bswap(bswap(x))` when it meets
   it; through the union-typed local (`v63.u8`) and with 16 live values it does not always.
2. **Transform.** A value whose every use is a store (and which `ctx` does not need: it is a local of the function or a
   LiveOut-dead register) becomes `REX_COPY16(dst_ea, src_ea)` = `vst1q_u8(dp, vld1q_u8(sp))` with no `rev` at all.
3. **Where.** A small pass (could be a mode of `vec_domain.py`), or hand-written in the macro layer.
4. **In place / hooked.** Both. **Exact.** `bswap(bswap(x)) = x`; the register image is dead (checked). **Gain.**
   0.3-0.8 ms on JQ 3/4 for the copy kernel and `8230B778`'s copy; the struct copies of other threads (`826803B0`: 27) too.
   **Verify.** Compare disassembly: `rev64` / `ext` count in the f_ form of `n_822EA3C0` (GCC may already fold it; if it does,
   drop this idea).

### VS-21  STRUCT-COPY: a run of `lvx`/`stvx` pairs is one `memcpy`

1. **Pattern.** `sub_826803B0` (49 samples, JQ 4 30 %): 27 pairs, e.g. four vectors copied into the frame
   (`r11 = r1 + 208`, `r10 = r1 + 224`, `r9 = r1 + 240`, `r8 = r1 + 256` each `lvx`/`stvx`). Others: `sub_82523550` (20),
   `sub_828E57D0` (30), and the many `sub_826C...` with 30.
2. **Transform.** Consecutive pairs with source offsets s + 16 k and destination offsets d + 16 k, no store in between that
   can alias, become one `__builtin_memcpy(d, s, 16 n)` (after a run-time check that [s, s+16n) and [d, d+16n)
   are equal or disjoint, and aligned: `& ~0xF` of both) with one card range (VS-14).
3. **Where.** `post_codegen` (next to `inline_memcalls.py`, whose 1-64 byte inlining it feeds).
4. **Exact*.** Final memory equal when disjoint or identical; overlapping partially: the original order matters, so the
   fallback runs the original text. **Gain.** 0.5-1 ms on the other threads; ~0 on JQ 3/4 (no hot case there).
5. **Verify.** Guard; a test with each overlap.

### VS-22  ULD-FUSE: `lvlx + lvrx + vor` is an unaligned 16-byte load

1. **Pattern.** `sub_82244170` (a hand native exists), the head loop of `sub_822D92D8`, `sub_829D2E80` lines ~758-767
   (1,138 `lvrx` in the tree). From `renut_recomp.*.cpp`:
   ```
   temp.u32 = l_r5.u32 + r11.u32;
   v62 = temp.u32 & 0xF ? REX_SHUFFLE_TABLE(load(temp & ~0xF), &VectorMaskR[(temp & 0xF) * 16]) : zero;   // lvrx
   temp.u32 = r11.u32;
   v63 = REX_SHUFFLE_TABLE(load(temp & ~0xF), &VectorMaskL[(temp & 0xF) * 16]);                          // lvlx
   v63 = v63 | v62;                                                                                      // vor
   ```
   Two aligned loads, two mask rows (indexed by the address), two TBL, an OR and a branch for the aligned case.
2. **Transform.** When the `lvrx` address is the `lvlx` address + 16 (the `li r5,16` constant is visible) and both
   feed only the `vor`: `v63 = REX_BSWAP128(vld1q_u8(host(ea)))` : one `ldur q` (unaligned loads are fine on A57 for
   normal memory) plus the swap. The range [ea, ea + 16) must not wrap or cross the host's view boundary
   (`Straight`).
3. **Where.** `post_codegen`, new `tools/vec_unaligned_loads.py` (the sibling of `vec_unaligned_stores.py`; SASRT's idea
   61 found no `lvlx/lvrx` in Sonic's hot code, here there are 1,138 + 10,335).
4. **In place / hooked.** Both. **Exact.** `lvlx` gives bytes ea .. end of its aligned block into the top of the vector
   (zeros elsewhere), `lvrx` at ea + 16 gives the start of the next block into the bottom (zeros if aligned): their OR
   is exactly the 16 bytes at ea. `sdk/tests/vec_unaligned_test.cpp` has the model for stores; add the loads.
5. **Gain.** Per 16 bytes: 2 loads + 2 mask loads + 2 TBL + OR + test -> 1 load + swap. In `82244170` (a hand native memcpy
   exists) it is 4 vectors a pass. The saving is where no native exists. 0.1-0.4 ms on JQ 3/4, more on other threads.
6. **Verify.** All 16 alignments against the table form (the existing test pattern); guard.

### VS-23  ULD-COPY: unaligned load and an aligned (or unaligned) store with no swap

1. **Pattern.** The same `82244170` loop: `lvlx/lvrx/vor` then `REX_CARD(ea), simde_mm_store_si128(REX_RAW_ADDR(ea), REX_BSWAP128(v63))` at `l_r10 + 0, 16, 32, 48`.
2. **Transform.** VS-22 + VS-20: `REX_CARD..., vst1q_u8(host(ea), vld1q_u8(host(src)))` (four of them: `ldp`-able). With the
   destination 16-aligned the store is `str q`; with an unaligned destination the pair-store merge of idea 57.
3. **Exact.** As VS-22 and VS-20. **Gain.** The whole loop (the hand note: a memcpy of 4 vectors a pass with a card per store):
   0.3-0.6 ms for the copies without the native; **in place** the native may then be unnecessary.

### VS-24  STV-PAIR-STATIC: `stvlx(E)` + `stvrx(E + 16)` with the offset known at generation time

1. **Pattern.** `n_829D2E80.h`:
   ```
   ea = r31.u32;                 if (s.Note(...)) REX_STVLX_C(ea, v63);
   ea = r31.u32 + r21.u32;       if (s.Note(...)) REX_STVRX_C(ea, v63);      // r21 = 16 (a `li` above)
   ```
   Idea 57's merge adds a run-time test (`uint32_t(e2) == uint32_t(e1) + 16u && Straight(...)`) with the original pair as
   the `else`. When `r21` is a tracked constant 16, the test is true at generation time.
2. **Transform.** Drop the test and the `else` text; the pair is `storeu_si128` and the two marks (VS-16).
3. **Where.** `localize_native.merge_vector_stores` (it matches `MS_PAIR`) or, better, a `post_codegen` pass on the tracked
   constants (then every function, not only copies). **Exact.** Same bytes. **Gain.** The branch and the second text;
   tiny (0.05-0.2 ms) but it also lets VS-20 see through the pair.

### VS-25  LVLX-SPLAT: a scalar float loaded with `lvlx128` and splatted

1. **Pattern.** 7,844 sites. `sub_8251E6F0` (a splat from a scalar, then `vsubfp`): 
   ```
   temp.u32 = ll_r9.u32 + r17.u32;
   simde_mm_store_si128(v63.u8, REX_SHUFFLE_TABLE(simde_mm_load_si128(REX_RAW_ADDR(temp.u32 & ~0xF)), &VectorMaskL[(temp.u32 & 0xF) * 16]));
   ...
   simde_mm_store_si128(v63.u32, simde_mm_shuffle_epi32(simde_mm_load_si128(v63.u32), 0xFF));      // vspltw128 v63,v63,0
   ```
   (JQ 3: `8230CBB8` has three of them for its start / step / gain scalars; JQ 0-2: `825087A0` 100, `82505EF8` 26, `8250A7E8` 19.)
2. **Transform.** When the `lvlx` result is used only by a splat of word 0:
   ```
   v63 = (temp & 0xF) <= 12 ? REX_SPLAT32(host(temp)) /* ld1r + rev32 */ : <original text>;
   ```
   Word 0 of an `lvlx` at address a is the 4 bytes a .. a + 3 when a & 15 <= 12, otherwise the remaining bytes zero-padded (a
   rare path; kept as the original).
3. **Where.** `post_codegen` (peephole over the lines already adjacent in the text, a variant of `vperm_one_source.py`).
4. **In place / hooked.** Both. **Exact.** Word 0 of `lvlx` is exactly those bytes; the fallback handles the straddle.
5. **Gain.** Instructions 16-byte load + mask-row load + TBL + `dup` -> `ld1r` + `rev32` (2): per site 3 instructions of
   ~7. On JQ 3/4 0.2-0.5 ms (`8230CBB8`, `822EA500`'s head); thread JQ 0-2: `825087A0`/`8250A7E8`/`8251E6F0` carry 100/19/10
   such sites in functions of 57/117/116 samples, 1-3 ms there.
6. **Verify.** All 16 alignments in `banjo_forms_selfcheck`'s table style; guard.

### VS-26  LVLX-WORD: word demand for `lvlx` gathers

1. **Pattern.** `sub_822D9040` (the hand native exists): 
   ```
   temp.u32 = ll_r7.u32;   v49 = REX_SHUFFLE_TABLE(load(temp & ~0xF), &VectorMaskL[(temp & 0xF) * 16]);   // lvlx128, 16 of them a pass
   v44 = simde_mm_unpackhi_epi32(v49, v47) ...                                                              // vmrghw128, only lane 0 of each survives
   ```
   32 passes of 16 table loads merged by `vmrghw128` and later by `vrlimi128`; only word 0 of each load is ever used.
2. **Transform.** A per-register lane-demand analysis over a function (a backward bitmask of lanes any later use reads);
   a load whose demand is {word 0} becomes `REX_LD_W0(a)` (the 4-byte load of VS-25); a gather of 4 such words into one
   vector becomes `ld1 {v.s}[0..3]` inserts.
3. **Where.** `post_codegen` (new `tools/vec_lane_demand.py`), the same tool VS-25 and VS-30 share.
4. **Exact.** Lanes nobody reads cannot change a result; the demand analysis must treat `ctx.vN` live-outs and calls as
   reading everything.
5. **Gain.** The hand-written natives for `822D9040`/`822D79A8` exist because of this; the pass makes them generic and
   inlinable. 0.2-0.5 ms on JQ 3/4 beyond the natives. **Verify.** Guard; a unit test of the analysis on the three hot
   functions' text.

### VS-27  CVT-FBITS: `cvtepi32_ps * 2^-n` as `scvtf #n`

1. **Pattern.** 282 sites; `sub_822D92D8`, eight per pass:
   ```
   simde_mm_store_ps(v50.f32, simde_mm_mul_ps(simde_mm_cvtepi32_ps(simde_mm_load_si128(v58.u32)), simde_mm_castsi128_ps(simde_mm_set1_epi32(int(0x38000000)))));
   ```
   (0x38000000 = 2^-15: this is `vcfsx v,v,15`).
2. **Transform.** `REX_VCFSX(x, 15)` = `vcvtq_n_f32_s32(x, 15)` (`scvtf v.4s, v.4s, #15`): one instruction, one rounding.
   `vcfux` -> `ucvtf #n`. The inverse `vctsxs` (`x * 2^n` then saturating convert) -> `fcvtzs #n` (the saturation differs
   from the PowerPC's: keep that one out unless the fuzz agrees).
3. **Where.** `renut_pch.h` macro + a regex over the 282 sites (they have the exact two-call form).
4. **In place / hooked.** Both. **Exact.** The original rounds the integer to float (to nearest) and then multiplies by
   2^-n, which is exact because it only changes the exponent; `scvtf #n` forms x / 2^n and rounds once. Rounding
   commutes with a power-of-two scale as long as nothing underflows: for a nonzero integer and n <= 31 the result is at
   least 2^-31, a normal number, so the flush mode cannot matter. Same bits for every int32 and every n in use; confirm by
   an exhaustive run over all 2^32 inputs for each n the 282 sites use (SASRT's F1/F2 fuzz did the same on the console).
5. **Gain.** One `fmul` and a constant register per 4 lanes: on 822D92D8 8 per pass: 0.3-0.6 ms; high confidence.

### VS-28  ALIGNED-SPEC: kernels specialised on 16-byte-aligned base pointers

1. **Pattern.** Every address in the kernels is `ea = (rX + K) & ~0xF` with K a literal multiple of 16 or a register
   holding one. `n_822EA500.h` has 48 such `& ~0xF` in the loop and, for each, an add of a zero-extended 32-bit register to the
   `base` pointer; the hand note puts 21 % of `822EE628`'s samples on "address arithmetic".
2. **Transform.** Entry test: `((r3 | r4 | r5) & 15) == 0` (the three base registers; one `orr`/`tst`). Variant body: every
   `(rX + K) & ~0xF` is `rX + K`, so the addresses are `base + rX` (one `add` per array) with immediate offsets in `ldr q, [x, #K]`
   (`K` up to 65,520 fits the scaled 12-bit immediate; the loops use up to 1,008). The unaligned variant is the
   original text.
3. **Where.** `post_codegen` function-level specialisation (the same `if (fast) {..} else {original}` frame as VS-03); GCC
   multi-versions poorly across an `asm`-free but pointer-masking body, so it has to be written.
4. **In place / hooked.** In place: both bodies are one function; hooked: both in the copy (the code doubles for the 6 kernels only).
5. **Exact.** For an aligned `rX` and K a multiple of 16, `(rX + K) & ~0xF = rX + K` (no carry into bit 3..0 since K's low
   4 bits are 0). The test is on the entry values; the base registers do not change except by the literal increments
   the pass tracks (`r10 += 256`).
6. **Gain.** Per vector: `add; and; ldr; tbl` -> `ldr; rev`-ish: roughly 3 instructions of ~6 on the load/store lines; in
   memory-bound kernels it matters less: 1-3 ms. Medium confidence (the 21 % is a native's own estimate).
7. **Risk / verify.** Start-up forms self-check does not cover it; the guard compares both bodies on random pointers;
   `sdk/tests` for the masking identity.

---

## E. Lanes of `stvewx` / `stvehx` (6,678 sites)

### VS-29  STVEWX-STATIC: the lane is known when the address is the stack pointer plus a constant

1. **Pattern.** `sub_8251E6F0`:
   ```
   r28.s64 = ll_r1.s64 + 80;                       // addi r28,r1,80
   ea = (r28.u32) & ~0x3;
   REX_STORE_U32(ea, REX_LANE_U32(v47, 3 - ((ea & 0xF) >> 2)));
   fb13.u32 = REX_LOAD_D_U32(ll_r1.u32, 80);       // lfs f13,80(r1)
   ```
   (+84, +104, +116 the same); `sub_82306F88`: `ctx.r4.s64 = r1l.s64 + 80;` five times, each followed by an `stvewx` through `r4`.
   `REX_LANE_U32` is two `umov`, two `csel` and a shift (the idea of `vec_lane.h`).
2. **Transform.** An entry check `(r1 & 15) == 0` (the PowerPC ABI keeps r1 16-aligned; every `stwu r1,-N(r1)` in the function
   has N a multiple of 16: the pass checks it); then `(ea & 0xF)` is a constant (80 & 15 = 0 -> lane 3; 84 -> lane 2; 104 -> 1;
   116 -> 2): `REX_STORE_U32(ea, v47.u32[3])` - one `umov`/`str` with a constant lane. Fall back to the original text when the check fails.
3. **Where.** `post_codegen`, after `vec_lane_index.py` (which writes the `REX_LANE_U32` form) using the `r1` mirror
   tracker of `r1_local.py`.
4. **In place / hooked.** Both. **Exact.** The lane selection is a function of `ea & 0xF`, which is `(r1 + c) & 0xF = c & 0xF` when
   r1 is 16-aligned (the entry check also tests the premise that the 360 ABI keeps it so). **Gain.** 5 instructions -> 1 per site; 17 sites in `82306F88` (JQ 3 56 % / 4 43 %, 1.3 ms), 12 in `8251E6F0`.
   0.1-0.3 ms on JQ 3/4, 1-2 ms on the others. **Verify.** `sdk/tests/vec_lane_test.cpp` (add the static form) and
   `banjo_forms_selfcheck`'s `VecLanes`.

### VS-30  STVEWX-SPLAT: the stored lane of a splat does not depend on the address

1. **Pattern.** `sub_8251E6F0` (lines 210 -> 290 of the function):
   ```
   v63 = lvlx(r9 + r17) ; v63 = vspltw128(v63, 0)         // splat of word 0 (VS-25)
   v55 = dp_ps(v56, v62, 0xEF)                             // vmsum3fp128: the sum in all four lanes
   v63 = v63 - v55 ; v47 = dp_ps(v48, v62, 0xEF) ; v63 = v63 + v47
   REX_STORE_U32_C(ea, REX_LANE_U32(v63, 3 - ((ea & 0xF) >> 2)))      // stvewx128 v63,r11,r8
   ```
   `82306F88` the same: `v63 = dp_ps(v61, v61, 0xEF)`, `vrsqrtefp`, `vmulfp`, `vsel` of splats, `stvewx`.
2. **Transform.** A forward dataflow bit "all four lanes equal" (born at `vspltw`, `vspltisw`, `vmsum3fp`/`vmsum4fp` (0xEF /
   0xFF broadcast), `lvlx` + splat, a constant; kept by lane-wise ops whose operands are all splats, by `vsel` with a splat
   mask and splat operands). A store of a splat's lane: `REX_STORE_U32(ea, v.u32[0])` (no lane arithmetic at all).
3. **Where.** `tools/vec_lane_demand.py` (shared with VS-26), run before `vec_lane_index.py`.
4. **Exact.** Equal lanes in, equal lanes out of any lane-wise IEEE operation (the same bits, NaN payload included);
   `vrsqrtefp` is per lane (the fast form of idea 72 too).
5. **Gain.** Removes the lane select entirely: same order as VS-29 where both apply; it also catches the stores through a
   register that is not r1-based. 0.1-0.3 ms JQ 3/4; 1-2 ms on JQ 0-2.

### VS-31  STVEWX-FWD: the scalar reload of the word an `stvewx` just stored

1. **Pattern.** `82306F88`: `ctx.r4 = r1 + 80; ...; stvewx v48 -> (r4 & ~3); fb0.u32 = REX_LOAD_D_U32(r1l.u32, 80); f0.f64 = SingleToDoublePure(fb0.u32);`:
   the store goes to the stack, the next statement loads the same word (`lfs f0,80(r1)`), a store-forward chain of
   a byte-swapped store and a byte-swapped load through a union the compiler cannot see through. Hand notes:
   "stvewx128 lane reload through a spilled vector", "stvewx128+lfs round trips" (`82500AC0`, `82526568`).
2. **Transform.** When the load's address expression is provably the stored one (`r4 = r1 + 80` and the load is `80(r1)`),
   `fb0.u32 = <the lane value just stored>`: no load. The store stays (the stack slot is guest-visible and the guard
   compares it); with a dead-slot proof (a later store to the slot, no read, a return pops it: the frame model) the store
   can go too (VS-32's decision).
3. **Where.** `post_codegen` store-to-load forwarding for stack slots (SASRT's idea 31, "the guest stack in locals",
   not done; this is its vector-lane case). **Exact.** Same value (the store writes the lane's bytes; the load reads them;
   nothing in between). **Gain.** One store-forward round trip (~10 cycles) per site: 5 in `82306F88`; 0.1-0.3 ms.

---

## F. Frames and spills

### VS-32  SPILL-ELIDE: `__savevmx` / `__restvmx` as nothing

1. **Pattern.** `sub_8250A7E8` (117 samples, JQ 4 27 %, JQ 0 29 %): `f___savevmx_119` stores nine vectors
   (`if (s.Note(..)) REX_CARD(ea), simde_mm_store_si128(.. REX_BSWAP128(.. l_v119.u8 ..))`), `__savevmx_124` four more, and the
   function ends with `__restvmx_124` / `_119`: each a bswap + a card + a store, then a load + a bswap. The hand note for
   `82521E68`: "the hand-over of 29 vector registers around the __restvmx_124 call (+0x480-+0x4cc, 14 of 50 samples)".
   A function saves v14-v31 / v64-v127 (non-volatile) in its prologue and restores them in its epilogue; in a copy the
   registers are locals.
2. **Transform.** For a function whose every caller is a copy or a function in the same build (all calls of the tree
   are direct or promoted; `indirect_calls.py` promotes the rest), keep the function's own use of v119..v127 in locals
   *distinct from* `ctx.v119..v127` and never write `ctx.v119` back: the caller's value is then preserved without
   saving it. Drop the `__savevmx_N` / `__restvmx_N` calls and the frame slots.
3. **Where.** The in-place transform (it must know the function's real calls and that no callee reads the slots), after
   `leaf_dead_stores.py`'s LiveOut (idea 37).
4. **In place / hooked.** In place: this is exactly the "write-backs only at real calls/returns" mechanism applied to
   the non-volatile vectors. Hooked: same.
5. **Not bit-exact.** The 16-byte slots below r1 are no longer written: **guest stack memory differs** (dead memory after
   the function returns; what could still read it is an exception / unwind handler, a debugger or a stack scan, none of
   which I found a use of in this game's code, but I did not search the kernel side). **Needs the user's decision**, and
   the guard needs an exemption list ("these slots are not compared") in verify builds.
6. **Gain.** The save/restore of 13-29 vectors a call: tens of instructions plus 13-29 card marks, on the threads
   that run `8250A7E8` (JQ 0-2/4: 2-4 ms, 117 + 116 samples); JQ 3/4 0 - 0.3 ms.
7. **Verify.** The guard with the exemption; a whole race with an exception path hit once, if the game has one.

### VS-33  SPILL-RAW: if the spill stays, no byte swap

1. **Pattern.** Same code; `REX_BSWAP128` on the save and again on the restore. The slot is private to the function (only its
   own `__restvmx` reads it).
2. **Transform.** Store `vst1q_u8` the register image as it is, load it as it is (the swap pair cancels). The slot's bytes
   are byte-reversed relative to the guest's convention.
3. **Not exact in memory** (same decision as VS-32, weaker: the stack still holds the values, in the other byte order),
   exact in every register. **Gain.** 2 TBL per vector: 26 TBL for `savevmx_119/124`: 0.2 ms on JQ 0-2. Takes the
   same exemption in the guard. **Where.** `localize_native` (the `__savevmx_N`/`__restvmx_N` pair recogniser).

### VS-34  SPILL-USED: save only the registers the body writes

1. **Pattern.** `__savevmx_119` is the shared helper "save v119..v127"; the function saves all nine though its body may
   write only some. Evidence: `n_8250A7E8.h` calls `__savevmx_119` then `__savevmx_124` at entry and the two restores at
   exit; the body's `PPCVRegister v..` list shows which it writes.
2. **Transform.** Replace the helper with stores of the written registers only (and the matching restores).
3. **Not exact in memory** (the unwritten slots keep their old contents instead of the copy of the register, which is
   the same value - the caller's - so **the slot content is equal after the restore**: only the memory *during* the call
   differs; after return it is the same bytes in the slots the body used and the old contents elsewhere). Exact for
   everything but a reader during the call. **Gain.** 0 - 0.2 ms; mention only because it is cheaper to review than VS-32.

### VS-35  VEC-LIVEOUT-SITE: LiveOut per call site once the closure is inlined (extends 37)

1. **Pattern.** `n_8250A7E8.h` "Registers the guard does not compare for this hook: f0 .. f13, r3 .. r12, v0, v12, v13, v35 .. v63,
   v124 .. v127"; the return write-back lists in `n_822EA500.h` (`ctx.v0 = l_v0; ... ctx.v13 = l_v13`). SASRT's idea 37 keeps
   a register written back if **any** caller can read it (`out/leaf_live_out.json`, a whole-program union), and always the
   non-volatile ones.
2. **Transform.** After inlining a callee into its caller (the closure), liveness is **per call site**: the write-back of
   a register the continuation at this site overwrites before reading is dropped. The in-place transform keeps the
   out-of-line body's union LiveOut for real calls.
3. **Where.** `localize_native` (the dirty-set code, `dirty_sets()`), fed by the continuation's liveness.
4. **Exact.** A dead register write is invisible. **Gain.** 14 vector stores to `ctx` at each kernel return where the
   caller overwrites them: see VS-36. 0.2-0.5 ms with VS-36.

### VS-36  NATIVE-LIVEOUT: natives write only what the call site reads

1. **Pattern.** `blur_hand_natives_jq34.h`, `n_822EA500::Native`:
   ```
   simde_mm_store_ps(ctx.v0.f32, sv);  simde_mm_store_ps(ctx.v1.f32, av[12]); ... simde_mm_store_ps(ctx.v13.f32, rv[15]);
   ctx.r3.s64 = 112; ... ctx.r8.s64 = 208; ctx.r9.s64 = int64_t(r5) + 1056; ctx.r10.s64 = int64_t(r3) + 1056;
   ```
   14 vector stores and 8 integer stores a call, because hand.txt's `exact=` list names them: "the registers the native
   leaves as the original does". The caller `8230AD50` overwrites v1-v13 right after.
2. **Transform.** `Native<LiveMask>(ctx, base, s)`: a `constexpr` bitmask of the registers live after the site (VS-35) and
   `if constexpr (LiveMask & kV3)` around each write-back. The `exact=` list is the superset and the guard still
   compares those in the verify build.
3. **Where.** `localize_native` emits the mask at each `Native(...)` call (it already writes the call text); the
   natives' signature gains a template parameter (defaulted to "all", so the hooked world is unchanged).
4. **In place.** The native is `always_inline` (it is today: `[[gnu::always_inline]] inline bool Native`), so the dead stores
   vanish at compile time. **Exact.** Dead writes only. **Gain.** 22 stores out of ~290 instructions per 1 KB call, 5
   natives: 0.4-1 ms.

### VS-37  KEY-FOLD: per-function runtime keys become build constants

1. **Pattern.** `if (!On(kN822EA500)) [[unlikely]] return false;` at the top of every native, `s.Hand(..)`, `Hit(..)`; the
   brief: "Hook stubs cost only 0.4 %" and "runtime keys and in-run A/B become build switches".
2. **Transform.** `On(k)` becomes a `constexpr bool` from a CMake option list; `s.Hand/Hit/Note/Stopped` are already the
   `Fast` struct's constants (`banjo_hot_common.h`), so they fold today: what remains is the key load and branch per call.
   Nothing else to design; listed so that the 0.4 % is accounted for. **Gain.** 0.1-0.3 ms. **Risk.** None (compile
   time); the in-run A/B disappears, as the brief says.

---

## G. Scheduling, coverage, code shape

### VS-38  REORDER-STREAM: stream order with an alias check instead of 16 loads then 16 stores

1. **Pattern.** The kernels load 16 vectors, compute, store 16 (`n_822EA500.h`: `ea = (r11.u32 + r22.u32) & ~0xF; load -> l_v13` ... 30 loads,
   then the `vmaddfp` block, then 16 stores), because the original does and the destination may overlap a source: the
   hand note ("each block's loads before its stores") and `n_822EE628`. 32 q-registers are not enough: the project's
   count, on SASRT's own game, is "21,731 q-register stack transfers (3.2 % weighted), register pressure from up to 64
   vector locals" (`machine-copies-deferred.md`; not measured on Blur).
2. **Transform.** With the disjointness (or equality) check the natives use (`apart()`), run `load a, load b, op, store`
   per vector (3 live q regs, no spills, and the load of block k+1 can issue during the store of block k). Fallback: the
   original order.
3. **Where.** `post_codegen` function-level specialisation like VS-28 (it shares the entry test); the in-place frame.
4. **Exact*.** When dst equals a source exactly the per-vector order is independent (each vector is read before it is
   written at the same index); when disjoint, order is irrelevant. Memory order seen by other threads differs (allowed).
5. **Gain.** Spill removal: 0.5-2 ms depending on how much of the 3.2 % lives in these kernels (the natives use arrays
   `av[16], bv[16], rv[16]`, which GCC may also keep in memory). Medium-low confidence.

### VS-39  ADJ-PAIR: addresses written so that GCC pairs `ldr q` / `str q`

1. **Pattern.** `ea = (l_r10.u32 + r25.u32) & ~0xF;` then `ea = (l_r10.u32 + r26.u32) & ~0xF;` with r25, r26 literal
   16, 32 in registers: two separate masked sums GCC cannot prove adjacent (the masks differ by variable registers until
   constants are propagated, which they are not across the `REX_LOOP_IN` copies).
2. **Transform.** Write a run as `p = host(base_masked); store(p + 0), store(p + 16), ...` with literals (after VS-28 this is
   free). **Gain.** `stp q` for pairs: fewer store instructions in the kernels, if GCC does not already pair them.
   **Exact.** Same bytes. 0.2-0.5 ms at best; low confidence: look at the disassembly of `f_sub_822EA500` first and
   drop the idea if the pairs are there.

### VS-40  KERNEL-FAMILY: one templated body for the 1 KB kernels and their clones

1. **Pattern.** The kernels are text-identical but for one operation: `sub_822E9F20` (`vaddfp`) and `sub_822EA170`
   (`vmulfp`) differ in 36 diff lines of 272 (a `diff` of the two bodies with labels normalised: 18 changed lines of 272, `add_ps` vs `mul_ps`);
   `sub_822EA3C0` is the copy, `sub_822EA500` the multiply-add, `sub_822EE628` is `vaddfp` with a table loop,
   `sub_822E4FE8` / `sub_822E53C8` differ in 20 changed lines of 346, and 5 natives were written by hand for what is one shape.
2. **Transform.** A structural hash of the generated body (registers renamed, labels and constants normalised) groups the
   family; one `template<class Op> kernel1K(...)` (VS-03 + VS-08 + VS-18 + VS-28 + VS-38 + VS-11/14 built in once) is
   emitted and each member is `{ kernel1K<VAdd>(ctx, base, ...); }` with its exact write-back list (VS-36).
3. **Where.** A new pass `tools/kernel_family.py` before `localize_native`, with the family table checked into
   `app/src/cpu/hot/`; **this replaces hand natives** for the shapes it finds, and the next `hand_candidates` batch.
4. **In place / hooked.** In place: the member keeps its name and `ctx` interface and is an inlinable wrapper. Hooked:
   the same.
5. **Exact.** The kernel's declines are the original text; the template instantiates the op as the original expression
   (`REX_VMADDFP`, `simde_mm_add_ps`) so FP bits and flush mode are unchanged.
6. **Gain.** Mostly coverage: the five natives cover five functions; the family has `822EA170`, `822EE628`'s clones,
   `8230C748`... The saved effort is also the point: 0.5-2 ms and fewer hand lines to maintain.
7. **Verify.** Guard per member; a differential test between the template and the generated text for random data and
   the overlap cases.

### VS-41  NATIVE-PROLOGUE: a native is the first block of the function, not a hook in front of it

1. **Pattern.** `n_822EA500.h:608`: `if (!::banjo::hand::n_822EA500::Native(ctx, base, s)) f_sub_822EA500(ctx, base, s);` plus the cold
   `[[gnu::noinline, gnu::cold]]` copy; in the caller `8230AD50`: `if (!n_8230A4D8::Native(ctx, base, s)) { f_sub_8230A4D8(ctx, base, s); ... }`
   with the register hand-over around it.
2. **Transform.** In place the generated function body is `{ if (fast_case(ctx)) [[likely]] { kernel; return; } original }`:
   one function, no hook, the fast case inlines into callers that GCC inlines the function into; the original body is
   `unlikely`, and the verify build compares against the original text kept as `o_`.
3. **Where.** `localize_native` (emit the native call as the first block of the in-place function).
4. **Gain.** The call, the hand-over of up to 16 registers (`ctx.r3 = ...` / `l_r3 = ctx.r3`), and the `.isra` callee
   form: the brief says the 1.3 MB `.isra` callee forms "got ~0 samples" and "callers absorbed their callees". Neutral to
   positive (0 - 1 ms); required for the rest to be free. **Exact.** Same semantics, declines run the original.

### VS-42  TWIN-ICF: identical bodies folded

1. **Pattern.** Pairs differing only in an address or a constant: `sub_822E4FE8` / `sub_822E53C8` (405 lines each, same store counts), `sub_822E2190` .. `sub_822E2930` (about
   20 functions of 41-168 lines, 3 `stvx` each), `sub_822DE360` .. `sub_822DFFF0` (24-40 stores each). In the copies the closure pastes
   each twice (once per root): `n_8230AD50.h` and `n_8230B778.h` both hold `f_sub_822EA3C0`.
2. **Transform.** GCC `-fipa-icf` already folds equal functions; the generated tree has them as different text (different labels and
   addresses), so a pass normalises labels (`loc_822EA004` -> `loc_L`) and emits one shared body behind two thin
   wrappers. **Gain.** Smaller text, less I-cache pressure on a 3-core A57 (48 KB L1I): the 1.3 MB of unused `.isra` forms did
   not cost samples, so the benefit is only for the hot twins: 0 - 0.5 ms. **Exact.** Textual identity.

---

## 3. What the guard, the self-check and the tests must cover (common to all)

- **Guard (verify builds only, original text kept as `o_*`).** Every idea above keeps its original text as the fallback
  or the reference; the guard compares memory writes (the Logged form records ranges: a fused range is recorded once,
  the original records the pieces; `Note(addr, len)` handles both), the registers in the `exact=` list and the cards
  (`banjo_renut_store_cards_verify`). The ideas that change dead stack bytes (VS-32, VS-33, VS-34) need an exemption table;
  VS-11 / VS-12 need the PC card-verify run as their proof.
- **Start-up forms self-check** (`app/src/cpu/banjo_forms_selfcheck.cpp`): new instruction forms (`dc zva` and `DCZID_EL0`,
  `scvtf #n`, `ld1r` + `rev32`, the 16-alignment of every `ldur q` of VS-22, `rev32`) go in its table with edge inputs
  (zeros, `-0.0`, denormals, NaNs with payload, the 16 alignments, `ea & 15 > 12`).
- **`sdk/tests`.** `vec_unaligned_test.cpp` (add unaligned loads), `vec_lane_test.cpp` (static lane forms),
  `vec_byteswap_test.cpp` (the `rev32` domain), `memcalls_test.cpp` (inline 1 KB), `vmx_small_helpers_test.cpp` (the new
  pack/unpack table), and a `K1K` differential test for VS-40.
- **Console measurements the estimates wait for:** `tools/switch_hot_pcs.py` on `sub_8230AD50`, `sub_822BEDD0`, `sub_822E17C8`,
  `sub_8230EDE0` (how many samples sit on the `dcbzl` lines, the loads, the stores, the card marks); L1D refill / L2 miss
  counters if the console exposes them; TBL vs REV32 and `dc zva` vs 8 x `stp q` micro-benchmarks.

## 4. The order I would do them in

1. Measure (section 3, last bullet): the per-PC profile of the four roots decides VS-01/02/03 against VS-08..10.
2. Cheap and exact: VS-27 (`scvtf #n`), VS-08 (prefetch halves), VS-12/11 (card macros, behind a switch), VS-36/37, VS-22.
3. The in-place frame (VS-41), then VS-28 + VS-03 + VS-38 as one function-level specialisation (they share the entry test).
4. VS-40 once two or three kernels went through 3; delete the corresponding natives when the guard shows the template
   equal.
5. The domain pass (VS-18/19/20) once a micro-benchmark shows `rev32` wins.
6. Decisions for the user: VS-11 (card view gate), VS-32/33/34 (stack bytes).

## 5. Existing ideas touched (so none is repeated)

37 (LiveOut): VS-35, VS-36 extend it per call site and into the natives. 39 (card runs): VS-14, VS-15, VS-16, VS-17
extend it. 52 (prefetch in pointer-chasing loops): VS-10 is its table / stride form; VS-08 / VS-09 are new. 57 (stvlx /
stvrx and stvewx merges): VS-24 removes its run-time test, VS-16 its dead mark. 60 (invariant loads / barrier): VS-02
relies on its counted-loop test. 31 (guest stack in locals): VS-31 is its vector-lane case; VS-32 is the vector
save/restore case. 43 (NEON idioms B1-B4, F1/F2): VS-27 is a new B-type idiom (`scvtf #n`). 44 (flush mode): untouched.
64 (inline memcalls): VS-21 feeds it. 61 (unaligned load pairs; "none in Sonic's code"): VS-22 is that idea for Blur, where the
pairs exist. 72 (fused / estimate VMX): the kernels use `REX_VMADDFP` fused on the Switch; VS-18 and VS-38 keep it.

## 6. Considered and rejected (so nobody repeats them)

- **First writer instead of zero-then-accumulate** (`dst = b` where the original does `0 + b`): differs for `b = -0.0`
  (-> `+0.0`), for denormals under the flush mode and in the NaN bits; only VS-04's form (the add kept) is exact.
- **Non-temporal stores (`stnp`)** for the output blocks: the A57 treats them as ordinary pair stores, and the next kernel
  reads the block at once.
- **Copy elision** (a lazily materialised `822EA3C0` copy): the destination bytes must exist for the next reader.
- **`dcbz` (32-byte) -> `dc zva`**: the 64-byte block would zero 32 bytes more than the guest writes.
- **Read-modify-write narrowing** (`lvx; vrlimi128; stvx` of one lane at the same address -> a 4-byte store): equal
  single-threaded, differs only against a concurrent writer of the other 12 bytes; no hot instance was found in the
  JQ 3/4 closure (`82306F88`'s inserts store to other addresses), so not proposed.
- **A wider `Card()` page (64 KB cards)**: Renut's proofs are per 4 KB page.

## 7. Files this document points at

`app/generated/hot/n_8230AD50.h`, `n_8230EDE0.h`, `n_822EA500.h`, `n_829D2E80.h`, `n_8250A7E8.h`;
`app/generated/renut_recomp.*.cpp` (`sub_822D92D8`, `sub_822D9040`, `sub_822E46F8`, `sub_82244170`, `sub_8251E6F0`, `sub_82306F88`,
`sub_826803B0`, `sub_8230CBB8`, `sub_822E61E0`, `__savevmx_124`); `app/generated/renut_pch.h` (`REX_PREFETCH`, `REX_BSWAP128`,
`REX_SHUFFLE_TABLE`, `REX_CARD`); `app/src/cpu/banjo_hot_prelude.h`, `banjo_hot_common.h`, `hot/blur_hand_natives_jq34.h`,
`hot/hand.txt`; `tools/store_cards.py`, `vec_unaligned_stores.py`, `vec_lane_index.py`, `vec_byteswap.py`,
`localize_native.py` (`merge_vector_stores`, `loop_info`, the dirty sets); `sdk/include/rex/ppc/vec_lane.h`, `vec_unaligned.h`,
`memcalls.h`; `app/src/renut_engine/gpu/render/draw.cpp:1513-1580` (the card readers);
SASRT: `docs/machine-copies-1110.md`, `machine-copies-deferred.md`, `optimising-functions.md`, `pgo-lto.md`.
