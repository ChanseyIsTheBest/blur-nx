# 30 universal codegen ideas (#226-#255): the generator, machine copies, the toolchain (2026-11-16)

The user (2026-11-16): "I'd really prefer universal codegen ideas", after the 30 of `cpu-ideas-196-225-1116.md`
(many of them Blur's own functions). Then: "It's OK if they aren't exact but very close and we can do census or
whatever". Every idea below works the same on any ReXGlue port: the generator's statements, the post-codegen passes, the
machine-copy tool, the build, or natives found by signature rather than by Blur's addresses.

**Checked against** every list on record: codegen-ideas-3 CG-01..40, #100-#225, the near-exact 65, moonshots
M-01..50, VX-01..40, VS-01..42, and the base's passes as they are (tools/loop_barriers.py, ro_constants.py,
float_loads.py, the generator's dform_disp_split, -fprofile-partial-training). Where an idea extends a listed one, the
line says what is new. **Evidence** comes from Blur's generated hot copies (`app/generated/hot/n_*.h`, 5,142 copied
functions), the disassembly of its six hottest copies, the 15 ms build's ELF, and today's console profiles.

**Kinds:** *exact* (the same results), *near-exact* (differs only in a stated case, which a census or the guard checks
first), *measure* (an A/B or a census decides). **ms**: Blur's CPU a frame at ~28 fps, as a guide for other ports.

## A. The generator and its passes (every generated function)

| # | Idea | Kind | Where | ms | Evidence and what is new |
|---|---|---|---|---|---|
| 226 | **Counted loops without barriers**: loops closed by `bdnz`, or by an induction register stepped by a constant and compared with a loop-invariant bound, get no `REX_LOOP_BARRIER`. They end whatever memory does, so a hoisted load cannot make them spin | exact (data-race-free rule, as #158) | loop_barriers.py, a third exemption | 0.2-1.0 | 14,161 barriers in the hot copies. The script exempts only loops with no guest reads or calls, and loops with an indirect call on every way back. A barrier forces every guest load in the loop to be reissued and stops GCC moving loads between iterations. Census: barriers by kind (bdnz / counted / other), then the guard and a console race with the hang watch |
| 227 | **Barriers on the exit test only**: the loops that still need one (a real polling loop) clobber only the word(s) the exit test reads (`"+m"` on that address), not the whole 4 GB window; the other loads in the loop may be hoisted | exact (DRF, as 226) | loop_barriers.py, the pch macro | 0.05-0.3 | marathon-nx narrowed "memory" to the guest window; this narrows the window to the polled word. Polled words found by the script (the loads feeding the back-edge test) |
| 228 | **Swap-folded tests**: scalar tests of a byte-swapped load done on the raw word: `andi.` / `rlwinm.` bit tests with the mask swapped, `cmpwi` / `cmplwi` eq/ne with the constant swapped, sign tests as a bit test of byte 0, a one-byte field as a byte load | exact | the generator (or a pass): a load whose register is read only by such a test | 0.1-0.5 | 1,478 `rev` in the six hottest copies; for 162 (11 %) the first reader is `tst` / `and #imm`, `cmp #imm` / `#0`, or `tbz` / `tbnz`. GCC folds `bswap(x) == C` only when nothing else uses the swapped value |
| 229 | **X-form index split**: `lwzx rD, rA, rB` (and the other X-forms) as base + zext(rA) + index, the index's shift folded into the access (`ldr w, [x, x, lsl #2]`), where rB is an index (made by `slwi` / `rlwinm` from a small value) | near-exact: differs only if rA + rB crosses 4 GB (a negative rB); the PC build counts such sites | the generator, beside dform_disp_split | 0.05-0.3 | the base's D-form split covers D-forms only; the six hottest copies still have ~1,600 `uxtw`-indexed accesses, the X-forms among them |
| 230 | **The read-only image inside the compiler**: the image's read-only bytes as a `const` array the LTO link can read; a load whose address GCC proves constant and inside .rdata becomes a constant (`__builtin_constant_p` in the load macro). This folds across labels, joins, loops, and values passed on through registers or stack slots. A folded code address turns the indirect call into a direct one | exact (the PC build keeps ro_constants' compare) | pch + the generator; the XEX import records excluded as ro_constants does | 0.1-0.6 | ro_constants.py follows constants along straight lines only ("a label forgets everything"): 12,616 loads folded in the hot copies, and at least 133 more through registers set once to a constant are not. It would also fold vtable slots once an object's vptr is known (stored by an inlined constructor). The static form of CG-06: no census needed when the target is a constant |
| 231 | **Double loads in the vector unit**: float_loads.py's method for `lfd`: `ldr d` + `rev64 v.8b` instead of `ldr x`, `rev x`, `fmov d, x` (the 5-cycle move between register files) | exact | float_loads.py | 0.1-0.4 | the pass does singles only. The hot copies have 11,432 `lfd` and 4,173 `stfd` through integers, many being f14-f31 restores in epilogues |
| 232 | **Non-volatile registers trusted at real calls**: after a real call, r14-r31, f14-f31, v14-v31 and v64-v127 stay in their locals (no reload from ctx) | near-exact: differs only if a guest function breaks the PowerPC ABI (hand-written assembly); a census build compares them after every real call and lists any callee that differs | localize_native, loop_locals / leaf_locals | 0.05-0.3 | 8250A7E8 reloads 75 registers after its real call to 82506E88 and 52 after 8251F778. #195 needs a proof for each callee, near-exact #9 covers r1 only, and #6 the volatile side |
| 233 | **Guest r1 and r13 in fixed host registers**: the stack pointer and the thread block's pointer as global register variables (`-ffixed-x27` / `-ffixed-x28` style) across the generated code, not loaded from and stored to ctx in every function | exact (the entries from host code set them) | pch + build; the host callbacks into guest code load them | 0.1-0.4 | extends CG-36 (ctx and base out of the callee-save traffic) by two guest registers that almost every function uses. AAPCS64 keeps x19-x28 across prebuilt libraries, so only the entry points need care |

