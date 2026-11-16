# 20 replacement ideas, exact or nearly exact (#176-#195, 2026-11-16)

The user (2026-11-16): "if you find out that some of your ideas have no gain replace them come up some more exact or
nearly exact ideas to replace them probably a new table of 20 I reckon so you have replacements ready now".

## Why replacements are needed

Reading the code for the 15 ms batch (docs/cpu-15ms-batch-1116.md) showed seven of the requested items have nothing,
or little, left to gain:

- **Already in the build:** #138 (the d-form address is base + zext(r) + d), #160 / #161 / #162 (SASRT's port 72) and
  #167 (SIMDE's NEON dot product is a multiply and pairwise adds).
- **Small:** #166 (SIMDE's paths for the operations the copies use are already native NEON) and #173 (on the Switch, a
  re-commit and a decommit are already page-table edits).

**Sources:**

- The nx race profile (docs/cpu-threads-audio-breakdown-1116.md).
- What the code does today.

**Checked against the earlier lists:** CG-01..40, VX-01..40, the near-exact #1-#69, #70-#175, and M-01..50 (a grep of
docs/ for each). Five candidates were dropped because a list already has them:

- the soft TLS calls;
- host mutex spinning (CG-32);
- the float constants' dirty bits;
- a cheaper denormal check (#72);
- the promotion census (CG-06).

**Verdicts:**

- *Exact:* the same results.
- *Nearly exact:* last-bit float differences in audio only (inaudible; not the simulation or the picture).
- *Timing:* the same results, different thread timing.

**ms:** CPU a frame, all threads (117 ms today). These are estimates, not measured. "Census first" means a PC count
decides whether to build it.

| # | Idea | Verdict | ms a frame | Difficulty |
|---|---|---|---|---|
| 176 | Machine copies for the functions the tool refuses today (atomics, barriers, the time base): the fast form only, the same statements; checked by the forms self-check and the guard on single-thread calls | Exact | 0.3-1 | Medium |
| 177 | Bigger closures for the top 40 copies (the callee size limit raised, so more callees inline) | Exact | 0.5-1.5 | Easy (watch the I-cache) |
| 178 | Tail calls in the copies: a call right before a return skips the reload after it and the write-back before the return | Exact | 0.2-0.6 | Easy-medium |
| 179 | Mixer rows written by the first child instead of zeroed and accumulated (the node loop's dcbz rows, 822E17C8's memset); x + 0.0f keeps -0 and NaN as before | Exact | 0.5-1.2 | Medium (inside N1, N3) |
| 180 | XMA decoded straight into the guest buffer; a cache hit copied once | Exact | 0.2-0.5 | Easy (count the copies first) |
| 181 | The console's XMA frame cache answering ~50 % of frames against the PC's ~95 %: find why it misses, then fix the key or the size | Exact | 0.5-1.0 | Census first |
| 182 | LZX output cache: a compressed block decoded again is answered by its input hash (DecodeBuffer, 1.2 ms on JQ1 / JQ2, the frame chain) | Exact | 0-1.0 | Census first |
| 183 | Vector byte reversals cancelled where a value is only moved (lvx then stvx, unchanged) | Exact | 0.2-0.8 | Medium |
| 184 | Uncontended guest critical sections inline: RtlEnterCriticalSection / RtlLeaveCriticalSection's fast path in the game code (one CAS, no import call) | Exact | 0.3-1.5 | Medium (census of the rate first) |
| 185 | The hottest kernel imports without the export wrapper (after #172: KeQueryPerformanceCounter, KeSetEvent with no waiter, the next ten by count) | Exact | 0.3-1 | Medium (census) |
| 186 | Natives for the top 7 frame-chain functions on JQ0-2 (824F35D8, 8251E6F0, 8252E168, 82504F90, 825087A0, 825123B8, 826EFA20; ~11 ms together) | Exact (guard) | 2-4 | Hard |
| 187 | The audio natives allowed last-bit float differences: fused multiply-adds and reordered sums in the mix | Nearly exact (inaudible) | 0.5-1.5 | Medium (inside N1-N7) |
| 188 | The reverb convolution's FFT as a NEON radix-4 FFT (inside N5) | Nearly exact (inaudible) | 0.3-0.6 | Medium |
| 189 | Guest memory chunks 2 MB-aligned so Horizon can map them with large blocks (fewer TLB misses over the ~600 MB window; the audio graph alone spends ~2.9 ms on first touches of node state) | Exact | 0-3 (unknown) | Medium (check what Horizon does first) |
| 190 | Fewer LTO partitions at the final link (-flto-partition=one, or 2): inlining across the SDK, the renderer and the game | Exact | 0-2 % | Easy (within the 6 GB memory rule) |
| 191 | The XMA decoder woken once per audio frame (the game's kicks batched), not once per register write | Timing | 0.1-0.4 | Easy-medium |
| 192 | The sound update thread's wake rate checked against the game's own period (a timer or sleep rounding could wake it more often than the 360 did) | Timing | 0-1.5 | Census first |
| 193 | The render thread's memcpy (1.6 ms on core 1) traced to its callers, then written straight into the GPU buffer | Exact | 0.3-1 | Census first |
| 194 | Scalar fres with a proven single input as one single-precision divide (1.0f / x is bit-identical to today's double divide: a division's double rounding is harmless); the exact half of #144 | Exact | 0.1-0.5 | Easy |
| 195 | The copies' reloads after a call skipped for non-volatile registers the callee provably restores (r14-r31, f14-f31: a whole-program proof like r1_preserve.py's for r1) | Exact | 0.5-1.5 | Medium |

**Sum (the ranges overlap, so not additive):** the 15 exact ones about 6-17 ms; the nearly exact and timing ones about
1-4 ms.

## Going into the batch

- **Built with the rest:** #176, #177, #178, #194, #195.
- **Inside the audio natives:** #179, #187, #188.
- **In the PC census round:** #180-#182, #184, #185, #192 and #193, each built if its count says so.
- **After the audio natives:** #186.
- **If time allows:** #183, #189, #190, #191.
