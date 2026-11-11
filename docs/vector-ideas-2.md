# Vector (VMX / VMX128) arithmetic ideas for the base: VX-01 .. VX-40

Written 2026-11-11, read-only study (nothing built, run or edited besides this file). Companion of
`docs/vector-store-ideas.md` (VS-01 .. VS-42: vector stores, dcbz, prefetch, store cards, byte-order domains, unaligned
load/store, `stvewx`, spills, kernel families). This list covers what that one did not: **arithmetic, permutes, compares,
conversions, the flush-mode (VSCR NJ) handling, the simde layer and the recompiler's own emission of each vector
instruction.** Every idea below is tied to code found in `sdk/src/codegen/builders/vector.cpp`, `context.cpp`,
`sdk/include/rex/ppc/*.h`, `app/generated/renut_pch.h`, `tools/*.py`, and to counts over both generated trees. Where an
idea extends an existing number it says which (VS-nn, or SASRT nn from `docs/machine-copies-1110.md`,
`machine-copies-deferred.md`, `optimising-functions.md`).

## 0. How to read the numbers (method, and what the numbers are not)

- **Sites** are guest-instruction sites, counted from the `// mnemonic operands` comment line the generator writes before
  every statement group, over `app/generated/renut_recomp.*.cpp` (Blur: 186 files, 49,746 functions) and
  `D:\sonic transformed\sasrt-nx\app\generated\default\sasrt_recomp.*.cpp` (SASRT: 177 files, 134,918 functions). They are
  *static* counts. Dataflow census (constants, dead registers, merge trees) is a textual walk of each function with state
  reset at labels, i.e. a **lower bound** for facts that must hold on every path; where I allowed a single-definition
  register to cross labels I say "(single-def)" and it is an upper bound.
- **Hot sites** are sites inside functions that appear in `out/console/race_by_optimisation_copies.csv`. In that profile
  611 machine copies hold 4,101 of the 4,106 guest samples (status `machine copy`), so "in a hot function" means "in a copy".
  One sample = 0.0441 ms of CPU per frame (84 ms frame, 11.9 fps, from the VS document), summed over threads.
- **Estimates (ms/frame)** use one formula, stated so it can be argued with: `ms = sum over hot functions f of
  samples_f x 0.0441 x (sites_f x saved_instr) / (3 x guest_instr_f)`. `saved_instr` is my count of NEON instructions
  removed per site (from reading the macro / simde source, not from a disassembly: nothing could be built), and the 3 is an
  assumed NEON instructions per guest instruction. It is an instruction-share model: it ignores latency, and it ignores the
  cost of mode writes (`msr fpcr`), which can dominate (VX-01 .. VX-06). Treat every figure as an order of magnitude.
- **The vector-math family** (the hot functions that are mostly VMX arithmetic, not copy kernels): `sub_8250A7E8` 117
  samples, `sub_8251E6F0` 116, `sub_8252E168` 81, `sub_825123B8` 81, `sub_82504F90` 62, `sub_824F35D8` 58,
  `sub_825087A0` 57, `sub_82668520` 52, `sub_826EFA20` 49, `sub_826803B0` 49, `sub_8250C1A8` 41, `sub_82525FA8` 40 =
  **803 samples = 35 ms of CPU a frame**, spread over JQ Worker 0, 1, 2, 4 and Game Master (not the JQ 3/4 critical pair,
  which is the copy-kernel work of the VS list; `sub_822D92D8` 36 and `sub_82306F88` 30 are the JQ 3/4 vector-math
  exceptions). The whole list below is therefore a **sub-millisecond-per-idea** list on the console race: the sum of every
  *exact* idea is about 2 ms of 181 ms guest CPU a frame. I say so per idea; none of them is a JQ 3/4 critical-path item.
- **Cortex-A57 facts** (an `msr fpcr` costing "dozens of instructions", TBL with a register pair costing more than TBL with
  one, FMLA accumulator forwarding) are the project's own notes (`sdk/include/rex/ppc/diet_ops.h:200-206`,
  `docs/hand-natives-jq012.md:229`, `tools/vec_byteswap.py`) or the A57 manual from memory. **Each needs a micro-benchmark
  on the console before an idea resting on it is taken**; `sdk/tests/arm_forms_check.cpp` and the start-up forms self-check
  (`app/src/cpu/banjo_forms_selfcheck.cpp`) are the places to host one.
- **What the guard sees.** `app/src/cpu/banjo_hot_guest.cpp` compares a copy (`f_*`) with the original statements (`o_*`)
  *as they stand after post_codegen*. A change made in the SDK headers, in the rexglue codegen or in a post_codegen step is on
  both sides, so the guard cannot see it: only unit tests, the forms self-check and `tools/arm_forms_check.py` can. A change
  made inside `localize_native.py` (the in-place step) is seen by the guard if the `o_` text keeps the old expression. Each
  idea's "Verify" says which applies.

## 1. Census of the vector instruction stream

Top vector mnemonics (128 and non-128 forms merged where the semantics are equal; sites, Blur / SASRT):

| mnemonic | Blur | SASRT | | mnemonic | Blur | SASRT |
|---|---|---|---|---|---|---|
| `lvx128` / `lvx` | 39,230 | 23,438 | | `vsubfp128` | 7,770 | 1,640 |
| `stvx128` / `stvx` | 28,271 | 17,807 | | `vspltisw128` / `vspltisw` | 7,878 | 1,394 |
| `vor128` / `vor` (mostly register copies) | 23,179 | 6,864 | | `vcmpgtfp128` | 8,386 | 220 |
| `vspltw128` / `vspltw` | 20,591 | 11,879 | | `vsel` / `vsel128` | 5,683 | 2,225 |
| `vmulfp128` | 19,066 | 8,181 | | `stvewx128` / `stvewx` | 6,650 | 2,047 |
| `vrlimi128` | 17,728 | 1,234 | | `vnmsubfp` / `vnmsubfp128` | 5,845 | 2,572 |
| `vmrghw128` + `vmrglw128` | 24,024 | 1,866 | | `vmaddfp` / `vmaddfp128` / `vmaddcfp128` | 7,997 | 7,343 |
| `vpermwi128` | 10,099 | 3,837 | | `vaddfp128` | 5,464 | 1,182 |
| `lvlx128` / `lvlx` | 10,335 | 3,795 | | `vupkd3d128` | 2,412 | 951 |
| `vmsum3fp128` + `vmsum4fp128` | 14,889 | 2,328 | | `vrsqrtefp128` / `vrsqrtefp` | 2,087 | 1,052 |
| `vand128` | 6,592 | 203 | | `vrefp128` / `vrefp` | 1,258 | 270 |
| `vcmpequw128.` (record form) | 2,164 | 193 | | `vcuxwfp128` / `vcsxwfp128` | 1,971 | 530 |
| `vslw128` / `vsrw128` / `vsraw128` | 2,030 | 454 | | `vminfp128` + `vmaxfp128` | 1,917 | 303 |

Dependent pairs (producer, consumer within the same block; site counts; Blur): `vmrghw -> vmrghw` 11,163,
`lvlx -> vspltw` 8,748 (VS-25/26), `vpermwi -> vmulfp` 5,749, `vcmpgtfp -> vand` 5,341, `vrlimi -> stvx` 5,212 (VS),
`vmulfp -> vmaddfp` 4,727, `vmsum4fp -> vmrghw` 4,649, `vmulfp -> vnmsubfp` 4,412, `vrsqrtefp -> vmulfp` 4,130,
`vspltw -> vmulfp` 4,125 (SASRT 43 B1, done), `vmsum3fp -> vcmpgtfp` 4,043. SASRT: `vspltw -> vmaddfp` 4,483,
`vspltw -> vmulfp` 3,527, `vmulfp -> vmaddfp` 3,021, `vmaddfp -> vmaddfp` 2,994.

State of the generated tree (what this study found in the text, relevant to many ideas):

- **Vector registers**: in the 611 hot copies they are `l_vN` locals (the copy's `f_` body has 0 stores to `ctx.v*`; the
  write-back is at exits and real calls). In the generated functions themselves 72,541 of 248,842 vector register writes
  (29 %) go to `ctx.vN` (SASRT: 74,358): the dead-store passes (`leaf_dead_stores.py` line 106, `dead_ctx_defs.py`) know only
  r and f registers.
- **VMX float mode**: `ctx.fpscr.enableFlushMode()` remains as a real check at 5,190 sites (Blur) / 2,725 (SASRT);
  `disableFlushMode()` at 7,047 / 12,927; `enableFlushModeUnconditional` 264 / 367, `disableFlushModeUnconditional` 260 / 222;
  `REX_FLUSH_KNOWN_ON` 1,736 / 536.
- **Config in force** (`app/renut_config.toml`, generated text): `vmx_exact_minmax`, `vmx_fused_madd` (the codegen's helper
  form), `vmx_dot_overflow_qnan` are off (`vmaxfp` is `simde_mm_max_ps`, `vmaddfp` is `REX_VMADDFP`, `vmsum` is
  `simde_mm_dp_ps`). `fp_compares_modeless`, `fp_mode_lookahead(_past_branch)`, `fp_copy_pairs`, `fp_moves_modeless` are on.
  CMake `SASRT_VMX_FAST_ESTIMATES` and `SASRT_VMX_FUSED_MADD` default **ON** in `app/CMakeLists.txt:59-65` (Blur and SASRT),
  AArch64 only; `-ffp-contract=off` on the game targets (`app/CMakeLists.txt:199-206`).
- **VSCR**: `mfvscr` / `mtvscr` occur **0 times** in either tree. The SAT bit is never read and NJ is never written by the
  guest: the port models neither, correctly for these two games.

## 2. What the pipeline already does (not proposed again)

`tools/post_codegen.py` order, vector-relevant steps: `vsel_neon.py` (VMX `vsel` as one `bsl`), `vec_byteswap.py`
(`REX_BSWAP128` = `rev64` + `ext`, `REX_SHUFFLE_TABLE` = one `tbl`), `pch_neon_shuffles.py` (SASRT 43 B1: word shuffles in
the float lanes so `vspltw -> vmulfp` becomes `fmul v, v, v.s[k]`; B2: `rex_dp_ps_` without the trailing `dup`),
`vperm_one_source.py` (`REX_VPERM1`), `vupkd3d_neon.py` (type 0 as one `tbx`; B3), `vec_lane_index.py` (VS), `float_loads.py`,
`vmx_float_macros.py` (`REX_VRSQRTEFP/VREFP/VLOGEFP/VMADDFP/VNMSUBFP`), `flush_mode_flow.py`, `flush_hoist.py`,
`ro_constants*.py` (`REX_RO_V128`), `inline_memcalls.py`, `vec_unaligned_stores.py`. SDK: `intrinsics.h` (NEON `vperm`
via `vqtbl2q`, `vctsxs/vctuxs` as `fcvtzs/fcvtzu`, `vcfux`, `adds_epu32`, `avg`, per-lane shifts, `vmx_*_ps` helpers behind
config keys), `context.h` `CRRegister::setFromMask` (B4: `sshr` + `xtn` + one 64-bit compare), `diet_ops.h` (the any-mode scalar
helpers). The rest of this document is what remains.

