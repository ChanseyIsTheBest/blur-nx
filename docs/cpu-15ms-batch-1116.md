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
| CG-25 | -freorder-blocks-and-partition | done (unverified) | GCC enables it by default only on x86; AArch64 needs the flag |
| CG-26 | -fno-trapping-math | done (unverified) | the old reason against it (float ops moved across a flush-mode switch) is gone under BLUR_FPCR_ONE_MODE; 18 guest FPSCR writes remain (rounding mode): their files keep -ftrapping-math |
| errno | -fno-math-errno for the SDK and the renderer | done (unverified) | the generated code and the copies have it already |
| #163 | lwarx / stwcx. as relaxed exclusives | done (unverified) | the CAS is seq_cst today (ldaxr / stlxr); Blur has no isync at all; 662 CAS sites |
| #164 | eieio as dmb ishst | done (unverified) | sync and eieio: 29 seq_cst fences |
| #165 | lwsync as dmb ishld + dmb ishst | done (unverified) | 162 acq_rel fences; measure the two forms (a runtime byte picks) |
| #144 | fres / frsqrte as estimates | no gain, kept exact | ~100 sites in the whole game; on the A57 an estimate plus two Newton steps takes about as long as the divide it replaces, and it would change physics bits |
| #160 #161 #162 | vrefp / vrsqrtefp / vlogefp | already in | SASRT port 72 (SASRT_VMX_FAST_ESTIMATES, ON): frecpe / frsqrte with two Newton steps, the NEON log2; the recheck doc missed it |
| #166 | SIMDE's x86 rules | small | the ops the copies use (loads / stores, mul / add / sub, shuffles, unpacks, blend, and / or, compares) have native NEON paths in SIMDE; only conversions and rounding (a few hundred sites) have fix-ups |
| #167 | dot products | already in | SIMDE's NEON dpps is vmulq + vaddvq (pairwise adds); 14,889 sites in the tree, 19,200 in the copies (not 848) |
| #171 | APC check without the spinlock | done (unverified) | DeliverAPCs takes the guest spinlock before looking at the queue |
| #172 | KeTlsGetValue inline | small | the PC census: ~10 k calls a second (not 100 k); the Switch's export wrapper has no census and LTO can inline it |
| #173 | commit / decommit | small | on the Switch a re-commit and a decommit are page-table edits already (RexGmCommit finds the chunk mapped; SyncHostPageAccess returns at once for 4 KB+ pages; Decommit does no host work) |
| #131 | XER carry dead at calls and returns | already in | XER is a local of every function (REX_CONFIG_XER_AS_LOCAL); only one copy syncs it |
| #134 | stack / globals / heap as separate arrays | todo | |
| #137 | loops lifted and vectorized | todo | |
| #138 | no 32-bit wrap on base + displacement | already in | REX_ADDR_D = base + zext(r) + d on the Switch (dform_disp_split) |
| #139 | 32-bit registers | todo | |
| #143 | if-conversion including loads | todo | |
| #145 | back-chain stores dropped | done (unverified) | BANJO_GEN_BACK_CHAIN_ELIDE exists (OFF, "not exact for a guest stack walk"): needs the 0(r1) scan |
| #146 | only ABI argument registers at calls | already in | the generator keeps non-volatile and non-argument registers as C++ locals (REX_CONFIG_NON_VOLATILE_AS_LOCAL, _NON_ARGUMENT_AS_LOCAL): calls hand over only ABI registers; one copy reloads r22-r31 after a kernel call |
| #148 | non-wrapping induction variables | todo | |
| #149 | profile-pruned copies with a bail-out | todo | |
| #150 | card marks once per page per function | small | the card runs already cost one compare per store (banjo_hot_prelude.h); card_groups.py merges groups |
| #153 | mfcr / mtcrf narrowed | no gain | one mfcr in the whole game, no mtcrf |
| #156 | indirect calls without range / null checks | census | the null check is only safe if no run reaches an empty slot (ResolveIndirectFunction): count it first |
| #157 | leaf red-zone slots as locals | todo | |
| #158 | getter call merging | todo | |
| #159 | non-escaped stack slots survive calls | todo | |
| N1 | the node loop 8230AD50 | done (unverified) | inexact flag (declines under the guard), calls= its child processors and the effect stage; node fields read once, each child's input rows fused per output row, no reload of a freshly zeroed row, one card per row |
| N2 | the effect stage 822BEDD0 (its dispatcher) and its compressor 822DA210 | done (unverified) | the dispatcher over the 14 effects with callouts; the compressor's ramp vectorized, its level of up to four rows in lanes (the same results) |
| N3 | the voice step 822E17C8 | done (unverified) | an exact transcription with callouts (the source's virtual calls through the generated dispatch); its time is a vtable cache miss, which no native removes |
| N4 | the resampler 8230EDE0: its two kernels 822E29A8 and 822D7DF0 | done (unverified), exact | the copy's 221 nx-race samples: ~60 in the 822D9040 / 822D79A8 gathers (natives already), ~51 in the recursive filter 822E29A8, ~16 in the stereo converter 822D7DF0, ~90 spread over the body's bookkeeping and calls (a transcription of its 737 instructions would compile to what its copy is). So the natives are the two kernels, exact and checked by the guard (blur_hand_natives_resampler.h): the filter's 16 dot products a block transposed (the same operands and order, the same bits), the converter one pass; one range check and one card a range |
| N5 | the convolution reverb 822E98B0 | done (unverified) | its steady-state call with callouts (the first call's set-up declined to the copy) |
| N6 | the sound update 822D2378 | done (unverified) | an exact transcription with callouts (the job vector kept in the guest frame its callees read) |
| N7 | the output node 8230B778 | done (unverified) | callouts for the child, all rows copied at once |

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
| #181 console XMA cache hit rate | already in | the nx race: 91-97 % answered after the first minute (cache v2; the 50 % was cycle 1) |
| #182 LZX output cache | census | |
| #184 critical sections inline | small | RtlEnterCriticalSection is not among the top 16 imports in the PC census |
| #185 hot imports without the wrapper | small | the Switch wrapper is thin; the top imports are yields and events |
| #186 natives for the top 7 frame-chain functions | todo | after N1-N7 |
| #187 audio natives with FMA / reordered sums | todo | inside N1-N7 |
| #188 NEON radix-4 FFT for the reverb | todo | inside N5 |
| #192 sound update wake rate | census | |
| #193 render thread memcpy | census | |
| #194 fres as a single divide (exact) | todo | |
| #195 no reload of restored non-volatiles | todo | |
| #183, #189, #190, #191 | later | if time allows |

## Findings along the way (2026-11-16)

- **The PC census** (census_nx1, cumulative over 470 s): NtYieldExecution ~176 k a second on the PC (~6 k on the
  console), NtSetEvent ~22 k, NtWaitForSingleObjectEx ~12 k, KeQueryPerformanceFrequency ~12 k, KeTlsGetValue ~10 k.
  RtlEnterCriticalSection is not among the top 16, so #184 (critical sections inline) and #185 (the wrapper) are
  small. #185 is folded into nothing.
- **#145:** frame_traffic.py already kept the chain store in functions that read their own slot 0. It now also marks
  none if any function reads another frame through a chain value; Blur has none (the 98 frame pops and the alloca's
  stwux read only their own). BLUR_BACK_CHAIN_ELIDE ON.
- **#171:** DeliverAPCs looks at the queue's forward link before taking the guest spinlock.
- **#163-#165:** tools/guest_fences.py (191 fences: 162 lwsync, 10 sync, 19 eieio); guest_cas.py's macro is relaxed on
  AArch64. Two runtime bytes give the old forms back without a rebuild: keys blur_guest_cas_relaxed and
  blur_guest_fences_split.
- **CG-26:** tools/fp_env_files.py lists the 18 generated files with a guest FPSCR write; they keep -ftrapping-math.

## The PC guard run (2026-11-16 18:33, guard_15ms, 600 s, a race with RT held)

1,927 listed and 1,903 of 3,093 bulk copies reached; 145 M calls compared; 7 differences in 5 functions, every one a
real difference and every one fixed (cd479041, afb88d14). The generated code's own checks found none (read-only
constants, promoted indirect calls, the r1 mirror, the flush-mode flow).

| Function | What differed | Cause | Fix |
|---|---|---|---|
| 822D7DF0 (N4's stereo converter, new) | r10 with n = 0 | the native left out the `subf / rlwinm / add` that sets r10 on every path before the vectors | the statement added |
| 8230A4D8 (and 8230AD50, which holds it) | the word at r1 - 144 | #145 (BLUR_BACK_CHAIN_ELIDE) dropped the back-chain store from the generated code; batch 3b's natives still wrote it | batch 3b's four natives store it only without REX_BACK_CHAIN_ELIDE, as the other files already did |
| 82954F28 (exp) | the flush mode left on | #43's memo (on in the tomls since this batch) returned on a hit before setting the mode off, which the original does on every call | the hit sets it off |
| 826803B0 (one of the ten bigger closures) | r12 | with members up to 2400 lines the copy took in a member that changes r12, and wrote its own entry r12 back at the return; the original keeps r12 as a local around its __savevmx calls and leaves the callee's | back to the default closure (the other nine showed nothing); a scratch register no caller reads, so harmless, but a difference |

The new filter native (822E29A8) and the inexact natives' copies (the guard compares those: the natives decline under
it) showed none.

## The audio natives: what the code allows (2026-11-16)

**The hand-native framework allows only leaves:** natives may call other natives, but no guest function. The guard
compares every byte either form writes, the stack included, so a native writes the same stack words its original
does (82500AC0's native does).

**The seven roots' callee trees** (tools: scratchpad callee_tree.py):

| Root | Instructions | Callees | Leaf tree? |
|---|---|---|---|
| 8230AD50 (node loop) | 228 | the child processors (itself, 822E17C8, 8230A4D8, 822BD548), the kernels (822E9F20, 822EA500, 8230CBB8: natives), 822BEDD0, memcpy | no |
| 822BEDD0 (effect stage) | 103 | 14 effect processors, most leaves calling CRT math (the sine 82954478 is a native); 822E69A0 has indirect calls | almost |
| 822E17C8 (voice step) | 153 | 4 indirect calls | no |
| 8230EDE0 (resampler) | 737 | 36 direct (the XDK's XMA functions at 0x82A24xxx among them), 1 indirect | no |
| 822E98B0 (convolution reverb) | 412 | an FFT tree (822E9198, 822E8968, 82262100, 82264640...), no indirect call | yes (large) |
| 822D2378 (sound update) | 321 | the job system's yield loop 822862B0 and more | no |
| 8230B778 (node dispatch) | 80 | the child processors | no |

**The node loop's profile:** 537 samples in its copy. 118 are in the inlined multiply-add kernel, 89 in vector copies
and stores, and 330 (~3 ms) in scalar control: node-field loads that miss the cache, and spills around calls. The 1 KB
kernels themselves are cheap (~0.2 us a call).

**So:** five roots need natives that call guest code: an extension. The native gets a struct from the copy header that
calls each callee's copy (f_ form, so the guard's logged run follows it) or, outside the closure, the generated function
(the logged run stops there). hand.txt option calls=.

## More measurements (2026-11-16, later)

- **Stack slots as locals (#134 / #157 / #159):** the copies' f_ bodies have 32.6 k stack accesses against 210 k guest
  accesses. But the 30 hottest copies' own bodies have only 345 of them, 80 provably private (most frames have their
  addresses taken for callees). The stack traffic is in cold code, so the gain in the hot code is well under 1 ms.
- **The near-exact audio tolerances** (#21 / #23 / #24: audio_nx_silent_eps, _ramp_eps, _unity_eps) are off in the
  shipping toml, but the census showed #21 skips only 1.4 % more kernel calls than the exact zeros already do.
- **The console's own sampler (switch_profile_stacks = true in every shipped toml):** every 1 ms it pauses each thread
  that ran, reads its context and resumes it (svcSetThreadActivity, svcGetThreadContext3, svcSetThreadActivity: three
  system calls a thread). Its own thread is left out of the reports, so its cost appears nowhere in the CPU tables.
  Worth an A/B: 1 ms against 10 ms sampling.

## Where it stands (2026-11-16, evening)

**Built in code, not yet compiled or tested (the worktree, commits 7b1ee5bc, a6ebbbc8):**

- cold-block splitting, -fno-trapping-math (the 18 FPSCR writers excepted), -fno-math-errno for the SDK and renderer;
- relaxed CAS (#163), the PowerPC fences (#164, #165), the APC check (#171), back-chain elision (#145);
- #196: audio_switch_pump_frames (1-4 audio frames a tick; the shipped tomls keep pairs) for an A/B;
- #197: switch_profile_stacks_ms (5 in Blur's tomls);
- natives that call guest code (hand.txt calls=, localize_native.py's Calls_<id> struct). No native written on it yet.

**Expected from those:** about 2-5 ms of CPU a frame (estimates), plus whatever the sampler's pauses cost (measure).

**Most of the requested items had already been done, or are small:**

- **Already in:** #131, #138, #146, #160-#162, #167, #181.
- **No gain:** #144, #153.
- **Small:** #150, #166, #172, #173, #184, #185, and the stack slots (#134 / #157 / #159) in the hot code.

**The audio natives:** the profile and the earlier round agree that the seven roots' time is node-state cache misses
and control flow, not arithmetic (the kernels under them are natives already). An exact native cannot remove a cache
miss: about 0.5-1.5 ms for all seven together, at a large cost in work and risk.

**So the 15 ms target is not in reach with these exact items.** Measuring what is built first, then deciding, is the
honest next step.

## 15 more (2026-11-16, the user: "15 more in the batch please")

Key settings first (the audio ones may cheat, the user's word; each tolerance at most 2^-15, under one step of 16-bit
output), then code and process. ms: CPU a frame, estimates.

| # | Item | Kind | Est. ms | Status |
|---|---|---|---|---|
| 1 | #21 audio_nx_silent_eps 2^-15: gains under it not mixed | audio, inaudible | 0.05-0.2 | in the tomls |
| 2 | #23 audio_nx_unity_eps 2^-15: gains within it of 1.0 as adds | audio, inaudible | 0.05-0.2 | in the tomls |
| 3 | #24 audio_nx_ramp_eps 2^-15: near-equal ramps as constant gains | audio, inaudible | 0.1-0.3 | in the tomls |
| 4 | #49 audio_nx_resample_copy: ratio-1 resampling as a copy | audio, inaudible | 0.1-0.3 | in the tomls |
| 5 | #12 banjo_xma_frame_cache_hash_only: the cache matches its 64-bit key without comparing packets | audio, near-exact | 0.05-0.2 | in the tomls |
| 6 | audio_xma_kick_wake_idle_only: a kick wakes the XMA decoder only when it waits | exact | 0.05-0.2 | in the tomls |
| 7 | #43 cpu_nx_exp_memo: the exp native's last 64 arguments per thread | exact | 0.1-0.3 | in the tomls |
| 8 | the XMA frame cache 256 -> 384 MB | exact | 0.05-0.3 | in the tomls |
| 9 | CG-06: the indirect-call promotions refreshed from a fresh PC census | exact (guarded) | 0.3-1.0 | in the PC batch (census), then the codegen |
| 10 | CG-11: the card run flushed before a call only when the callee's tree can synchronise | exact | 0.1-0.3 | done (localize_native.py call_syncs) |
| 11 | CG-28: the copies' functions at 32 bytes, loops and jump targets at 16 | exact | 0-0.2 | done (BLUR_SWITCH_ALIGN_HOT) |
| 12 | inline-unit-growth 12 -> 20 with cold-block splitting | exact | 0-0.3 | done |
| 13 | ~~a longer PGO training race~~ | - | - | skipped (the user: "Skip longer pgo please"); the usual training race |
| 14 | #26 switch_yield_spin: an A/B config | timing | 0-0.3 | config |
| 15 | #196 three audio frames a tick: an A/B config (with the four-frame one) | timing | 0-0.5 | config |

**Checked and dropped on the way:**

- **CG-30** (NewOp's clean-page skip): the store cards cover only the views Renut reads. Cards for the device's state
  pages would mean a check in every guest store, and a missed one is a stale draw state.
- **CG-34** (Replay's prefetch): already there (the op records three ahead, the next state's lines; one variant
  measured worse, banjo_renut_replay_record_prefetch).
- **CG-31** (the render thread's memcpy): the profile's caller histograms keep only a thread's top entries, too few to
  find memcpy's callers.
- **CG-17 / CG-20** (stack store-to-load forwarding): the same address expression and no store between, so GCC
  forwards those already.
- **The Bink decoder during races:** no sample in the nx race.


## The console A/Bs (planned; the user, 2026-11-16: "Do a/bs of the untested and new stuff when you get to console")

Each config is the shipped renut.toml with one group as before, raced ABBA against renut.toml on the same build (the
usual measured race; wall-clock fps over the first 12 racing lines, the [latency] line where it matters). A group that
loses is split into its keys.

| Config | What is turned back | Measures |
|---|---|---|
| (the build) against c2b | the whole batch and the base merge (compile-time items included: cold splitting, -fno-trapping-math / -fno-math-errno, the hot alignment, #145, CG-11, CG-06, spin_hint_yield, the NORMSHORT2 form) | the total |
| renut_15ms_audio_natives_off | the seven inexact audio natives | their gain (and the PC's three empty frames, by ear) |
| renut_15ms_exact_natives_off | the resampler's filter and converter, memset, memcpy | their gain |
| renut_15ms_keys_off | the batch's new shipped keys | their gain |
| renut_15ms_pump3 / pump4 | #196, three / four audio frames a tick | fps against latency |
| renut_15ms_old_atomics | #163-#165 (the relaxed CAS, the split fences) | their gain |
| renut_15ms_yieldspin | #26 | timing |
| renut_async_present_off | port 145's async present (RR6: pacing to check on a console) | fps, swap -> present max |
| renut_p145_renderer_off | port 145's stream redeem, kept targets, dirty rows, native 16-bit | render thread |
| renut_p146_runtime_off | port 146's lock-free mutant release and file pieces | fps, load times |
| renut_p143_kernel_off | port 143's overlapped delay and XMP throttle | boot and menu times |

## The audio natives on the PC (2026-11-16, round 2: the merged build, four races, natives on / off / off / on)

The six inexact natives the guard cannot check, compared through the PC's audio dump (120 s of each race from 150 s;
scratchpad audio_dump_compare.py, audio_silences.py). No non-finite sample, no clipping, no silent stretch in any of the
four; peaks 0.473-0.479. Spectral centroid: on 2,998 / 3,151 Hz, off 3,053 / 3,127; loudness on -22.65 / -22.69 dBFS
(left), off -22.45 / -21.79; octave bands within about 0.5 dB. The two off races differ from each other as much as on
from off: no difference the natives make shows. Round 1's three empty frames and its brighter on-race came with that
run's census and another session's build beside it (none in any round-2 race).
