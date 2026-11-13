# Ideas #130-#175 combined, with ms savings (2026-11-14)

The user (05:25): "Can you send me this combined table on mobile please with ms savings? Double check to see if they
are actually exact and you are being too conservative and give me that new table + 20 more where we being too
precise vs 360". Sources: docs/cpu-codegen-inexact-30-1114.md (#130-#159), docs/cpu-codegen-recheck-360-precision-1114.md
(the recheck, #160-#173), and two GPU items found now (#174, #175).

**ms:** milliseconds of CPU a frame, all threads together (Blur: ~117 ms of CPU a frame, the machine copies 84 ms of it,
so 1 % of the copies is ~0.8 ms). On the frame chain (Game Master + JQ0-2) roughly half of that turns into frame time.
GPU items are GPU ms a frame (GPU ~27.7 ms now). All unmeasured estimates.

**Verdicts:** *Exact by proof* = provable for Blur's code (the PowerPC calling convention, or a scan of Blur's
functions), so the output is the same. *Timing* = same results, different thread timing. *Closer to the 360* = differs
from today's build but nearer to what the real 360 computes. *Not exact* = can change results (rarely).

## The 46 in one table, by verdict, biggest first

| # | Idea | Verdict | ms a frame | Difficulty |
|---|---|---|---|---|
| 134 | Stack, globals, heap as separate restrict arrays (compiler keeps stack values in registers) | Exact by proof (escape analysis) | 1.7-4.2 | Hard |
| 138 | No 32-bit wrap on base + displacement (one host pointer per base register, ldp / stp) | Exact by proof (d-form) | 1.7-4.2 (if not done already) | Medium |
| 149 | Profile-pruned copies with a bail-out to the full translation | Exact by construction | 1.7-4.2 | Very hard |
| 137 | Guest loops lifted to for-loops, auto-vectorized (with an overlap check) | Exact (element-wise loops) | 1-4 | Very hard |
| 150 | Card marks once per page per function (flushed at every barrier, atomic, call) | Exact (race-free code) | 1-3 | Medium |
| 159 | Non-escaped stack slots survive calls (no reload) | Exact by proof (escape analysis) | 0.8-2.5 | Hard |
| 146 | Only ABI argument registers written at calls | Exact by proof (ABI + millicode scan) | 0.8-1.7 | Medium |
| 148 | Loop counters as non-wrapping induction variables | Exact (counted loops) | 0.4-1.7 | Medium |
| 143 | If-conversion including loads (bases already dereferenced) | Exact (results) | 0.4-1.3 | Hard |
| 131 | XER carry dead across calls and returns | Exact by proof (ABI + scan) | 0.3-1 | Easy |
| 145 | Back-chain stores dropped (functions reading 0(r1) excepted) | Exact by proof (scan) | 0.3-1 | Easy-medium |
| 153 | mfcr / mtcrf narrowed to cr2-cr4 (today 32 conditional ORs) | Exact by proof (ABI + scan) | 0.3-1 | Easy |
| 156 | Indirect calls without range / null checks | Exact (runs that do not crash) | 0.3-1 | Easy |
| 158 | Repeated store-free getter calls merged (no loop, barrier or atomic between) | Exact (data-race-free rule) | 0.3-1 | Medium |
| 157 | Leaf functions' red-zone slots as host locals | Exact by proof (ABI + scan) | 0.4-0.8 | Easy-medium |
| 139 | 32-bit registers where no 64-bit op runs | Exact by proof (register dataflow) | 0.2-0.8 | Medium |
| 147 | Guest spin-wait loops given a host back-off | Timing only | 0-2 | Medium |
| 163 | lwarx / stwcx. as relaxed exclusives, isync as an acquire fence (today seq_cst) | Exact vs the 360 | 0.5-2 | Medium |
| 173 | Virtual memory commit / decommit without host remapping (~1,300 a second each) | Exact (no reads of freed memory) | 0.5-3 (census first) | Medium |
| 168 | Callee-saved register save areas in host byte order | Exact by proof (ABI + scan) | 0.4-1.3 | Medium |
| 165 | lwsync as `dmb ishld` + `dmb ishst` (today a full `dmb ish`) | Exact vs the 360 | 0-1 (measure) | Easy |
| 166 | SIMDE's x86 compatibility fix-ups off where native ARM = VMX | Exact vs the 360 (op by op) | 0.2-0.8 | Medium |
| 171 | APC check behind an atomic pending flag (today a spinlock per alertable wait) | Exact | 0.1-0.5 | Easy |
| 172 | KeTlsGetValue (~100 k a second) and the perf frequency inlined | Exact | 0.1-0.4 | Easy |
| 164 | eieio as `dmb ishst` (today a full fence) | Exact vs the 360 | 0-0.2 | Easy |
| 144 | fres / frsqrte as estimate + one Newton step (today double divide / sqrt) | Closer to the 360 | 0.3-1.5 | Easy |
| 154 | lwarx / stwcx. as true LDXR / STXR (address-based like the 360) | Closer to the 360 | 0.3-1 | Medium |
| 160 | vrefp as frecpe + one Newton step (today an exact vector divide) | Closer to the 360 | 0.3-1 | Easy |
| 161 | vrsqrtefp as frsqrte + one Newton step (today sqrt + divide) | Closer to the 360 | 0.3-1 | Easy |
| 167 | Dot products without x86's dpps order (848 sites in 10 files) | As close to the 360 as today | 0.2-0.6 | Easy |
| 162 | vlogefp as a NEON polynomial (today 4 x log2f) | Closer to the 360 | 0-0.3 | Easy |
| 170 | XMA decode at lower internal precision (16-bit output) | As close to the 360 as today | 0-0.3 | Hard |
| 169 | vpkd3d128 / vupkd3d128 with native conversions (GPU-only data) | Not bit-exact (+-1 ulp, GPU data) | 0-0.5 (count first) | Medium |
| 132 | Single ops in single precision without CG-14's proof | Not exact | 0.8-2.5 | Medium |
| 140 | Typed guest memory (float / integer aliasing) | Not exact (stfs + lwz punning) | 0.8-2.5 | Hard |
| 130 | lwsync as release / acquire forms | Not exact (orders only one access) | 0.5-2 | Medium |
| 142 | Fast approximate CRT float math | Not exact | 0.5-2 | Medium |
| 133 | Double chains ending in frsp computed in single | Not exact (chains of 2+) | 0.4-1.7 | Medium |
| 135 | Fast-math subset on float-heavy shards | Not exact | 0.5-1.5 | Easy (flags) |
| 141 | Read-mostly globals folded as constants | Not exact | 0.5-1.5 | Medium-hard |
| 136 | -freciprocal-math | Not exact | 0.3-1 | Easy |
| 155 | vtable loads hoisted across calls | Not exact | 0.3-1 | Medium |
| 152 | Dot products as fused chains | Not exact (vs today) | 0.2-0.6 | Easy |
| 151 | lvx / stvx alignment masks dropped | Not exact (lvsl idiom) | 0.1-0.4 | Easy |
| 174 | GPU: 7e3 render targets (2_10_10_10_FLOAT) at 64 bits a pixel (RGBA16F); the 360 stores 32 | Visual (only if the 2-bit alpha is unused) | GPU 1-3 | Medium |
| 175 | GPU: depth (the 360's 24-bit float D24FS8) as 32-bit float + stencil, 64 bits a pixel | Visual (depth precision; check stencil use) | GPU 0.5-2 | Medium-hard |

**Sums (the upper ends overlap, so not additive):** the 16 exact-by-proof ones, about 11-33 ms of CPU a frame; the 8 exact-vs-the-360 ones, about 2-9 ms; the 7 closer-to-the-360 (or as-close) ones, about 1.4-5.7 ms.

## The "more precise than the 360" list: 16, not 20

#160-#173 and #174-#175 above. Checked and dropped (already done, or too small to matter):

- Rumble: already sent only when it changes.
- Audio output: already two frames a wake-up (M-43).
- Sleeps: no spinning for accuracy.
- dcbf / dcbst: already no-ops.
- vexptefp: already at the 360's precision.
- vmaxfp / vminfp: the exact helper is needed (#42: 4.1 % of lanes differ).
- The CAS's trailing barrier: already off in Blur.
- Input polling and priority changes: under ~260 calls a second.
- MmQueryAddressProtect (~2 k a second): already cheap.
- The yield after a timed-out poll: ~340 a second (about 0.02 ms a frame).
- The per-wait core-layout hook: one cached check.
- Vertex-stream re-hashing: already skipped when the store cards prove a range unchanged.

## Re-verified now

The 11 "not exact" ones were checked again for a proof that would make them exact; none has one. Two near misses:

- **#151:** exact only where an address is proven 16-aligned.
- **#140:** exact only where no punning reaches across functions.

A check worth one census: whether Blur runs the FPU in non-IEEE mode (FPSCR[NI]). If it does, CG-13's scalar flush-to-zero is exactly what the 360 itself does, not an approximation.