## 3. Ranked table (expected gain across games, then confidence)

"Exact" = bit-identical to the current generated behaviour for every input (stronger than bit-identical to the Xenon,
which the current code is not in places); "decision" = changes results and needs the user. `ms` = Blur console race, CPU
ms per frame summed over threads, from the section-0 formula unless noted. SASRT sites are static.

| rank | ID | idea | Blur sites (hot) | SASRT sites | ms/frame | conf. | exact | lives in |
|---|---|---|---|---|---|---|---|---|
| 1 | VX-16 | `vnmsubfp` as one `fmls` (not neg-fma-neg) | 5,845 (578) | 2,572 | 0.3 - 0.6 | low-med (GCC may already do it) | NaN sign only | pch macro / SDK |
| 2 | VX-12 | fast `vrsqrtefp`: drop the special-case fix-up where a range/NaN `vsel` discards those lanes | 1,354 (~140) | 889 | 0.25 - 0.4 | med | exact vs fast form | post_codegen + pch |
| 3 | VX-01 | six int/round vector ops do not need the VMX mode | 2,138 (97) | 787 | 0.15 - 0.6 | med | exact | rexglue codegen |
| 4 | VX-15 | two dot products + merge = 2 `faddp` | 3,858 (320) | 16 | 0.2 - 0.3 | high | exact | post_codegen |
| 5 | VX-22 | constant-mask idioms (`vspltisw` / `vslw` / `vsrw`) folded | 1,556 (148) | 287 | 0.15 - 0.3 | low-med (GCC may fold part) | exact | post_codegen |
| 6 | VX-10 | `vrsqrtefp` estimate depth: 1 Newton step where the guest refines | 1,950 (207) | 1,027 | 0.2 - 0.26 | med | decision | pch macro + post_codegen |
| 7 | VX-20 | `vsrw` / `vsraw` / `vsrb` / `vrlw` as whole-vector NEON | 860 (38) | 61 | 0.2 | high | exact | rexglue codegen |
| 8 | VX-29 | `vupkd3d128` type 0 of a zero source = 1.0 splat | 1,314 (89) | 355 | 0.2 | high | exact | post_codegen |
| 9 | VX-37 | scalar `stfs` -> `lvlx128` -> `vspltw 0`: splat from the register, no load | 1,504 (hot weight 3,260) | 469 | 0.2 - 0.4 | med | exact | in-place transform |
| 10 | VX-02 | float constants from `vspltisw` + `vcuxwfp128` folded to literals | 1,238 (92) | 154 | 0.15 - 0.3 | high | exact | post_codegen |
| 11 | VX-04 | the cached mode word in a register local (copies) | 12,236 checks (1,307) | 15,646 | 0.2 - 0.6 | low-med | exact | in-place / pch |
| 12 | VX-14 | four dot products + merge tree = 3 `faddp` | 855 (86) | 8 | 0.14 | high | exact | post_codegen |
| 13 | VX-11 | `vrefp` estimate depth | 1,258 (224) | 270 | 0.17 | med | decision | pch macro |
| 14 | VX-38 | vector def/use in the LiveOut analysis (dead `ctx.v` stores, exits) | 72,541 ctx stores | 74,358 | 0.1 - 0.4 | med | exact | tools (leaf_dead_stores) |
| 15 | VX-23 | sign / abs masks as `fneg` / `fabs` | 2,224 | 222 | 0.1 - 0.2 | med | exact | post_codegen |
| 16 | VX-09 | two `vcmpgtfp` + `vand` against FLT_MIN / 2^126 as one integer range test | 1,124 (77) | 0 | 0.1 - 0.15 | med | exact | post_codegen + pch |
| 17 | VX-13 | cross product with 3 shuffles instead of 4 | 1,501 (81) | 342 | 0.1 | med | exact | post_codegen |
| 18 | VX-07 | `vcmpequw.` against zero -> one across-lane min / max | 1,540 (128) | ~15 | 0.08 - 0.15 | med | exact | SDK context.h + codegen |
| 19 | VX-17 | `vmaxfp` / `vminfp` as `fmax` / `fmin` (and compare+select pairs) | 1,917 (312) | 303 | 0.09 | med | decision | pch macro |
| 20 | VX-05 | flush-mode checks proven by the flow on the copy's closure / PRE | 6,514 of 12,236 | 7,058 | 0.1 - 0.4 | low | exact | tools (flush_mode_flow) |
| 21 | VX-30 | `vupkd3d128` FLOAT16 / NORMSHORT types in NEON | 283 (26) | 239 | 0.04 (SASRT more) | med | exact | post_codegen |
| 22 | VX-08 | `setFromMask` for canonical masks drops the `sshr` | 2,400 | ~1,500 | 0.05 | high (gain tiny) | exact | SDK context.h |
| 23 | VX-40 | clamp pair `vmaxfp -> vminfp` with one NaN fix-up (when exact min/max is wanted) | 294 | 28 | 0.04 | low | exact (AltiVec) | SDK + post_codegen |
| 24 | VX-21 | `vslw` / `vslh` / `vsrh`: the clamp after the 0x1F mask is dead | 1,174 (37) | 401 | 0.05 | high | exact | SDK helper |
| 25 | VX-06 | cluster independent scalar and vector float ops to cut mode switches | 3,785 weighted alternations | n/a | 0.1 - 0.5 | low | exact | post_codegen scheduler |
| 26 | VX-26 | `vupkh*` / `vupkl*` as `sxtl` / `sxtl2` (then `scvtf #n`, with VS-27) | 268 (40) | 209 | 0.04 | low (GCC may fold) | exact | SDK / codegen |
| 27 | VX-36 | splat of a splat is a copy | 30 (0) | 1,268 | 0 - 0.1 | low (GCC may fold) | exact | post_codegen |
| 28 | VX-19 | `|a - b|` = `fabd` | 106 | 0 | 0.01 | high | exact | post_codegen |
| 29 | VX-31 | `vperm` with a constant control that is a word permutation of one source | 0 | 104 | SASRT | med | exact | post_codegen |
| 30 | VX-32 | `vperm` with a constant word-aligned two-source control | 6 | 267 | SASRT | med | exact | post_codegen |
| 31 | VX-24 | saturating word ops (`vsubsws` scalar loop, `vaddsws`, `vsubuws`) | 31 | 101 | SASRT audio | high | exact | SDK helper |
| 32 | VX-33 | `lvsl` / `lvsr` + `vperm` as an offset TBL (no 256-byte table) | 327 vperms | 326 | 0.02 | low | exact | post_codegen |
| 33 | VX-34 | two-register TBL (`vperm`) vs two one-register TBL + `orr` | 512 (variable control) | 992 | 0.02 | low (benchmark) | exact | SDK helper |
| 34 | VX-35 | 4x4 transposes of four `lvx` as `ld4` | ~175 (cold) | ~127 | 0 - 0.02 | low | exact | in-place transform |
| 35 | VX-03 | `vcmpeqfp v, v, v` (NaN test) needs no mode | 118 | 257 | 0.01 | high | exact | rexglue codegen |
| 36 | VX-39 | `lvsr` row + `vcmpgtub` + `vsel` byte-select as one table row | 50 | 0 | 0.01 | med | exact | post_codegen |
| 37 | VX-25 | `vpkuhum` / `vpkuwum` as `uzp1` | 1 | 10 | 0 | high | exact | SDK / codegen |
| 38 | VX-27 | `vsplth` / `vspltb` as `dup` | 89 | 80 | 0.001 | high | exact | codegen |
| 39 | VX-28 | `vsl` / `vslo` / `vsro` / `vsr` without the memory round trip | 15 | 35 | 0 | high | exact | SDK helper |
| 40 | VX-18 | `vexptefp` polynomial with `fmla` | 8 | 58 | 0 (SASRT cold) | med | decision | rexglue codegen |

Honest summary: the sum of the exact rows 3, 4, 5, 7, 8, 9, 10, 12, 14, 15, 16, 17, 18 is about 2 - 3.5 ms of CPU a frame
on the console race (of 181 ms guest CPU; the vector-math family is 35 ms of it); the decisions (VX-10, 11, 17) add 0.4 - 0.5 ms.
Rows 1, 2, 5 and 11 depend on what GCC already does with the current text (marked): the first act for each is to read the
disassembly of one hot copy (`tools/switch_hot_pcs.py` with the ELF), which this study could not do.

---

## A. The flush mode (VSCR NJ) around vector ops