## B. Machine copies (tools/localize_native.py)

| # | Idea | Kind | ms | Evidence and what is new |
|---|---|---|---|---|
| 234 | **Frames as host locals**: a copy's guest frame becomes a host array when none of its addresses leaves the closure. That means r1-derived addresses are only loaded, stored and handed to members, never stored to memory or passed to a real call, and the back chain is never read. The frame is then in host byte order, has no card checks, and GCC's SRA keeps its slots in registers | exact | 0.2-1.0 | r1-based accesses are 16,527 loads and 19,317 stores in the hot copies (11 % of their guest accesses). The exact, scoped form of #115 (the whole stack little-endian) and #157 (leaf red zones); CG-20 forwards only within one block. Escape census per root: 8250A7E8's frame escapes (r1+84 goes to a real call), so the census decides the reach |
| 235 | **Members by reference**: an inlined member takes the root's register locals by reference (one struct of `l_` registers that never escapes) instead of exchanging them through ctx. ctx is then written only at real calls and at the root's return | exact | 0.3-1.0 | 8250A7E8 (52 member calls, 2 real) still has at least 518 stores and 272 loads on ctx after GCC. CG-10 trims what real calls store, #206 is for members GCC does not inline, and CG-22 drops same-value stores |
| 236 | **Indirect calls inside members**: a callee that makes an indirect call joins the closure. Only the indirect call itself stays a real call (ctx synced around it, and the guard stops there as at any real call) | exact | 0.1-0.5 | today the whole callee is a real call ("it makes an indirect call": 8250A7E8 → 82506E88, with 75 registers reloaded after it) |
| 237 | **Entry tests inlined**: the early-exit test at a function's entry (a flag, a null pointer, a count: `lwz; cmpwi; beqlr`) goes to the call site, and the ctx sync and the call happen only when the test passes. Applies to callees outside a closure and to big roots called from generated code | exact | 0.1-0.5 | 8250A7E8 tests a global flag (`lbz 7029(r11)`) after a prologue that has already stored the back chain, f1 and r4; its copy is 113,884 bytes and never inlines. The PGO arcs give the exit share. New against #177 (bigger closures) and CG-38 (whole tiny callees) |
| 238 | **Callers by call count**: a caller that calls a copy many times a frame (a loop over objects) is copied too, with the callee as its member, so the per-call hand-over and the stub disappear | exact | 0.2-0.8 | chosen from the .gcda call counts: a cheap loop over an expensive callee has few samples of its own, so the sample-based bulk list misses it. CG-12 goes downward (members by call count); this goes upward |
| 239 | **Cold members out of line, the closure in bytes**: members the training entered rarely per root call emitted `noinline, cold` (with #206's register ABI), and the closure's limit counted in compiled bytes (from the last build's symbol sizes) rather than guest lines | exact | 0.2-1.0 | the biggest copies: 8250A7E8 113,884 bytes, 829D0300 87,220, 821A0220 86,456, 826EFA20 66,904, against the A57's 48 KB instruction cache. #209 handles cold blocks, #207 shared members, M-27 hot loops; this is the member-level split |
| 240 | **Cold-path registers stay in ctx**: a guest register that only cold blocks use (by the PGO / Counting-form counts) is read and written in ctx there, with no local, so the hot path does not carry it from entry to the write-back at exit | exact | 0.1-0.5 | 8250A7E8 localizes 75 registers and has a 1,856-byte host frame with 1,366 q-register spill stores. A local that is live from entry to exit costs a spill even on paths that never touch it |
| 241 | **Hand-native gates settled at boot** (in-place ship builds): a native with no key, or one whose key is on, is called straight, without the per-call state load and the cold `Init` / fallback paths | exact | 0-0.05 | 8250A7E8's asm: 11 `banjo::hand::Init` and 6 `BlurOwn` call sites, plus 11 cold fallback calls. Smaller copies more than saved cycles |
| 242 | **The bulk list from the PGO counts**: `switch_hot_bulk.py` reads the merged .gcda (function entry and block counts) besides the stack profiles, so functions that are hot in scenes the sampler never saw (other tracks, menus) get copies too | exact | 0-0.5 | the optimising guide recommends it ("a list from them covers scenes the stack sampler never saw"), but the tool reads profiles only; tools/pgo_summary.py already parses the .gcda records |

## C. The toolchain (devkitA64 GCC 16.1; every option below checked to exist in it)

| # | Idea | Kind | ms | Evidence and what is new |
|---|---|---|---|---|
| 243 | **Register-pressure options for the biggest copies**: `-flive-range-shrinkage`, `-fira-loop-pressure`, `-fira-region=all`, per source (set_source_files_properties on the top 10 by host frame size) | measure | 0-0.8 | the 1,366 spill stores above. CG-27 is the scheduler's pressure option and M-45 the early RA (`-mearly-ra=none` today); these are the register allocator's own |
| 244 | **`-fipa-pta` at the LTO link** of the generated code: interprocedural points-to analysis can separate ctx, the host stack and guest memory across calls (restrict only covers a function's own parameters) | measure (link time grows) | 0-0.5 | not tried on any port |
| 245 | **Code-growth passes held back on the flat shards**: `-fno-tracer`, `-fno-peel-loops` and a lower unroll limit (all turned on by `-fprofile-use`), and `-fvect-cost-model=very-cheap` (no alias-versioned vector loops over guest memory) | measure | 0-0.5 | Game Master is 22 ms over 413 functions with none above 2 %: instruction-cache bound, and tail duplication and unrolling grow exactly that code |
| 246 | **The hot thresholds with a one-race profile**: `--param hot-bb-count-fraction` / `hot-bb-count-ws-permille` set so the code a second track or a menu runs is not treated as cold (pgo_summary.py approximates sum_max over the sessions) | measure | 0-0.4 | `-fprofile-partial-training` (on) covers never-run code; this covers rarely-run code that GCC optimises for size |
| 247 | **Training on several scenes**: the instrumented build trained on two tracks, the menus and a split-screen race, the counts added before pgo_merge | exact | 0 on the measured race, 0.2-1 elsewhere | one training race today. Correction to yesterday's list: #210 (`-fprofile-partial-training`) is already in the shards' flags, so this is what is left of it |
| 248 | **`-fipa-reorder-for-locality`** (GCC 15+'s locality partitioning and cloning under LTO, with its `lto-partition-locality-*` params) | measure | 0-1 | the 25 hottest frame-chain copies (840 KB) lie across 8.3 MB of the 61 MB text, two of them 51-53 MB in; the A57's L2 TLB covers 4 MB in 4 KB pages. The compiler-native alternative to #211's link order file and #113's BOLT |
| 249 | **The host C library's hot routines** (memcpy, memset, memcmp) taken from Arm's optimized-routines (the A57-tuned AdvSIMD forms) instead of newlib's | measure | 0-0.5 | host memcpy 3.26 ms a frame (render thread 48 %, XMA 18 %), memset 1.49, memcmp 0.19; every port links the same newlib |

## D. Natives found by signature (one table in the base for every port)

The base already matches Renut's D3D hooks by signature (`tools/renut_sigmatch.py`: instructions with the registers
removed). The same matching can place natives: one written for one game then serves every game that linked the same
library. Blur's XEX links **XDK 2.0.11164** for every static library (XAPILIB, D3D9, D3DX9, LIBCPMT, XAPOBA,
XAUDIO2, XMCORE, ...), the same XDK as **Guwange, A World of Keflings and PAC-MAN CE DX** (blur-notes.md).

| # | Idea | Kind | ms | Evidence and what is new |
|---|---|---|---|---|
| 250 | **XAudio2 natives by signature**, *check first*: match Blur's audio natives' functions (the node loop, voice step, effect stage, resampler, filters) against the XAUDIO2 / XAPOBA code of another game on the same XDK. If they are the library's, they become universal for that XDK version (a version check on the XEX's library list) | exact, check first | Blur: 0 (it has them); another XAudio2 game on 2.0.11164: up to Blur's own audio-native gains | the audio natives are Blur's largest CPU wins; N&B had XAudio natives at its own addresses too (katamari-notes-reference.md) |
| 251 | **The XDK C runtime and XAPI by signature**: memcpy, memset, memmove, memcmp, strlen, strcmp, _stricmp, strncpy, wcslen, XMCORE's XMemCpy / XMemSet, and the hot XAPILIB wrappers (TlsGetValue, QueryPerformanceCounter, EnterCriticalSection) | exact | 0.5-2 a port (Blur: the large memcpy 82953D90, #224) | Blur's memset / memcpy natives were written at its own addresses; the XDK links the same routines into every game |
| 252 | **Exact math and D3DX by signature**: the CRT's exactly-defined functions (floor, ceil, fmod, modf, frexp, ldexp, sqrt) as host calls, and D3DX9's matrix and vector routines (D3DXMatrixMultiply, D3DXVec3TransformCoord, ...) in the library's own operation order | exact (the PC build compares) | 0-0.5 | unlike #142 (approximate sinf etc.), these have one right answer |
| 253 | **Middleware natives shared across ports**: one table for code many 360 games linked (Havok, Bink, Scaleform, Unreal Engine 3's core): a native written in one port serves the rest | exact | Blur: small (its own engine); UE3 / Havok ports: large | a base-level library of signatures, as the D3D one |

## E. Measurement that steers every port

| # | Idea | Kind | Evidence and what is new |
|---|---|---|---|
| 254 | **A codegen-gap census from the ELF**: for every copy, host instructions per guest instruction, host frame size, q-register spills, ctx loads and stores, swaps, card checks, flush-mode switches. A ranked list of which generator overhead to attack, for any port | measure | today's numbers above were gathered by hand from six copies (scratchpad hot_asm.py, rev_use.py) |
| 255 | **Samples by instruction class on the console**: the sampler's pcs mapped onto the disassembly and grouped (guest loads, swaps, card checks, ctx traffic, spills, mode switches, calls) per thread | measure | hot_asm.py's manual pass found 39 % of the hot copies' samples on loads; a standing tool makes it per build and per port |

## Listed ideas that today's evidence moves

- **VS-17** (one card run across inlined members): 2,266 card flushes before inlined members in the hot copies, still
  there.
- **VS-13** (no card mark for stack-derived bases): the `__savevmx` members still card every vector save
  (`(r11 + r12) & ~0xF`, r12 = r1 - 192). Yesterday's #215 is VS-13 again.
- **#211 / #113** (layout): the hot copies are spread over 8 MB (see 248).
- **CG-21** (vector locals as registers): 8250A7E8's 1,366 q-register stores to its host frame. Some are register
  pressure and some address-taken locals; 254's census tells them apart.
- **#138** (no 32-bit wrap on base + displacement): already done for D-forms by the generator's dform_disp_split;
  229 is the X-form part left.

## Yesterday's universal ones (#196-#225)

These apply to any port and stand as written: #206 (member register ABI), #207 (shared members), #208 (CG-25 on the
generated shards only), #209 (C-level cold outlining), #211 (link order), #212 (optimisation level per shard class),
#213 (adjacent fields as one access), #217 (direct calls into copies), #220 (per-thread malloc pool), #221 (lock
census), #222 (ctx hot fields). #210 is already on; #215 is VS-13.

## Suggested order

1. The censuses that decide the rest: 254 (codegen gaps), 226's barrier kinds, 234's frame escapes, 232's ABI check,
   250's XAudio2 match.
2. Broad exact ones: 226 + 227 (barriers), 235 (members by reference), 230 (read-only image), 234 (frames), 228
   (swap folds), 231 (double loads).
3. Copy structure: 236, 237, 238, 239, 240.
4. Toolchain A/Bs, one build each, batched into few builds: 243 + 245 + 246 together, then 248, then 244.
5. Signature natives: 251 first (memcpy / memset are in every port's top functions), then 250, 252.
