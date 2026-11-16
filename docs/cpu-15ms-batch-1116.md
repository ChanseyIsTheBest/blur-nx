# The 15 ms CPU batch (2026-11-16)

The user (2026-11-16): "at least 15ms cut off CPU time before I test on switch again I don't want any impact on
gameplay/simulation or visuals". Asked for:

- seven natives for the top seven audio functions;
- cold-block splitting, -fno-trapping-math, -fno-math-errno;
- the 360-precision list (#163-#165, #144, #160, #161, #166, #167, #171-#173);
- "all the top 16 exact ones" (docs/cpu-ideas-130-175-combined-1114.md).

The rules: work independently until all of it is done, in the main chat with no agents, and test the whole batch on
the PC before the console.

**Baseline:** the nx race, 117 ms of CPU a frame (docs/cpu-threads-audio-breakdown-1116.md).

## Status

**Status words:**

- *already in* = the build already does it (found while reading the code); nothing to add.
- *small* = checked, and less to gain than the table thought.
- *todo / doing / done (unverified) / verified (PC)*.

| Item | What | Status | Notes |
|---|---|---|---|
| CG-25 | -freorder-blocks-and-partition | todo | GCC enables it by default only on x86; AArch64 needs the flag |
| CG-26 | -fno-trapping-math | todo | the old reason against it (float ops moved across a flush-mode switch) is gone under BLUR_FPCR_ONE_MODE; 18 guest FPSCR writes remain (rounding mode): their files keep -ftrapping-math |
| errno | -fno-math-errno for the SDK and the renderer | todo | the generated code and the copies have it already |
| #163 | lwarx / stwcx. as relaxed exclusives | todo | the CAS is seq_cst today (ldaxr / stlxr); Blur has no isync at all; 662 CAS sites |
| #164 | eieio as dmb ishst | todo | sync and eieio: 29 seq_cst fences |
| #165 | lwsync as dmb ishld + dmb ishst | todo | 162 acq_rel fences; measure the two forms (a runtime byte picks) |
| #144 | fres / frsqrte as estimates | todo | today a double divide (and a double sqrt) |
| #160 #161 #162 | vrefp / vrsqrtefp / vlogefp | already in | SASRT port 72 (SASRT_VMX_FAST_ESTIMATES, ON): frecpe / frsqrte with two Newton steps, the NEON log2; the recheck doc missed it |
| #166 | SIMDE's x86 rules | small | the ops the copies use (loads / stores, mul / add / sub, shuffles, unpacks, blend, and / or, compares) have native NEON paths in SIMDE; only conversions and rounding (a few hundred sites) have fix-ups |
| #167 | dot products | already in | SIMDE's NEON dpps is vmulq + vaddvq (pairwise adds); 14,889 sites in the tree, 19,200 in the copies (not 848) |
| #171 | APC check without the spinlock | todo | DeliverAPCs takes the guest spinlock before looking at the queue |
| #172 | KeTlsGetValue inline | todo | an import call, a thread lookup and a bounds check |
| #173 | commit / decommit | small | on the Switch a re-commit and a decommit are page-table edits already (RexGmCommit finds the chunk mapped; SyncHostPageAccess returns at once for 4 KB+ pages; Decommit does no host work) |
| #131 | XER carry dead at calls and returns | todo | |
| #134 | stack / globals / heap as separate arrays | todo | |
| #137 | loops lifted and vectorized | todo | |
| #138 | no 32-bit wrap on base + displacement | already in | REX_ADDR_D = base + zext(r) + d on the Switch (dform_disp_split) |
| #139 | 32-bit registers | todo | |
| #143 | if-conversion including loads | todo | |
| #145 | back-chain stores dropped | todo | BANJO_GEN_BACK_CHAIN_ELIDE exists (OFF, "not exact for a guest stack walk"): needs the 0(r1) scan |
| #146 | only ABI argument registers at calls | todo | the copies already sync only what a known callee reaches; unknown callees get every register |
| #148 | non-wrapping induction variables | todo | |
| #149 | profile-pruned copies with a bail-out | todo | |
| #150 | card marks once per page per function | todo | |
| #153 | mfcr / mtcrf narrowed | todo | |
| #156 | indirect calls without range / null checks | todo | |
| #157 | leaf red-zone slots as locals | todo | |
| #158 | getter call merging | todo | |
| #159 | non-escaped stack slots survive calls | todo | |
| N1-N7 | natives: 8230AD50, 822BEDD0, 822E17C8, 8230EDE0, 822E98B0, 822D2378, 8230B778 | todo | |

## Replacements (#176-#195)

The user (2026-11-16): "if you find out that some of your ideas have no gain replace them". Twenty replacements are in
docs/cpu-ideas-176-195-replacements-1116.md.

| Item | Status | Notes |
|---|---|---|
| #176 copies of refused functions | todo | |
| #177 bigger closures | todo | |
| #178 tail calls in copies | todo | |
| #179 first-write mixer rows | todo | inside N1, N3 |
| #180 XMA straight into the guest buffer | census | |
| #181 console XMA cache hit rate | census | |
| #182 LZX output cache | census | |
| #184 critical sections inline | census | |
| #185 hot imports without the wrapper | census | |
| #186 natives for the top 7 frame-chain functions | todo | after N1-N7 |
| #187 audio natives with FMA / reordered sums | todo | inside N1-N7 |
| #188 NEON radix-4 FFT for the reverb | todo | inside N5 |
| #192 sound update wake rate | census | |
| #193 render thread memcpy | census | |
| #194 fres as a single divide (exact) | todo | |
| #195 no reload of restored non-volatiles | todo | |
| #183, #189, #190, #191 | later | if time allows |