The 360's VMX unit always flushes denormals; the port reproduces that with FPCR.FZ and a cached mode word in
`ctx.fpscr.csr` (`sdk/include/rex/ppc/context.h:240-254`): every vector float instruction starts with `emit_set_flush_mode(true)`
(`sdk/src/codegen/builders/context.cpp:495`), i.e. `enableFlushMode()` (load csr, mask, compare, branch; the `msr fpcr`
only on a real change), `...Unconditional()` or a `REX_FLUSH_KNOWN_ON()` that the flow passes proved. Facts that bound
this section: `diet_ops.h:200-206` ("every switch is an `msr fpcr` that costs as much as dozens of instructions; SA2's
VMX-heavy game thread spent 20-26 % of its time right after one"), and `docs/hand-natives-jq012.md:229` (8 of 50 samples of
`sub_82525FA8` on the copy's `msr fpcr`).

### VX-01  MODELESS-CVT: six int/round vector ops do not need the VMX mode

1. **Pattern.** `ClassifyFloat` (`sdk/src/codegen/builders/context.cpp:694-746`) puts `VCFSX`, `VCFUX`, `VCSXWFP128`,
   `VCUXWFP128`, `VCTSXS`, `VCTUXS`, `VCFPSXWS128`, `VCFPUXWS128`, `VRFIN(128)`, `VRFIZ(128)` in the "needs VMX mode"
   class, and `build_vcfsx/vcfux/vctsxs/vctuxs/vrfin/vrfiz` (`vector.cpp:300-330, 821-880`) call `emit_set_flush_mode(true)`.
   Example, `sub_822D92D8` (JQ 3/4, 36 samples, the Q15 -> float kernel): `// vcsxwfp128 v50,v58,15` is followed by
   `ctx.fpscr.enableFlushMode();`, while the same function's scalar tail loop does `ctx.fpscr.disableFlushMode();`, so each
   call pays two mode writes around a conversion that cannot see a denormal. Census: Blur 2,138 sites, **966 still carry a
   mode statement** (869 checks, 81 `KNOWN_ON`, 13 unconditional, 3 hoist), 97 in hot functions (74 hot functions); SASRT
   787 sites, 154 with a statement. The conversions are also what the lookahead (`next_float_demand`, `context.cpp:790-810`)
   counts as a "VMX demand", so an `lfs` ahead of one stays in the any-mode form for nothing.
2. **Transform.** Drop `emit_set_flush_mode(true)` from those six builders and the six from `ClassifyFloat`'s `kVmx` list
   (they become `kOther`; `csrState` is untouched). Before: `ctx.fpscr.enableFlushMode(); store(cvtepi32_ps(...))`. After:
   `store(cvtepi32_ps(...))`. Why exact: int -> float yields 0 or a normal number, and the scale 2^-n (n <= 31) of a
   non-zero integer is >= 2^-31, normal; float -> int truncates, and a denormal input (and its product with 2^n, n <= 31,
   below 2^-95) truncates to 0 whether or not FZ flushed it first; `frintn` / `frintz` of a denormal give a signed zero
   whether FZ flushed the input or not. Every neighbouring mode-dependent instruction still carries its own check, so no
   proof elsewhere changes.
3. **Where.** rexglue codegen (`vector.cpp`, `context.cpp`); the analogue of sasrt-nx's `fp_compares_modeless` for vectors.
   Then re-run codegen and the post_codegen chain (all flow tools run on the new text).
4. **Exact.** For every input, both on AArch64 and in the PC build (`cvtdq2ps`, `cvttps2dq`, `roundps` are the same with
   DAZ/FTZ on or off for the inputs that matter).
5. **Gain.** Instruction share 0.16 ms/frame (3 instructions a site); the real term is the mode writes the sites cause
   when the surrounding mode is scalar (every `disableFlushMode` in `sub_822D92D8`'s tail, `sub_82525FA8`'s 8-of-50 `msr
   fpcr` samples): up to 0.6 ms. Risk low (re-codegen; the lookahead's decisions change slightly and must be re-read).
6. **Verify.** New `sdk/tests/vmx_modeless_test.cpp` (model: `vmx_small_helpers_test.cpp`): the six ops over denormals,
   +-0, +-inf, NaN, +-2^31, 2^-126 edges with FZ on and off against the current helpers; the same set in
   `banjo_forms_selfcheck.cpp` on the device (toggle FPCR.FZ around each); the PC verify run's `REX_FLUSH_KNOWN` counters.
   The guard cannot see it (codegen change).

### VX-02  CONST-FLOAT: float constants built from `vspltisw` + `vcuxwfp128` / `vcsxwfp128` become literals

1. **Pattern.** The XDK compiler builds float constants with integer splats: `sub_82504F90`: `vspltisw128 v127,1` /
   `vcuxwfp128 v122,v127,1` (= 0.5f), `vcuxwfp128 v51,v127,2` (0.25f). The generated text (`sub_825123B8`):
   ```
   // vcuxwfp128 v120,v121,1
   ctx.fpscr.enableFlushMode();
   simde_mm_store_ps(ctx.v120.f32, simde_mm_mul_ps(rex::ppc::simde_mm_cvtepu32_ps_(load(ctx.v121.u32)), set1(0x3F000000)));
   ```
   Census (source defined by a `vspltisw` earlier in the block): `vcuxwfp128` 1,081 of 1,137 Blur sites, `vcsxwfp128` 157
   of 834; hot sites 92 in 70 functions; SASRT 146 + 8. These constants are the 0.5 / 0.25 / 2.0 / 3.0 of the Newton-Raphson
   chains (VX-10 .. VX-12), so they sit in the hottest vector functions (`sub_8250A7E8`, `sub_8251E6F0`, `sub_825123B8`).
2. **Transform.** Evaluate at post_codegen time: `float(uint32(imm)) * 2^-n` with the host's IEEE nearest conversion, which is
   what `ucvtf` + `fmul` by a power of two produce, and emit `simde_mm_store_ps(vD.f32, simde_mm_castsi128_ps(
   simde_mm_set1_epi32(int(0x3F000000))))` (a `movi` / `fmov`), with no mode statement (VX-01 argues the conversion is
   mode-free). Shared mechanism with VX-22, VX-23, VX-29: one pass, `tools/vconst.py`, a block-local splat-constant
   propagator.
3. **Where.** A post_codegen pass before `localize_native.py` (so the copies get it), after `vperm_one_source.py`.
4. **Exact.** Yes (integer in [-16, 15] or its unsigned wrap; the scale is a power of two; no denormal result).
5. **Gain.** 4 instructions a site (cvt, mul, check, constant materialisation vs one `movi`): 0.19 ms/frame, the
   conversion's latency leaves the NR chains' critical path. High confidence. Risk low.
6. **Verify.** Pass self-test over all 32 immediates x 32 scales against `simde_mm_cvtepu32_ps_` + `mul_ps`; the pass is
   idempotent (post_codegen requirement). Not visible to the guard.

### VX-03  NAN-TEST: `vcmpeqfp v, v, v` needs no mode

1. **Pattern.** `sub_82AA3E58` (Blur): `vcmpeqfp128 v6,v38,v38` three times in a row; SASRT `sub_8280A5C0`:
   `vcmpeqfp v5,v12,v12` / `vsel v11,v9,v12,v5` (the NaN guard after a Newton step). Census, identical operands: Blur 118,
   SASRT 257 (and 889 SASRT `vrsqrtefp` chains end in such a guard, VX-12). A self-compare is false exactly for NaN; a denormal
   flushed to 0 still equals itself.
2. **Transform.** `build_vcmpeqfp`: if `operands[1] == operands[2]` do not call `emit_set_flush_mode(true)`; also
   `ClassifyFloat` needs the operand test (it only has the opcode: classify in the builder's own lookahead).
3. **Where.** rexglue codegen. **Exact.** Yes. **Gain.** 0.01 ms; listed because it is the same fix as VX-01 and removes a
   false "VMX demand". **Verify.** Add NaN / denormal self-compares to the VX-01 test.

### VX-04  CSR-SHADOW: the cached mode word in a local for the length of a call-free region

1. **Pattern.** 12,236 plain `enableFlushMode()` / `disableFlushMode()` checks remain in Blur (SASRT 15,646); 1,307 in hot
   functions. Each is `ldr w, [ctx, #csr]` + test + branch, and the load cannot be hoisted: guest stores go through `base`
   and GCC cannot prove they do not alias `ctx` (documented in `flush_mode_flow.py` and `flush_hoist.py`). Example
   (`sub_825087A0`, 57 samples, 26 static alternations): `vmulfp128` (`enableFlushModeUnconditional`), `lfs f0` (`disableFlushMode`),
   `fmadds` (`disableFlushModeUnconditional`), `vmulfp128` (`enableFlushModeUnconditional`).
2. **Transform.** Like `r1_local.py`: `uint32_t fz = ctx.fpscr.csr;` at function entry (copies: at the localized-register
   load), the checks test and update `fz`, written back to `ctx.fpscr.csr` (and the `msr` still executed on a change) at every
   statement that passes `ctx` to other code and at exits. A change is `REX_FLUSH_CHECK(fz)` macros in the pch.
3. **Where.** Pch macros + a post_codegen / localize step (the `REX_R1_LOCAL` family is the model).
4. **Exact.** Yes: the mode word is read and written in the same order; only the memory traffic changes. Any code that can
   change the mode behind the function's back (calls, hooks, `mtfsf`) is a sync point, exactly the set `flush_mode_flow.py`
   already lists as "unknown after".
5. **Gain.** One load fewer per check: 0.2 - 0.6 ms/frame by the section-0 formula (1,307 hot checks x 1 instruction), lower
   if the load hits L1 behind the branch. Confidence low-medium; GCC may already keep the word if `-fno-strict-aliasing`
   plus `__restrict` on `ctx` let it (the text says it cannot; the disassembly decides).
6. **Verify.** PC verify run (the `REX_FLUSH_KNOWN_*` check counters), the guard (copy vs original, both with the shadow:
   compare `csr` at exits), `banjo_hot_guest.cpp` already compares the mode word at exits.

### VX-05  MODE-PRE: the flush-mode flow on the copy's closure, and partial redundancy

1. **Pattern.** Of the 12,236 plain checks, **6,514 (53 %) are preceded in the function by a statement of the same direction**
   and still stay: 4,306 with a label in between (a join where a predecessor disagrees or is unknown), 2,199 with a call
   (the callee's mode summary says "unknown"), 8 with a block. SASRT: 7,058 of 15,646. `flush_mode_flow.py` documents the
   limits: unknown at entry unless the function is only called directly, unknown after hooked / indirect calls, and the
   copies (`localize_native.py`) are entered through hooks, so their entry is "unknown" even when every caller is a copy.
2. **Transform.** (a) Run the flow on the *copy's* text, where the closure's callees are `f_sub_X` bodies called from known
   states (the existing entry-mode logic with the copy's call graph instead of the image's). (b) PRE at joins: when all but
   one predecessor leave the mode in direction d and the remaining edge is cheap/cold, put the check on that edge and make
   the join's check `KNOWN`. Same macros (`REX_FLUSH_KNOWN_ON/OFF`), same PC verification.
3. **Where.** `tools/flush_mode_flow.py` (new lattice element "anticipated") and `localize_native.py`.
4. **Exact.** Yes (a check moves or is proven; the mode at every statement that needs one is unchanged).
5. **Gain.** The removable fraction is unknown (the 6,514 is an upper bound of "same direction before"); 0.1 - 0.4 ms if a
   third goes. Confidence low; effort high.
6. **Verify.** The PC run's `g_rex_flush_flow_checked` / "N had to switch the mode" lines; the guard compares `csr`.

### VX-06  MODE-CLUSTER: reorder independent scalar and vector float work inside a block

1. **Pattern.** Static mode alternations (a `disable` after an `enable` or the reverse, textual order) weight 3,785
   sample-sites in the hot functions: `sub_825087A0` 26, `sub_8252E168` 5, `sub_82505EF8` 8, `sub_82306F88` 5,
   `sub_82525FA8` 3, `sub_827E40D0` 8. The guest code interleaves them because the XDK scheduler was free to (scalar `lfs` /
   `fmadds` between `vmulfp128`s, quoted in VX-04). GCC cannot reorder across an `msr fpcr`.
2. **Transform.** A post_codegen basic-block scheduler: within a block (no label, branch, call, trap), move a run of
   independent scalar float statements next to the other scalar run, and vector next to vector, subject to register
   (r, f, v, cr), memory (loads and stores keep their order unless provably disjoint, e.g. both r1-relative with different
   offsets) and `mffs` / `mtfsf` dependences. The result has fewer alternations; `flush_mode_flow.py` then proves the rest.
3. **Where.** A new post_codegen step before `flush_mode_flow.py` (guest-text level; the statement groups are delimited by the
   `// mnemonic` comments).
4. **Exact.** Yes if the dependence test is conservative. **Gain.** Unknown without dynamic toggle counts (the PC
   census `kCensusFlushWrite` in `context.h` would give them); each avoided toggle is "dozens of instructions". 0.1 - 0.5 ms;
   confidence low; this is the riskiest idea in the list (a scheduler bug is silent). **Verify.** Guard + PC verify; a
   randomised dependence test over the block grammar.

---

## B. CR6, compares and masks

### VX-07  CR6-ALL: `vcmpequw.` against zero as one across-lane reduction

1. **Pattern.** 2,163 of 2,164 record-form `vcmpequw128.` consume CR6 through `mfocrf rX,2` + `rlwinm rY,rX,27,31,31` (2,028
   take the `eq` bit; SASRT takes `lt`: 62 `rlwinm 25,31,31`), the XDK's `XMVector4EqualR` / "all lanes greater" idiom.
   `sub_825087A0`:
   ```
   // vcmpgtfp128 v39,v42,v40
   // vcmpequw128. v38,v39,v127        ; v127 = 0
   cr6.setFromMask(simde_mm_load_ps(v38.f32), 0xF);
   // mfocrf r4,2 / rlwinm r3,r4,27,31,31 / cmplwi cr6,r3,0 / bne ...
   ```
   Census (the compared register is a zero splat, single-def): about 1,540 of 2,163 (716 of them compare a `vcmp*fp`
   result, 217 an AND of compares, 141 a loaded vector), 128 in hot functions (`sub_824F35D8`, `sub_82668520`,
   `sub_826803B0`, `sub_8250A7E8`); SASRT 15 (SASRT compares data against data, 171).
2. **Transform.** For `vcmpequw. vD, x, zero`: `cr6.lt = (max over lanes of x) == 0`, `cr6.eq = (min over lanes of x) != 0`
   (`vmaxvq_u32` / `vminvq_u32`; two instructions with the `fmov` instead of `cmeq` + `sshr` + `xtn` + `fmov` + compares).
   New `CRRegister::setFromZeroTest(simde__m128i x)`; the vD store is kept only if vD is live (it usually is not).
   Before: `cmeq; sshr; xtn; fmov x; cmp` (about 5). After: `umaxv`/`uminv; fmov; cmp` (3).
3. **Where.** `sdk/include/rex/ppc/context.h` (next to B4's `setFromMask`) and `build_vcmpequw` (`vector.cpp:718`) emitting it
   when operand 2 (or 1) is a register a pass knows is zero (a post_codegen annotation `REX_ZEROV(v127)`), or simply when the
   builder sees a `vspltisw/vxor` zero earlier in the block.
4. **Exact.** Yes for every input (it is the same two Boolean facts about the lanes).
5. **Gain.** 2 instructions a site: 0.08 - 0.15 ms. Medium confidence (A57 across-lane reductions have a multi-cycle latency;
   benchmark `umaxv` + `fmov` against `xtn` + `fmov`, which may be the better 2-instruction form: `uminp`/`xtn` on the
   canonical mask, see VX-08). Risk low.
6. **Verify.** Extend a `context.h` test: all 2^16 four-lane patterns of {0, nonzero} vs the movemask form; on the device
   in the forms self-check.

### VX-08  CR6-CANON: masks from `vcmp*` are all-ones / zero per lane

1. **Pattern.** `CRRegister::setFromMask` (`context.h:114-159`, SASRT 43 B4) computes `vshrq_n_s32(mask, 31)` before narrowing
   so that any mask works (`vcmpbfp` passes sign-bit masks). Every record-form compare other than `vcmpbfp` passes a canonical
   mask (cmeq/cmgt/fcmgt results: each lane all-ones or zero), for which the shift is a no-op. Sites: all `vcmp*.` (Blur
   about 2,400: 2,164 `vcmpequw128.`, 54 `vcmpgefp128.`, 53 `vcmpgtfp128.`, 31 `vcmpeqfp128.`, ...; SASRT about 1,500 by
   proportion).
2. **Transform.** `setFromCanonicalMask(mask)`: `s = vmovn_u32(mask)` as a 64-bit word; `lt = s == ~0; eq = s == 0` (no `sshr`;
   byte form: `vshrn_n_u16(mask,4)` without `sshr #7`). Emitted by the compare builders (which know the mask is canonical).
3. **Where.** `context.h` + the `vcmp*` builders. **Exact.** Yes. **Gain.** 1 instruction a site, 0.05 ms; high confidence in
   correctness, and GCC may already fold `sshr(cmeq)` (check the disassembly first). **Verify.** As VX-07.

### VX-09  RANGE-GUARD: `FLT_MIN < x < 2^126` as one integer compare

1. **Pattern.** Every reciprocal / reciprocal-square-root recipe of the XDK math library guards its result with
   `vcmpgtfp (2^126, x)`, `vcmpgtfp (x, FLT_MIN)`, `vand`. `sub_825123B8`:
   ```
   lvx128 v63,r0,r20      ; REX_RO_V128(0x82000B70, 0x7E8000007E800000ull, ...)   2^126
   lvx128 v62,r0,r25      ; ... 0x0080000000800000ull                             FLT_MIN
   vcmpgtfp128 v46,v63,v48 / vcmpgtfp128 v45,v48,v62 / vand128 v125,v45,v46
   ```
   The two constants are the commonest read-only vectors in the image: 1,003 loads of 0x00800000 and 784 of 0x7E800000
   (Blur). Census of the triple (both constants RO splats, same x): **1,124 in Blur** (77 hot sites), 0 in SASRT.
2. **Transform.** For positive normal bounds lo < hi the mask is `(bits(x) - (bits(lo)+1)) <u (bits(hi) - (bits(lo)+1))` (unsigned):
   `vsubq_u32(x_bits, movi) ; vcltq_u32(., movi)`. Negative x has the sign bit set so the unsigned difference is huge; NaN
   and infinity fail the upper bound; a denormal is below lo (and FZ makes the float compare false too); `x == lo` and
   `x == hi` fail both ways (strict). 2 instructions, 2 constants in `movi`/`mov` immediates, instead of 2 `fcmgt`, 1 `and`
   and 2 `ldr q` / `movi` constants. Macro `REX_RANGE_GUARD(x, lo_bits, hi_bits)`; the pass pattern-matches the triple and
   reads the literals out of `REX_RO_V128` (so it is not tied to these two constants).
3. **Where.** post_codegen (before `localize_native.py`) + pch macro. **Exact.** Yes, including the mode: the float compares
   were mode-dependent only through the denormal rule, which the integer test reproduces.
4. **Gain.** 3 instructions a site plus register pressure (two constants), 0.1 - 0.15 ms (77 hot sites). Medium confidence.
5. **Verify.** Exhaustive test over all 2^32 bit patterns of x against the three-instruction float form (seconds on the PC),
   plus the forms self-check on the device with a stratified sample.

---

## C. Estimates and Newton-Raphson refinement (extends SASRT 43 F1 / 72)

`REX_VRSQRTEFP` / `REX_VREFP` default to `frsqrte` / `frecpe` plus **two** Newton steps (`app/generated/renut_pch.h:553-625`,
`CMake SASRT_VMX_FAST_ESTIMATES` ON): the fuzz of SASRT's batch 3 shows 20.3 % / 32.3 % of lanes differ from the exact
`1/sqrt`, `1/x` by up to 3 / 1 ulp; the 360's own estimate is good to about 2^-12 (~2,000 ulp). The guest code then runs its own
refinement: **93 % of Blur's `vrsqrtefp` sites (1,950 of 2,087; SASRT 1,027 of 1,049) and 56 % of `vrefp` (705 of 1,258)** are followed
by the Newton-Raphson chain. From `sub_825123B8` (the normalize recipe, 1,354 times in Blur):
```
vmsum3fp128 v48,v119,v119          ; length^2 (2,365 dp3 of a vector with itself in Blur, 1,241 SASRT)
vrsqrtefp128 v0,v48                ; estimate
vcmpgtfp128 v46,v63,v48 / vcmpgtfp128 v45,v48,v62 / vand128 v125,v45,v46      ; range guard (VX-09)
vmulfp128 v12,v0,v0 / vmulfp128 v9,v0,v120                  ; e*e, 0.5*e
vnmsubfp128 v10,v13,v12,v10  /  vmaddfp v0,v10,v9,v0       ; e' = e + 0.5*e*(1 - x*e*e)
vsel128 v125,v11,v0,v125 / vmulfp128 v44,v119,v125          ; select (0 outside the range), v * e'
```

### VX-10  RSQRT-DEPTH: one internal Newton step where the guest refines

1. **Pattern.** As above: the port computes ~23 accurate bits (two steps, 13 NEON instructions with the special-case fix-up)
   and the guest throws half of them into a refinement written for a 12-bit estimate.
2. **Transform.** `rex_vrsqrtefp_fast_<1>`: `frsqrte` (8 bits) + one step (`fmul, frsqrts, fmul`) = about 15 bits, more than
   the 360's 12; after the guest's step the error is ~2^-30, below float rounding. The pass picks the depth by consumer: depth 1
   when the NR chain follows (the census pattern: `vmulfp e,e` and a `vmaddfp` whose addend is the estimate), depth 2 elsewhere
   (bare estimates 6.6 %); `REX_VMX_ESTIMATE_STEPS`. Saves `fmul, frsqrts, fmul` (3 instructions) and one dependent
   chain (about 15 cycles) per site.
3. **Where.** pch macro (`vmx_float_macros.py`) + a consumer-matching pass. **Not bit-exact**: the 2-step form is not the 360's
   either; the 1-step differs from today in the last bit of some lanes after the guest chain. **Needs the user's decision**
   (the rule for pixel-changing items, `machine-copies-deferred.md`). If a bit-exact 360 estimate table ever becomes
   available, a `tbl`-based exact form supersedes this whole section.
4. **Gain.** 207 hot sites: 0.2 - 0.26 ms/frame (3 instructions each). Medium confidence. Risk: lighting / normals differing by
   one ulp in a fraction of lanes. **Verify.** SASRT's `sasrt_test_vmx_fuzz` style (64 M lanes, FZ on, ulp histogram of the
   *composed* estimate + guest chain, not of the estimate alone) on the console; Blur has no such harness yet.

### VX-11  RECIP-DEPTH: the same for `vrefp`

1. **Pattern.** `sub_825087A0`: `vrefp128 v0,v51 ... vnmsubfp v10,v0,v12,v10 / vmaddfp v0,v0,v10,v0` (705 NR chains of 1,258;
   224 hot `vrefp` sites, 28 in `sub_825087A0`). `rex_vrefp_fast_` does `frecpe` + two `frecps` steps (5 instructions).
2. **Transform.** One step (8 -> 16 bits, above the 360's 12) before the guest's NR, as VX-10; bare uses (553) also fit (16 > 12 bits).
   Saves `frecps, fmul` = 2 instructions a site. 3. **Where / exactness.** As VX-10 (decision). 4. **Gain.** 0.17 ms. 5. **Verify.** As VX-10.

### VX-12  RSQRT-NOSPECIAL: drop the fast form's special-case fix-up where the guest select discards those lanes

1. **Pattern.** `rex_vrsqrtefp_fast_` ends with `special = (e0 == 0) | (|e0| == inf); r = bsl(special, e0, r)` (5 instructions, to keep
   `+-0 -> +-inf` and `inf -> 0` from turning into NaN through `0 * inf` in the steps; batch 3 audit finding 5). In **1,354 of
   2,087 Blur chains** the result reaches a `vsel` whose mask is the range-guard AND (`rsqrt_then_vsel_vand`; weight 2,766), whose
   false lanes are exactly the lanes where e0 is 0 or inf (x <= FLT_MIN, x >= 2^126, NaN); SASRT: 889 of 1,049 chains end in
   `vsel` with a `vcmpeqfp x,x` NaN mask (VX-03), where the special lanes also come out NaN after the guest NR and select the fallback.
2. **Transform.** `REX_VRSQRTEFP_NS(x)` without the fix-up when the pass proves the use: the estimate's value reaches only
   the NR chain whose result feeds a `vsel`/`bsl` with the guard mask (Blur) or a `x == x` mask (SASRT). Lanes outside the
   range then hold garbage that the select discards.
3. **Where.** post_codegen pattern + pch macro. **Exact** with respect to the current fast form (the discarded lanes are the
   only ones that differ); the SASRT variant needs the proof that the guard lanes cover the special inputs.
4. **Gain.** 5 instructions a site on about 140 hot sites: 0.25 - 0.4 ms/frame (the largest single exact item in section C).
   Medium confidence. **Verify.** Exhaustive 2^32-input test: for every x, compare `select(guard(x), fast(x), z)` of both
   forms; forms self-check on the device.

---

## D. Float arithmetic recipes

### VX-13  CROSS3: a cross product with 3 shuffles instead of 4

1. **Pattern.** `vpermwi128 ..., 135` and `..., 99` (the two cyclic permutations that keep w) are the commonest permutes
   (3,600 and 3,610 sites; SASRT 864 / 879). `sub_8251E6F0`:
   ```
   vpermwi128 v59,v62,135 / vpermwi128 v13,v62,99 / vpermwi128 v57,v60,99 / vpermwi128 v0,v60,135
   vmulfp128 v12,v57,v59 / vnmsubfp v12,v0,v13,v12        ; a x b
   ```
   Census (four shuffles with imm in {99,135}, a `vmulfp` of two of them, a `vnmsubfp` of the other two): **1,501 Blur (81 hot
   sites), 342 SASRT**.
2. **Transform.** With a = v60, b = v62: the original is r = rn(P99(a)*P135(b)) - fused(P135(a)*P99(b)). Compute
   `L = rn(a * P99(b)) - fused(P99(a) * b)` and `r = P99(L)`: lane algebra shows each lane pairs the same two products with the
   same rounded / fused roles and the same operand order (a's factor first in both), and the w lane is `rn(a_w b_w) - a_w b_w`
   in both. Three shuffles (`P99(b)`, `P99(a)`, `P99(L)`), all imm 99 (one `tbl` index vector), instead of four with two
   indices. Valid when the four original shuffle results are dead after the pair; all four sign/role combinations are matched
   by evaluating the lane permutations symbolically in the pass.
3. **Where.** post_codegen (before the copies); matcher on the instruction comments plus register liveness.
4. **Exact.** Yes, including NaN payload choice (operand order preserved). 5. **Gain.** One shuffle (1 - 2 NEON instructions) of
   about 12 a cross: 0.1 ms/frame. Medium confidence. 6. **Verify.** Pass self-test with random floats incl. inf/NaN against
   the four-shuffle form through `vmx_madd_ps`'s scalar model; the copy/original guard if done in `localize_native`.

### VX-14  DP-QUAD: four dot products merged into a vector = 3 `faddp`

1. **Pattern.** Matrix x vector and frustum/box tests compute four `vmsum4fp128` (each broadcast to all lanes) and merge them
   with a `vmrghw` tree. `sub_824F35D8` (58 samples): eight `vmsum4fp128` (`v35,v48,v59`, `v61,v56,v59`, `v53,v59,v60`, ...)
   then `vmrghw128 v39,v61,v33 / vmrghw128 v37,v55,v56 / vmrghw128 v32,v37,v39`. Census (merge nodes whose four leaves are four
   distinct dot-product results): **808 quads of `vmsum4fp128`, 47 of `vmsum3fp128`** in Blur (86 hot sites), SASRT 8;
   `vmsum4fp -> vmrghw` is 4,649 pairs.
2. **Transform.** `rex_dp_ps_` computes `p = a*b; faddp(p,p); faddp(.,.)` per dot product (3 + broadcast), then 3 `zip`. For the
   quad: `t01 = faddp(P0,P1); t23 = faddp(P2,P3); r = faddp(t01,t23)` gives lanes `[(P0.0+P0.1)+(P0.2+P0.3), (P1...), (P2...),
   (P3...)]`, the same additions in the same operand order as the four `rex_dp_ps_` results, and the dot-product choice per
   lane is whatever lane order the merge tree produced (choose the pairing of the products to match it). 4 `fmul` + 3
   `faddp` (7) instead of 4 `fmul` + 8 `faddp` + 3 `zip` (15). `vmsum3fp128` is the same with each product ANDed with the lane
   mask first.
3. **Where.** post_codegen: match the merge tree, require the four dot results dead after it, emit
   `rex_dp4_quad_(a0,b0,a1,b1,a2,b2,a3,b3, lane_order)` in the pch. Extends SASRT 43 B2.
4. **Exact.** Yes (same adds, same order; NaN payloads follow from the identical operand order). 5. **Gain.** 8 instructions a
   quad: 0.14 ms/frame (high confidence). 6. **Verify.** `sdk/tests/dp_quad_test.cpp` against four `rex_dp_ps_` + the merge
   tree incl. inf/NaN; forms self-check.

### VX-15  DP-PAIR: two dot products merged = 2 `faddp`

1. **Pattern.** `vmrghw`/`vmrglw` of two broadcast dot-product results: Blur 2,577 `vmsum4fp` pairs + 1,281 `vmsum3fp`
   pairs (320 hot sites; `sub_8251E6F0` 8, `sub_82451530` 31, `sub_824F35D8` 6), SASRT 16.
2. **Transform.** `t = faddp(P_a, P_b); r = faddp(t, t)` = `[dp_a, dp_b, dp_a, dp_b]` (= the `vmrghw` of the two broadcasts; `vmrglw`
   of broadcasts is the same vector). 2 `faddp` instead of 4 `faddp` + `zip`: 3 instructions.
3. **Where / Exact / Verify.** As VX-14 (same pass, pair case). 4. **Gain.** 0.2 - 0.3 ms/frame (the largest exact arithmetic
   item; high confidence). Risk low.

### VX-16  NMSUB-FMLS: `vnmsubfp` as one `fmls`

1. **Pattern.** `rex_vnmsubfp_fused_` (pch, `vmx_float_macros.py`) is `vnegq(vfmaq(vnegq(c), a, b))`, i.e. -(a*b - c) written as
   neg-fma-neg. The same value is `c - a*b` with one rounding = `fmls`. `vnmsubfp`/`vnmsubfp128`: Blur 5,845 (578 hot sites in
   108 functions), SASRT 2,572. AArch64's `fma` patterns fold `(fma (neg a) b c)` to `fmls`, but I found no pattern for
   `(neg (fma a b (neg c)))`; GCC's `match.pd` folds it only for internal-function FMA, not the target builtin behind `vfmaq_f32`
   (my reading, not verified).
2. **Transform.** `return vfmsq_f32(c, a, b);` (accumulator c, `fmls`). 1 instruction instead of up to 3.
3. **Where.** `renut_pch.h` macro text in `tools/vmx_float_macros.py`; same change in SASRT's copy and in `intrinsics.h`
   `vmx_madd_ps`'s NEON arm if it is used. 4. **Exact** except the *sign of a NaN result produced from the multiplier operand*:
   `fmls` negates only its first multiplicand, the current form flips every NaN result; with b NaN the current result has the
   opposite sign. A NaN input to `vnmsubfp` is a bug in the game's data; if even that must match today, `vfmsq_f32(c, b, a)`
   matches the b case instead and the a case differs, or keep the 3-op form for sites whose operands the pass cannot prove non-NaN.
5. **Gain.** 0.3 - 0.6 ms/frame (578 hot sites x 2 instructions) **if** GCC leaves the three instructions; first read one
   `vnmsubfp` in a hot copy's disassembly. A57 `fmla`/`fmls` accumulate has special forwarding of the accumulator, which the
   `c`-as-accumulator form uses. 6. **Verify.** `vmx_small_helpers_test` style over random + special floats against
   `std::fma(-a,b,c)`; fuzz harness; forms self-check.

### VX-17  MINMAX-FMAX: `vmaxfp` / `vminfp` as `fmax` / `fmin`; compare + select pairs

1. **Pattern.** With `vmx_exact_minmax` off, `vmaxfp` is `simde_mm_max_ps`, which simde implements for NEON as
   `vbslq(vcgtq(a,b), a, b)` (`sdk/thirdparty/simde/simde/x86/sse.h:2986-2987`, x86 semantics: the second operand for NaN and
   for equal zeros): 2 instructions and a dependent chain. Blur 925 + 990 sites (312 hot, 35 in `sub_825087A0`), SASRT 142 +
   161; plus **112 Blur `vsel(a, b, vcmpgtfp(a, b))`** selects of the compared operands (an open-coded max / min).
2. **Transform.** `vmaxq_f32` / `vminq_f32`: AltiVec's own answer for the zeros (max(+0,-0)=+0, min(-0,+0)=-0) and for NaN
   (the NaN, quieted); differs from AltiVec only for a quiet-NaN `a` with a signalling-NaN `b` (the exact `vmx_max_ps` handles that
   with 4 extra instructions). Closer to the 360 than today, one instruction cheaper, one cycle shorter.
3. **Where.** pch macro `REX_VMAXFP/VMINFP` + `build_vmaxfp/vminfp` (the codegen key `vmx_exact_minmax` already exists).
   **Not bit-exact to today** (NaN and signed-zero operands): **decision**. 4. **Gain.** 0.09 ms/frame; confidence medium.
5. **Verify.** Fuzz with NaN / +-0 inputs against the AltiVec definition (`vmx_max_ps`'s x86 branch is the reference).

### VX-18  EXPTE-FMA: the `vexptefp` polynomial with `fmla` (cold)

1. **Pattern.** `build_vexptefp` (`vector.cpp:178-215`) is a degree-5 polynomial in Horner form with separate `mul` + `add`
   (-ffp-contract=off) plus an exponent insertion; Blur 8 + SASRT 58 sites (`sub_826B4438`, `sub_826A2440`,
   `sub_82695EA8`), all cold in the Blur race. 2. **Transform.** Five `fmla`. 3. **Where.** The codegen builder. **Not exact**
   (the estimate's low bits move; it is a ~12-bit estimate of an unknown 360 table): decision. 4. **Gain.** 5 instructions a
   site, nothing measurable. 5. **Verify.** Error histogram against `exp2f`, the existing range cases (`vmx_expte_range`).

### VX-19  ABS-DIFF: `|a - b|` as `fabd`

1. **Pattern.** `vsubfp128` followed by `vand` with the 0x7FFFFFFF mask (built by the VX-22 idiom): 106 in Blur (`vsubfp ->
   abs`), 0 SASRT. 2. **Transform.** `vabdq_f32(a, b)` = `|a - b|` with the subtraction rounded once and the sign cleared (the
   `and` also clears a NaN's sign; `fabd` does the same). 3. **Where.** post_codegen after VX-22. 4. **Exact.** Yes.
5. **Gain.** 2 instructions a site and the mask register: 0.01 ms. 6. **Verify.** Pass self-test on random + special floats.

---

## E. Integer ops, shifts, constants and conversions

### VX-20  SHIFT-RIGHT-VEC: element-wise loops become whole-vector shifts

1. **Pattern.** `build_vsrw`, `build_vsraw`, `build_vsrb`, `build_vsrab`, `build_vrlw` (`vector.cpp:981-1019, 1086-1103`) emit
   four (sixteen) scalar statements, "TODO(tomc): vectorize": `sub_8251E6F0`:
   ```
   // vsrw128 v53,v61,v62
   v53.u32[0] = v61.u32[0] >> (v62.u8[0] & 0x1F);   ... u32[1] .. u8[4], u32[2] .. u8[8], u32[3] .. u8[12]
   ```
   Four 4-byte stores into the union followed by a 16-byte vector load of the whole register (`vand128 v47,v51,v53`): the A57
   cannot forward four narrow stores to one wide load, so the element form can stall as well as cost about 16 instructions.
   Census (shift count not constant): `vsrw128` 218 + `vsraw128` 49 (Blur, 38 hot sites), SASRT 20 + 34 + 3 `vrlw`.
2. **Transform.** `vshlq_u32(a, vnegq_s32(vreinterpretq_s32_u32(vandq_u32(b, 31))))` (3 instructions); `vshlq_s32` for `vsraw`;
   the byte forms with 7 and `vshl_u8`; `vrlw` = `ushl(a, n) | ushl(a, n - 32)`.
3. **Where.** `intrinsics.h` helpers (`vmx_srw`, ...) + the builders. 4. **Exact.** Yes (the count is the low 5 bits of the
   lane's low byte in both).
5. **Gain.** about 12 instructions a site and the forwarding hazard: 0.2 ms/frame on 38 hot sites. High confidence.
6. **Verify.** Add to `sdk/tests/vmx_small_helpers_test.cpp` (it already holds the per-lane shifts); forms self-check.

### VX-21  SHIFT-CLAMP: `vslw` and friends clamp a count that was just masked

1. **Pattern.** `build_vslw` masks the count with 0x1F and then calls `simde_mm_sllv_epi32`, which on NEON is `ushl` +
   `cmhi(count < 32)` + `and` (`avx2.h:4425-4427`): the compare and the AND can never fire. The same in `vslh`/`vsrh`/`vsrah`
   (`vector.cpp:956-1000` via `emit_vec_var_shift`) through `simde_mm_sllv_epi16` (`intrinsics.h`: already `vminq`-clamped, so
   cheaper but still a `umin` after a mask of 0xF). Blur: `vslw128` 1,172 (+2), 37 hot sites with a non-constant count;
   SASRT 396 + 5. SASRT 43 B5 mentions the redundant compare as "small" without a number.
2. **Transform.** `vshlq_u32(a, vandq_u32(b, 31))` (the mask stays, the clamp goes); for 16/8 bits the same.
3. **Where.** the builder emits `rex::ppc::vmx_slw(a, b)`; **exact**. 4. **Gain.** 2 instructions a site, 0.05 ms. 5. **Verify.** as
   VX-20.

### VX-22  MASK-FOLD: constants built by `vspltisw` / `vslw` / `vsrw` / `vand` / `vxor` evaluated at post_codegen

1. **Pattern.** The XDK builds the sign and abs masks with shifts of -1 and 1, per function: `sub_8251E6F0`:
   ```
   vspltisw128 v62,1 / vspltisw128 v61,-1
   vslw128 v57,v62,v61        ; 0x80000000 in every lane
   vsrw128 v53,v61,v62        ; 0x7FFFFFFF
   ```
   Census (block-local constant propagation through splats, shifts and logic): **963 `vslw` and 593 `vsrw`** of Blur's 1,174 + 811
   are fully constant (118 of 1,518 hot-weighted sample-sites each, 148 hot sites); SASRT 283 + 4. The count-dependent
   statements are the scalar loops of VX-20 and the compare-and-AND of VX-21; whether GCC folds them depends on the union
   type punning (the `v62.u8[0]` read of a `.u32` store), which SASRT's B5 note suggests it does not fully do.
2. **Transform.** `tools/vconst.py`: block-local evaluation over `vspltisw/b/h`, `vslw/vsrw/vsraw`, `vand/vandc/vor/vxor/vnor`,
   `vor` copies; replace the defining statements with `simde_mm_store_si128(vD.u32, simde_mm_set1_epi32(int(K)))` (or the
   non-uniform `set_epi32`). The same pass feeds VX-02, VX-23, VX-29.
3. **Where.** post_codegen before the copies. **Exact.** Yes. 4. **Gain.** Fully constant chains: Blur has 4,216 dependent
   constant instructions (943 `vcuxwfp`, 963 `vslw`, 593 `vsrw`, 156 `vcsxwfp`, 1,561 `vupkd3d`; SASRT 1,062) besides the
   7,864 plain splats; folding saves about 4 instructions each when GCC does not: 0.15 - 0.3 ms. Confidence low-medium until
   one hot copy's disassembly shows the chain surviving. 5. **Verify.** Idempotence; unit test of the evaluator against the
   simde helpers; not visible to the guard.

### VX-23  FNEG-FABS: sign / abs masks as `fneg` / `fabs`

1. **Pattern.** With VX-22's constants known: `vxor128 vD, x, signmask` = negate (**1,363 in Blur**: `sub_82504F90`
   `vxor128 v126,v57,v124` with v124 = 0x80000000), `vand128 x, 0x7FFFFFFF` = abs (**861**), `vandc` with the sign mask (SASRT
   120 abs, 102 xor). 2. **Transform.** `vnegq_f32` / `vabsq_f32` (pure bit operations on the sign bit, NaNs included): one
   instruction, no mask register live across the block. 3. **Where.** `vconst.py`. 4. **Exact.** Yes (bit-identical).
5. **Gain.** 1 instruction and one register a site: 0.1 - 0.2 ms (the mask is often live across 20+ vector instructions
   in the XDK's schedule). 6. **Verify.** Pass self-test.

### VX-24  SAT-FAMILY: `vsubsws`, `vaddsws`, `vsubuws` as one saturating instruction

1. **Pattern.** `build_vsubsws` is four scalar statements with 64-bit arithmetic and clamps ("TODO: vectorize"); `build_vaddsws`
   is an x86 overflow detect + `blendv` (6 instructions); `build_vsubuws` is `sub(a, min(a,b))`. Census: `vsubsws` 16 Blur (all
   in `sub_82264A80`) / 96 SASRT (`sub_82EB6B78`, `sub_82EADC18`, `sub_82EB23E0`, 32 each), `vaddsws` 15 Blur, `vsubuws` 5 SASRT.
   VSCR[SAT] is never read (0 `mfvscr`), so no flag need be kept.
2. **Transform.** `vqsubq_s32`, `vqaddq_s32`, `vqsubq_u32`. 3. **Where.** `intrinsics.h` helpers + builders. 4. **Exact.**
   Yes. 5. **Gain.** 7 - 20 instructions a site; SASRT's audio functions are in the AK::EventManager thread; Blur nothing
   measurable. 6. **Verify.** `vmx_small_helpers_test.cpp` (it already compares saturating add against a scalar statement).

### VX-25  PACK-UZP: `vpkuhum` / `vpkuwum` are `uzp1`

1. **Pattern.** `build_vpkuhum` masks both inputs with 0xFF and calls `packus_epi16` (two `and`, two `sqxtun`/`combine`);
   a modulo pack is `uzp1` on the right element size. `vpkuwum` likewise. Census: `vpkuhum128` 1 Blur / 8 SASRT (`sub_8280E320`
   family), `vpkuwum128` 1 / 0; also `vpkuhus` / `vpkuwus` are the scalar loops through `v_temp`. 2. **Transform.** `vuzp1q_u8` /
   `vuzp1q_u16`. 3. **Where / Exact / Verify.** `vector.cpp`; exact; extend the helper test. 4. **Gain.** 3 instructions a
   site on cold sites; listed because it is the whole fix.

### VX-26  UNPACK-SXTL2: `vupkh*` / `vupkl*` and the unpack -> convert pipeline

1. **Pattern.** `sub_822D92D8`: `vupkhsb128 v58,v61,v96` emits `simde_mm_cvtepi16_epi32(simde_mm_unpackhi_epi64(x, x))`, then `vcsxwfp128 v50,v58,15`.
   Census: 251 `vupkhsb128` + 17 `vupklsb128` in Blur (40 hot sites), 113 + 96 in SASRT; `vcsxwfp <- vupkhsb` is 251 of the 834
   `vcsxwfp128`. 2. **Transform.** `vmovl_high_s16` (`sxtl2`) for the high half, `vmovl_s16(vget_low_s16(x))` for the low
   half; then `scvtf #fbits` takes the Q15 scale (VS-27's fold). GCC probably already forms `sxtl2` from
   `vcombine(vget_high, vget_high)`; check. 3. **Where.** `intrinsics.h` / builders. 4. **Exact.** Yes. 5. **Gain.** about 1
   instruction a site, 0.04 ms; the pipeline view is VS-19 / VS-27. 6. **Verify.** helper test.

### VX-27  SPLAT-DUP: `vsplth` / `vspltb` as `dup`

1. **Pattern.** `build_vsplth/vspltb` emit `shuffle_epi8(x, set1_epi16/epi8(const))`: a `tbl` with a constant index vector
   (a literal load) where `dup v.8h, v.h[k]` is one instruction. Blur 32 + 57 (`sub_826DE9A0` 12), SASRT 80 + 2
   (`sub_82D1F6D0` 55, a half-float decode). 2. **Transform.** `vdupq_laneq_u16(x, 7 - k)` / `vdupq_laneq_u8`. 3. **Where /
   Exact / Verify.** codegen; exact; helper test. 4. **Gain.** 2 instructions and a literal a site; nothing hot in Blur.

### VX-28  WHOLE-SHIFT: `vsl` / `vslo` / `vsro` / `vsr` without the memory round trip

1. **Pattern.** `rex::ppc::simde_mm_vslo` / `vsro` store the vector to the stack, `memcpy` with the run-time shift, reload
   (`intrinsics.h:280-340`); `vsl` goes through `vld1q_u64` / `vst1q` / a lane-by-lane rebuild; `vsr` uses simde SSE4. Blur
   3 + 3 + 3 + 6 (`sub_82552800`, `sub_82551C08`), SASRT 18 + 17 (`sub_827EAC80`, `sub_827EB060`, 8-9 each). Each is a 16-byte
   store followed by narrower loads and a 16-byte reload: a forwarding stall. 2. **Transform.** `vslo`/`vsro`: a `tbl` with the index
   `iota + n` (and `0xFF` out of range -> 0); `vsl`/`vsr`: `ushl`/`ushr` of the 64-bit halves with the carry from `ext`.
3. **Where / Exact / Verify.** `intrinsics.h` helpers; exact; extend `vmx_helpers_neon_test.cpp`. 4. **Gain.** about 10
   instructions and a stall per site on cold code.

---

## F. D3D formats

### VX-29  UPKD3D-ONE: `vupkd3d128` type 0 of a zero source is the constant 1.0

1. **Pattern.** `vupkd3d128 vD,vZ,0` with vZ a zero register (`vspltisw128 v127,0`) builds (1,1,1,1) so the XDK can use it as
   the float 1.0: `sub_825123B8`: `vupkd3d128 v126,v127,0` -> `REX_VUPKD3D_COLOR(vTemp, ctx.v127); ctx.v126 = vTemp;` (SASRT 43 B3
   makes it one `tbx` over a loaded vector). Census: **1,314 of 2,128 Blur type-0 sites** (89 hot sites, 82 hot functions;
   `sub_826803B0` 4, `sub_825123B8` 2), SASRT 355 of 704. 2. **Transform.** `set1_ps(1.0f)` (a `fmov v.4s, #1.0`),
   no load of the source, no table. 3. **Where.** `vconst.py` (the source's zero-ness is the evaluator's fact). 4. **Exact.** Yes
   (the `tbx` result for a zero source is 0x3F800000 in every lane). 5. **Gain.** 5 instructions and a literal a site, 0.2 ms.
   High confidence. 6. **Verify.** Pass self-test against `REX_VUPKD3D_COLOR`.

### VX-30  UPKD3D-FAMILY: the other `vupkd3d128` / `vpkd3d128` types in NEON

1. **Pattern.** `build_vupkd3d128` (`vector.cpp:1482-1622`): the NORMSHORT2/4 (imm 4 / 16), FLOAT16_2/4 (12 / 20) cases are
   scalar statements writing vD lane by lane, FLOAT16 with a per-lane branch for the exponent-0 case. Census: imm 12 (FLOAT16_2)
   217 Blur (`sub_82451530` 12 samples, 6 each at three sites, `sub_82193598`, `sub_82174570`), imm 4: 33, imm 16: 27, imm 20: 6;
   SASRT imm 4: 161 (`sub_82AD62D8`, `sub_827603F8`), imm 20: 78 (75 in `sub_82D1E250`). `vpkd3d128`: Blur 35, SASRT 69.
   2. **Transform.** FLOAT16: `uxtl` (u16 -> u32), then `((h & 0x8000) << 16) | (((h & 0x7C00) + 0x1C000) << 13) | ((h & 0x3FF)
   << 13)` with the exponent-0 lanes selected to sign-only by one `bsl` (the current formula, which does *not* preserve inf/NaN,
   so `fcvtl` would be wrong), 8 instructions for 4 lanes instead of about 40. NORMSHORT: `sxtl` + `add 0x40400000` pattern.
   3. **Where.** `vupkd3d_neon.py`'s sibling pass / the builder. 4. **Exact** to the current formulas. 5. **Gain.** 0.04 ms Blur
   (the hot sites are few), more for SASRT's half-float decoder. 6. **Verify.** All 65,536 half values and a sample of
   NORMSHORT words against the scalar statements, in a new `sdk/tests/vupkd3d_test.cpp`; forms self-check.

---

## G. Permutes

### VX-31  VPERM-SINGLE: constant-control `vperm` that permutes whole words of one source

1. **Pattern.** SASRT: `vperm128` with a read-only control: 506 sites (of 1,419); 104 permute whole words of one source (`word_perm_src_a`),
   28 are lane selects between the two, 267 word permutes using both, 107 general byte permutes (`sub_826FEA80`: controls
   `0x1c1d1e1f 0x14151617 0x0c0d0e0f 0x04050607` repeated). Blur: 8 sites (variable controls dominate: 674).
2. **Transform.** Classify the 16 control bytes at post_codegen: word-aligned single source -> `vpermwi`-style
   `simde_mm_shuffle_epi32` (which lowers to `zip/uzp/ext/dup/ins` or one `tbl`); lane select -> `bsl` with a constant mask or `ins`.
   The two-register `tbl` and the `and`/`eor` on the control are dropped. 3. **Where.** post_codegen next to `vperm_one_source.py`
   (it already has the one-source case). 4. **Exact.** Yes. 5. **Gain.** SASRT: 3 - 4 instructions and a literal per site; Blur
   none. 6. **Verify.** `neon_shuffles_test.cpp` style: all classified controls against `simde_mm_perm_epi8_`.

### VX-32  VPERM-2SRC: two-source word-aligned constant controls

1. **Pattern.** The 267 SASRT sites above: e.g. `0x1c1d1e1f,0x14151617,0x0c0d0e0f,0x04050607` = words (7,5,3,1) of the 8-word
   concatenation = `uzp2` of the two registers (odd words); `0x10111213...` patterns are `zip`. 2. **Transform.** classify into
   `uzp1/uzp2/zip1/zip2/trn1/trn2/ext` and emit the one NEON instruction (the lane reversal of the register image maps the
   classes onto each other with the operands swapped); remaining word controls get a `tbl2` with a preformed index (no
   `and`/`eor`). 3. **Where / Exact / Verify.** as VX-31. 4. **Gain.** SASRT only (Blur 6 sites); 2 - 3 instructions a site.

### VX-33  VPERM-LVS: `lvsl` / `lvsr` + `vperm` as an offset lookup

1. **Pattern.** Blur: of 682 `vperm` sites 196 take a control from `lvsl` and 131 from `lvsr` (and 346 from an `lvx` of data);
   SASRT 166 + 160. `lvsl` loads a 16-byte row of the 256-byte `VectorShiftTableL` (`memory.cpp:637-653`), then `vperm` does
   `and 0x1F; eor 0x0F; tbl2`. `sub_82236E78` (19 samples): `lvsl v0,r0,r4` ... `vperm128 v63,v63,v62,v0`; `sub_829D2E80` uses `lvsr`.
2. **Transform.** For `vperm(a, b, lvsl(sa))` the result is bytes `[16-sa .. 31-sa]` of `{b, a}`: index vector `iota + (16 - sa)`
   (`dup` + `add` with a constant) into one `tbl2` of `{b,a}`, no table row, no `and`/`eor`; when the control is loop-invariant GCC
   already hoists the `and`/`eor`, so the gain is outside loops. 3. **Where.** post_codegen pattern on `lvsl/lvsr -> vperm`
   (control used only by vperms). 4. **Exact.** Yes. 5. **Gain.** 3 instructions and a 16-byte load a site, 0.02 ms. Low. 6. **Verify.**
   16 values of sa against the table form.

### VX-34  VPERM-SPLIT: `tbl` with a register pair vs two one-register `tbl` and an `orr`

1. **Pattern.** `simde_mm_perm_epi8_` uses `vqtbl2q_u8({a, b}, idx)` (`intrinsics.h:176-186`). A two-register table needs the two
   registers consecutive, so GCC inserts `mov`s when the allocator cannot give a pair; the A57 manual lists TBL with two
   registers as slower than with one (my memory). Variable-control `vperm` with `a != b`: Blur 512, SASRT 992.
2. **Transform.** Candidate: `vqtbl1q(a, idx) | vqtbl1q(b, idx - 16)` (out of range gives 0): 2 `tbl` + `sub` + `orr`. Only a
   micro-benchmark can say which is faster on the A57 (`arm_forms_check.cpp`). 3. **Where.** `intrinsics.h`. 4. **Exact.** Yes.
   5. **Gain.** unknown (0.02 ms if the alternative wins). 6. **Verify.** helper test.

### VX-35  TRANSPOSE-LD4: a 4x4 transpose of four consecutive `lvx` as `ld4`

1. **Pattern.** Merge trees whose four outputs each combine the same four `lvx128` loads: Blur 701 merge outputs (about 175
   transposes; `sub_823BEAC0`, hot weight only 76), SASRT 508 (`sub_8280E320`). Each is 4 x (`ldr q` + `rev64` + `ext`) +
   8 `zip` = 20 instructions. 2. **Transform.** When the four addresses are `p, p+16, p+32, p+48` and `p` is 16-aligned
   (proved from `addi`/`lvx` forms or checked at run time with a fallback), `ld4 {v.4s}` + four `rev32` = 5. The mirror image
   `st4` for the 4x4 store. 3. **Where.** in-place transform (it must see the addresses). 4. **Exact.** Yes if the addresses
   are proven (the memory read is the same 64 bytes). 5. **Gain.** 15 instructions a transpose; cold in Blur, so 0 - 0.02 ms.
   Related: VS-18 / VS-19 (domains). 6. **Verify.** the guard, unit test over alignments.

### VX-36  SPLAT-FOLD: a splat of a splat is a copy

1. **Pattern.** `vspltw128 v58,v59,0` where v59 was just produced by `vspltw128 v59,v61,0`: **1,268 SASRT sites** (11 % of its
   `vspltw`), 30 Blur. Lane choice is irrelevant (all lanes equal). 2. **Transform.** A copy (`vD = vS`). GCC folds nested
   `VEC_PERM_EXPR`s when the intermediate is an SSA value; the union `.u32` store/load between them may hide it. 3. **Where.**
   post_codegen peephole (`pch_neon_shuffles.py`'s sibling). 4. **Exact.** Yes. 5. **Gain.** 1 instruction a site if GCC does not
   fold it, else 0. Check one SASRT copy before doing it. 6. **Verify.** pass self-test.

---

## H. Register traffic, liveness and recipes that cross instructions

### VX-37  SPLAT-FROM-REG: a scalar float stored to the stack and reloaded as a vector splat

1. **Pattern.** The XDK has no float-register -> vector move: it stores the scalar and reloads it with `lvlx` + `vspltw`.
   `sub_825123B8`:
   ```
   // stfs f1,80(r1)       temp.f32 = DoubleToSingleAnyMode(ctx.f1.f64);  REX_STORE_D_U32(r1l.u32, 80, temp.u32);
   // addi r6,r1,80 ; ... ; lvlx128 v49,r0,r6   -> 16-byte aligned load + tbl row
   // vspltw128 v47,v49,0   -> shuffle_epi32(..., 0xFF)
   ```
   A 4-byte store followed by a 16-byte load of the line it sits in cannot be forwarded on the A57 (the wider load waits for the
   store to commit). Census (an `r1`-relative `stfs`/`stw`, no other store or call in between, `lvlx128`/`lvx128` of the same slot,
   within 40 statements): **`lvlx128` after `stfs`+`vspltw 0`: 1,367 Blur (hot weight 3,189), `stw`: 137; 463 + 6 SASRT**; with no
   splat consumer another 365 + 122 + 89 + 62 (Blur). VS-25 would turn the load into a 4-byte load; this idea needs no load at all.
2. **Transform.** `v47 = vdupq_n_u32(bits)` with `bits` the value just stored (`temp.u32`), the store kept (guest memory
   unchanged), the `lvlx` removed when v49 has no other use (otherwise `v49` = `{bits, 0, 0, 0}`-style from the same value for
   an aligned slot). Preconditions proven in the pass: same `addi r,r1,K` base, K % 4 == 0, lane 0 word wholly inside the quadword,
   no intervening store that may overlap (stores through other bases, calls, labels, `stvx`/`dcbz` kill the pending slot).
3. **Where.** `localize_native.py` / a frame-slot dataflow like `frame_traffic.py` and `store_cards.py`'s r1-derived rule.
   Extends VS-25 (load width) and is the converse of VS-31 (`stvewx` then scalar load). 4. **Exact.** Yes. 5. **Gain.** a load,
   a `tbl` and a `dup` become one `dup`, and the store-to-load stall goes: 0.2 - 0.4 ms/frame (hot weight 3,189 site-samples).
   Medium confidence. 6. **Verify.** The guard (copy vs original compare registers and stores), pass tests on the slot logic.

### VX-38  VEC-DEADSTORE: vector def/use in the LiveOut analysis

1. **Pattern.** `tools/leaf_dead_stores.py:defuse()` returns `(0, 0)` for every `v*` mnemonic ("vector arithmetic: no general or float
   register"), so UE / LiveOut and `dead_ctx_defs.py` never see vector registers: 72,541 of Blur's 248,842 vector register writes
   (29 %) are stores to `ctx.vN`, 74,358 in SASRT. In the copies the registers are locals but the exits and real calls write
   them all back (`n_8250A7E8.h`: "75 context registers ... written back at a return"; calls "stored before / loaded after" up to 6/75,
   17/52, 16/52). VS-35 and VS-36 assume the analysis exists. 2. **Transform.** A vector def/use table (writes: operand 0 of
   `v*`/`lv*`; reads: the other `v` operands, `stv*` operand 0, `vrlimi`/`vmaddcfp`/`vsel` read operand 0 too; `bl` reads the
   callee's UE), UE and LiveOut for `v0 .. v127` to the same fixed point; then `REX_DEAD_DEF` for dead vector writes to ctx in
   the generated functions and the continuation sets of VS-35 in the copies. 3. **Where.** `leaf_dead_stores.py` /
   `dead_ctx_defs.py`, ahead of `localize_native.py`. 4. **Exact.** Yes if conservative (unknown calls read everything;
   `__savevmx`/`__restvmx` are reads/writes of the saved set). 5. **Gain.** The write-backs at copy exits plus code size
   (72k stores): 0.1 - 0.4 ms/frame, more on JQ 0-2 (the `sub_8250A7E8` family). 6. **Verify.** The PC poison mode the GPR version uses
   (`REX_LEAF_DEAD_POISON`) with a vector pattern, the guard.

### VX-39  LVS-MASK: the unaligned-merge byte select as one table row

1. **Pattern.** The XDK's unaligned-copy tail: `sub_829D2E80` (33 samples, JQ 1/2/4):
   ```
   lvsr v12,r9,r5 ; vcmpgtub v12,v11,v12 ; vsel v12,v0,v13,v12 ; stvlx v12,r29,r4 ; stvrx v12,r6,r21
   ```
   `lvsr` loads a table row, `vcmpgtub` compares it with a constant vector v11 (`lvx128 v11,r0,r9`). Blur 50 `vcmpgtub` sites. 2.
   **Transform.** `vcmpgtub(const, lvsr(sa))` depends on `sa & 15` only, so a precomputed 16 x 16 byte table (built once from the
   loaded constant, which is read-only) turns the row load + `cmhi` into one load; or `cmhi` against `dup`+`add`. 3. **Where.**
   post_codegen pattern on `lvsr -> vcmpgtub -> vsel`. 4. **Exact.** Yes given a read-only constant. 5. **Gain.** about 2
   instructions a site; 0.01 ms. 6. **Verify.** 16 values of sa.

### VX-40  CLAMP-PAIR: `vmaxfp` -> `vminfp` with one NaN fix-up (when exact min/max is wanted)

1. **Pattern.** 294 Blur clamp pairs (a `vmaxfp` result feeding a `vminfp`, or the reverse; 17 + 18 in `sub_825087A0`), 28 SASRT.
   The codegen's `vmx_exact_minmax` helper (`intrinsics.h:~455-470`, off in Blur) costs `fmax` + `fcmeq` + `mvn` + `orr` + `bsl` = 5
   instructions a use, so a clamp is 10. 2. **Transform.** `r = fmin(fmax(x, lo), hi)`; `r = bsl(x is NaN, quiet(x), r)` once: the
   first operation passes a NaN `x` through unchanged (quieted), the second keeps it, so one fix-up covers both (valid when lo and
   hi are not NaN; the pass requires them to be splats of constants or of results of non-NaN-producing ops, or keeps the 5-op form).
   6 instructions. 3. **Where.** post_codegen + SDK macro. 4. **Exact** with respect to `vmx_exact_minmax` (AltiVec semantics),
   *not* to today's `simde_mm_max_ps`; relevant only if the key is switched on (and then VX-17 is moot). 5. **Gain.** 4
   instructions a pair: 0.04 ms. Low. 6. **Verify.** fuzz with NaN / +-0 / inf bounds against the helper pair.

---

## 4. Considered and rejected (so nobody repeats them)

- **`vmaddfp` / `vnmsubfp` with a zero addend as a multiply** (the AltiVec "no vmulfp" idiom): not found. The block-local census
  finds the addend is +0 in 1 Blur site; the XDK uses `vmulfp128` for products. (A +0 addend is also not a plain multiply:
  -0 + +0 = +0.)
- **`vrlimi128` / `vmrg*` / `vpermwi128` / `vsldoi128` / `vcmp*` / `vsel` into `ins` / `zip` / `ext` / `bsl`**: single instructions
  already (SASRT's disassembly study, `machine-copies-deferred.md` "Ruled out"); `vrlimi` is 14.9 k of 17.7 k Blur sites with
  rotation 0 and a one- or two-lane mask.
- **Saturation-flag tracking (`vaddubs`, `vaddshs`, ...)**: `mfvscr` occurs 0 times, so the SAT bit is dead; nothing to
  optimise, and the simde forms are single `uqadd`/`sqadd` already except the three in VX-24.
- **CR6 computed when dead**: `cr6` is a local (`REX_CONFIG_CR_AS_LOCAL`) and GCC deletes an unread `setFromMask`; 68 + 60 record
  forms in Blur have no reader and cost nothing. Only the live reductions (VX-07 / VX-08) matter.
- **Scalar double arithmetic in the VMX mode** (a double denormal needs |x| < 2.2e-308, so `fadd`/`fmul`/`fmadd` are mode-free
  in practice): only 134 + 118 + 962 + 238 + 83 + 33 + 199 = 1,767 double-precision sites against ~26,000 single-precision ones,
  and they are not exact for double denormals. Not worth a decision.
- **`vmaxfp`/`vminfp` NaN fix-up elision for a constant bound**: only 18 Blur sites have a read-only constant operand; the clamp
  bounds of `sub_825087A0` are splats of loaded scalars, not provably non-NaN.
- **A bit-exact table for `vrefp` / `vrsqrtefp`**: the 360's estimate table is not in the tree; Xenia's x64 backend uses `rcpps`.
  If it appears, VX-10 / 11 / 12 are replaced by a `tbl` lookup (4096-entry style) and the whole section becomes exact.
- **`dp3` / `dp4` with a unit-vector operand as a lane extract**: not exact (`inf * 0`, `-0`).
- **`vsubfp(0, x)` for negation, `vmulfp(x, 1)`, `vaddfp(x, -0)`** and similar identities: not exact for signed zeros / NaN payloads.
- **Register-pressure scheduling** (sinking loads): peak simultaneously live vector values per hot Blur function is 11 - 38 (linear
  scan over the guest text); only 19 hot functions exceed 32 and they hold 133 samples. The 21 k stack transfers of SASRT's
  study are not a Blur problem.
- **Typed vector locals instead of union views**: no evidence that GCC fails to promote them (the hot `f_` bodies have no
  `ctx.v` accesses besides call/exit); a large refactor for an unmeasured effect.
- **`vspltw` of a `vspltisw` constant** (14 sites) and **`vmrghw` with a zero vector** (3,203 sites): single instructions.
- **Splat constants materialised per use** (1,003 reads of the FLT_MIN vector): a `movi` each; VX-09 removes most of them anyway.

## 5. The order I would do them in

1. **Read one hot copy's disassembly** (`tools/switch_hot_pcs.py` with the ELF of the current NRO, `sub_8250A7E8` and
   `sub_825087A0`): it decides VX-16, VX-22, VX-26, VX-36 and VX-08 in an hour, and tells which of the `fmla`/`fmls` forms GCC emits.
2. **The exact, mechanical ones that need no benchmark**: VX-20 (shift loops), VX-29 (`vupkd3d` of zero), VX-02 / VX-22 / VX-23 (one
   `vconst.py`), VX-15 / VX-14 (dot-product merges), VX-13 (cross), VX-24, VX-25, VX-27, VX-21.
3. **The mode work**: VX-01 and VX-03 (a small codegen change), then measure toggles with the PC census counters before VX-04 /
   VX-05 / VX-06.
4. **The recipe ones** VX-12 (exact against the fast form) and VX-09, then, with the user's word, VX-10 / VX-11 / VX-17.
5. **Liveness** VX-38 together with VS-35 / VS-36; **SPLAT-FROM-REG** VX-37 together with VS-25.

## 6. Files this document points at

`sdk/src/codegen/builders/vector.cpp` (all `build_v*`), `context.cpp` (`emit_set_flush_mode`, `ClassifyFloat`,
`next_float_demand`, `conversion_prefers_any_mode`), `memory.cpp` (`build_lvsl/lvsr`); `sdk/include/rex/ppc/intrinsics.h`,
`context.h` (`enableFlushMode`, `setFromMask`), `diet_ops.h`; `sdk/thirdparty/simde/simde/x86/sse.h` (`max_ps`), `avx2.h`
(`sllv_epi32`), `ssse3.h` (`alignr_epi8`); `app/generated/renut_pch.h` (`REX_VRSQRTEFP`, `rex_vrsqrtefp_fast_`, `REX_VNMSUBFP`,
`rex_dp_ps_`, `REX_VUPKD3D_COLOR`, `REX_SHUFFLE_TABLE`); `app/CMakeLists.txt:55-66, 195-206`; `tools/post_codegen.py`,
`vmx_float_macros.py`, `pch_neon_shuffles.py`, `vupkd3d_neon.py`, `vperm_one_source.py`, `flush_mode_flow.py`, `flush_hoist.py`,
`leaf_dead_stores.py` (line 106), `dead_ctx_defs.py`, `localize_native.py`; `app/generated/hot/n_8250A7E8.h`, `n_825087A0.h`
(copy shape); generated examples in `app/generated/renut_recomp.24.cpp` (`sub_825087A0`), `.119.cpp` (`sub_8251E6F0`),
and the functions named in each idea; `out/console/race_by_optimisation_copies.csv`; `app/src/cpu/banjo_forms_selfcheck.cpp`,
`banjo_hot_guest.cpp`; `sdk/tests/vec_lane_test.cpp`, `vmx_small_helpers_test.cpp`, `vmx_helpers_neon_test.cpp`,
`neon_shuffles_test.cpp`, `arm_forms_check.cpp`; SASRT: `docs/machine-copies-1110.md`, `machine-copies-deferred.md`,
`optimising-functions.md`, `app/generated/default/sasrt_recomp.*.cpp`.
