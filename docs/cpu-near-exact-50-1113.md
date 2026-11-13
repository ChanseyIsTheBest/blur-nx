# Blur: 50 near-exact CPU ideas (2026-11-13)

Near-exact means each idea gives the same results except in rare cases, as CG-13 does (`REX_FPCR_ONE_MODE`: one
flush mode for all code). The user asked for them in the chat ("come up with 30 ideas for cpu like CG-13 that aren't
exact in very rare circumstances but could help improve performance", then 20 more, then a universal column and a
census-effort column).

**The user's choice (13:55):** "do nearly all the ones that need no census plus all the easy ones plus
1,2,3,4,41,42,8,34 and 38. Try to do the census for all of or as many as possible at a single time; you have 58 hours"
(until about 2026-11-15 23:55).

## The chosen set

| # | Idea | Census / check | Status |
|---|---|---|---|
| 1 | No signalling-NaN quieting on float stores | PC census slot 0 | switch REX_NX_SNAN_PASS (BLUR_NEAR_EXACT) |
| 2 | Float-to-int without the NaN fix-ups | PC census slot 1 | switch REX_NX_FCTI_NO_NAN (AArch64 only) |
| 3 | Round-to-nearest only | the kCensusRound* census | **dropped** (the game selects round-toward-zero); the exact part done: a write that changes nothing skips the `msr fpcr` |
| 4 | No FPU status flags | scan | **nothing to do** (never modelled) |
| 5 | No integer-overflow summary bit | scan + PC census slot 11 | switch REX_NX_XER_SO_ZERO |
| 8 | Link-register elision | the guard | SASRT_HOT_LR_ELIDE ON in the near-exact builds; guard run pending |
| 9 | Stack pointer preserved everywhere | REX_R1_CHECK (PC) | r1_preserve.py --near-exact (BLUR_NX_R1_ASSUME=1, a codegen-time choice) |
| 12 | Hash-only caches | collision census in the XMA cache's report | key banjo_xma_frame_cache_hash_only (off) |
| 13 | Smaller Begin*Vertices device copies | the narrowed copies' bytes (snapshot report), the NaN poison | **found exact**: banjo_renut_deferred_narrow (on) |
| 15 | Trust marks over compares | the texture card counts | **mostly done before** (CheckedHash / ExtentClean) |
| 16 | Skip the zero-fill before a full overwrite | a subagent's reading (14:50) | **deferred**: the zero-fills fully overwritten are 822EA3C0's eight dcbz (a hot copy) and 8230EDE0's four row-0 re-zeroes, but at the patch step their lines are the plain forms localize_native.py, store_cards.py and vec_dcbz_zva.py read exactly (a wrapper would drop 822EA3C0's copy); needs those taught or a hand native of 822EA3C0 (~0.05-0.1 ms) |
| 17 | Drop the division checks | PC census slots 2-4 | switch REX_NX_DIV_NO_CHECK |
| 19 | Remove trap asserts | 0 hits in the PC race | **found exact** (a hit only logs): switch REX_NX_NO_TRAPS |
| 20 | Unguarded promoted calls | census_indirect1 | **skipped**: ~0.01 ms a frame, and a new target would be a wrong call (a crash), not a rare tiny difference |
| 23 | Near-unity gains use the unity kernel | PC census slots 16-18, 23 | key audio_nx_unity_eps (0 = off), tools/blur_mix_nx.py |
| 24 | Tiny ramps treated as constant | PC census slots 19, 20, 24 | key audio_nx_ramp_eps (0 = off) |
| 26 | Spin briefly, then yield | none | keys switch_yield_spin (0 = off), switch_yield_spin_first |
| 27 | No-op debug output and telemetry | export census | **nothing measurable** (DbgPrint not among the most called) |
| 29 | Cheaper performance-counter reads | console counts | **nothing measurable** (0 clock reads a second in races) |
| 30 | Quiet the remaining host timers | the timer list | **done by M-38** (the rest are the game's own timers) |
| 34 | Sleep until the flag changes instead of yield-spinning | M-44 per-thread yields and sleeps (Switch profiler) | keys switch_yield_backoff_us / _after for the A/B; a wake on the job queue after the console census |
| 35 | Higher priority for the frame chain | none | exists (switch_thread_priorities, CG-01, rejected 2026-11-12): A/B again with stereo and paired audio |
| 38 | Batch dirty-page marks per block | PC census slots 25-26 | **done before** in the copies (card_groups.py, card runs); the generated tree now marks by the copies' rule (REX_CARD_VIEWS_ONLY, exact) |
| 40 | Drop stack cookies and probes | scan | **probably nothing** (no cookie check found) |
| 41 | Float compares fused into branches | PC census slot 5 | switch REX_NX_FCMP_NO_UN |
| 42 | Vector min/max without fix-ups | PC census slots 6, 13 | switch REX_NX_VMINMAX_FAST (tools/near_exact.py) |
| 48 | Skip draws that change nothing | Renut's draw kinds | **nothing to skip** (0 such draws in the PC race) |
| 49 | Skip resampling at ratio ~1 | PC census slots 27-28 | key audio_nx_resample_copy (off): the kernels (822D7880, 822D9040, 822D79A8) already copy at ratio == 1.0 exactly; a block consuming exactly 256 samples (fraction 0 everywhere) takes that copy too (about ratio 1 to 1.004 / 0.998 to 1.002) |

Already done before the choice: #11 (store_cards.py has always left stack stores plain: 186,852 in cycle 3's codegen;
only stores that reach the stack through a pointer the tool cannot trace are marked) and #14 = M-35 (found exact:
cycle 3).

## Method

- One census build (PC) with every runtime census on, one race; the static scans need no run.
- Each idea is a switch: compile time for generated code, a cvar for the runtime. Off is exactly as before.
- Verification where it applies: the guard (`banjo_cpu_verify`), the store-cards and deltas verify modes, audio
  dumps compared to one 16-bit step, frame hashes.
- Then one Switch PGO cycle with everything on (cycle 3's exact items too) and ABBA against the c2b build; the
  timing-only cvars (#26, #34, #35) also A/B'd alone.
- Base-universal ones go to RR6 after the A/B.

## The full list (as sent to the user, sorted by the estimated ms a frame)

| Rank | # | Idea | Est. ms | Only differs when | Risk | Universal | Census / check |
|---|---|---|---|---|---|---|---|
| 1 | 6 | Scratch registers dead across calls | 1-3 | hand-written assembly breaks the convention | High | Yes | Hard |
| 2 | 34 | Sleep until a flag changes instead of yield-spinning | 0.5-2 | never (timing) | Low-med | Per game | Easy |
| 3 | 21 | Skip inaudible voices and sends | 0.5-1.5 | many inaudible voices add to one step | Low-med | Per game | Medium |
| 4 | 37 | GPU fences and queries done a frame early | 0-2 | the game reads GPU results back | High | Yes | Hard |
| 5 | 11 | No dirty-page marks for stack stores | 0.5-1 | the GPU reads stack-built data | Med | Yes + check | done before |
| 6 | 8 | Link-register elision | 0.3-1 | code reads its return address as data | Low | Yes | Easy |
| 7 | 26 | Spin briefly, then yield | 0.3-1 | never (timing) | Low | Yes | None |
| 8 | 33 | Host-native critical sections | 0.3-1 | the game reads a critical section's fields | Low-med | Yes | Medium |
| 9 | 31 | Store-to-load forwarding in copies | 0.3-1 | another thread writes in between | Med | Yes | Hard |
| 10 | 43 | Memoize hot pure functions | 0.2-1 | a hash collides | Med | Per game | Medium |
| 11 | 38 | Batch dirty-page marks per block | 0.3-0.8 | a snapshot lands in the window | Low-med | Yes | Easy |
| 12 | 13 | Smaller Begin*Vertices device copies | ~0.5 | an unlisted field is read | Med | Yes | Easy |
| 13 | 35 | Higher priority for the frame chain | 0-1 | never (timing) | Low | Yes + census | None |
| 14 | 36 | Ignore thread affinities | 0-1 | per-core data without locks | Med-high | Yes + census | Hard |
| 15 | 12 | Hash-only caches | 0.2-0.5 | a hash collides | Very low | Yes | Easy |
| 16 | 1 | No signalling-NaN quieting | 0.2-0.5 | data holds a signalling NaN | Low | Yes | Medium (shared build) |
| 17 | 22 | Cut filter and reverb tails | 0.2-0.5 | never audible | Low | Per game | Medium |
| 18 | 41 | Float compares fused into branches | 0.2-0.5 | a compare sees NaN | Low | Yes | Medium (shared build) |
| 19 | 49 | Skip resampling at ratio ~1 | 0.2-0.5 | never audible | Low | Per game | Easy |
| 20 | 50 | 3D panning only on audible changes | 0.2-0.5 | never audible | Low | Per game | Medium |
| 21 | 44 | Host 2-input math | 0.2-0.5 | an unsampled input disagrees | Low-med | Yes + addresses | Medium |
| 22 | 9 | Stack pointer preserved everywhere | 0.2-0.5 | fibers / stack switching | Med | Yes | Easy |
| 23 | 32 | Plain operations for one-thread locks | 0.2-0.5 | a second thread uses one later | Med | Yes + census | Medium |
| 24 | 39 | Dead stack stores removed | 0.2-0.5 | read through a pointer | Med | Yes | Medium |
| 25 | 46 | Host-native heap allocator | 0.2-0.5 | code depends on pointer order | Med | Yes + addresses | Hard |
| 26 | 47 | Skip unreadable shader constants | 0.2-0.5 | a relative index leaves the range | Med | Yes | Medium |
| 27 | 16 | Skip the zero-fill before a full overwrite | ~0.3 | never where proven | Low | Yes | Easy |
| 28 | 25 | Short-circuit silent voices | ~0.3 | state not fully decayed | Low | Per game | Medium |
| 29 | 10 | Drop saves of preserved registers | ~0.3 | something reads the save slots | Med | Yes | Medium |
| 30 | 15 | Trust dirty-page marks over compares | ~0.3 | a write skips the marks | Med | Yes | Easy |
| 31 | 40 | Drop stack cookies and probes | 0.1-0.3 | the stack overflows | Low | Yes | Easy |
| 32 | 42 | Vector min/max/compare without fix-ups | 0.1-0.3 | NaN, or +0 meets -0 | Low | Yes | Medium (shared build) |
| 33 | 45 | Host-native sprintf | 0.1-0.3 | a last digit rounds differently | Low | Yes + addresses | Medium |
| 34 | 48 | Skip draws that change nothing | 0.1-0.3 | a missed side effect | Low | Yes | Easy |
| 35 | 7 | Scratch condition-register fields dead | ~0.2 | same as #6 | Med | Yes | Hard |
| 36 | 3 | Round-to-nearest only | 0-0.3 | the game changes the rounding mode | Low | Yes + census | Easy |
| 37 | 14 | Smaller resolve copies (M-35) | ~0.15 | never: exact | none | Yes | done (cycle 3) |
| 38 | 23 | Near-unity gains use the unity kernel | ~0.1 | gain within 1e-7 of 1.0 | Very low | Per game | Easy |
| 39 | 24 | Tiny ramps treated as constant | ~0.1 | never audible | Very low | Per game | Easy |
| 40 | 29 | Cheaper performance-counter reads | ~0.1 | never | Very low | Yes | None |
| 41 | 2 | Float-to-int without NaN fix-ups | ~0.1 | the input is NaN | Low | Yes | Medium (shared build) |
| 42 | 5 | No integer-overflow summary bit | ~0.1 | the overflow-recording forms are used | Low | Yes | Easy |
| 43 | 20 | Unguarded promoted calls | ~0.1 | a new target appears | Low | Yes + census | Easy |
| 44 | 17 | Drop the division checks | ~0.05 | divide by zero | Very low | Yes | Easy |
| 45 | 19 | Remove trap asserts | <0.05 | it would crash anyway | Low | Yes | Easy |
| 46 | 18 | Drop the 32-or-more shift check | <0.05 | a table holds 32 or more | Med | Per game | Medium |
| 47 | 4 | No FPU status flags | ? | the game reads the flags | Low | Yes + census | Easy |
| 48 | 27 | No-op debug output and telemetry | ? | never | Low | Partly | Easy |
| 49 | 28 | No-op performance markers | ? | never | Low | Per game | Medium |
| 50 | 30 | Quiet the remaining host timers | small | never | Very low | Yes | Easy |

## Census results

From the static scans of cycle 3's generated code and cycle 3's PC check race (check_c3, 14:00, census on):

- **#3 dropped:** the guest writes round-toward-zero: FPSCR rounding writes 23,629 to nearest and 11,776 toward zero
  (cumulative, boot to a race). Forcing nearest would change real results. Kept the exact part: a write that leaves
  the host word as it is skips the `msr fpcr` (compared with the live register, `mrs`, not the cached copy: a
  thread-context restore sets that without writing the host's).
- **#4 nothing to do:** the port never models the FPSCR's status flags; `mffs` (19 sites) returns the rounding mode
  only.
- **#5:** no overflow-recording form (addo, subfo, mullwo, divwo, nego...) anywhere in the code, one `mtxer` (a
  context restore that also restores CR). The census counts compares that copy SO = 1.
- **#9:** with the assumption, 49,594 of 49,746 functions count as preserving r1 (48,260 proven), 54 fail in their own
  body (13 setjmp / longjmp, ~25 exits that disagree, the rest untracked r1 writes).
- **#11 already done:** store_cards.py has always left stack stores plain (186,852 in cycle 3).
- **#15 mostly done:** Renut already skips texture and physical-range hashes when the cards are clean
  (`CheckedHash`, `ExtentClean`; banjo_renut_store_cards_verify checks it). The census run's card counts show what
  is still hashed.
- **#19:** 563 `twllei rX,0` (divide-by-zero traps before divisions: `if (r == 0) ppc_trap(...)`, one compare and
  branch each), 18 `twi`, 9 `tdlgei`.
- **#27 probably nothing:** DbgPrint is not among the 16 most called exports (NtYieldExecution, NtSetEvent, waits, TLS,
  KeQueryPerformanceFrequency lead).
- **#29 nothing measurable:** the console counts 0 guest clock reads a second in races (UpdateGuestClock); 17 `mftb`
  sites; KeQueryPerformanceFrequency (13,000 a second on the PC) returns a global.
- **#34:** NtYieldExecution 127,000 a second on the PC in the race (4,309 on the console, plus 1,694 short sleeps a
  second that last 1.02 ms each: switch_yield_backoff's "100 us" sleeps). The job workers' loop (sub_8227CCE8) waits
  on its event when its queue counter says empty; the yields come from scans that find work pending but none it can
  pop (sub_8227CC58 scans up to 10 queues, then SwitchToThread). The console census (M-44's per-thread yields and
  sleeps, the Switch profiler) decides between a shorter backoff and a wake on the queue.
- **#40 probably nothing:** no stack-cookie check among the most called functions (the top ones are memcpy, the
  register save / restore helpers and small getters): Blur seems built without /GS.
- **#42:** 1,924 vmaxfp / vminfp sites (all rewritten to the helpers); the vector compares need no fix-ups (Arm's
  fcmgt and SSE's cmpgt agree for every input).
- **#48:** Renut already counts "draws that write nothing at all" (DiagCountsOn); the census run reads it.

## Builds

- PC census build: the near-exact codegen (BLUR_NX_R1_ASSUME=1), BLUR_NEAR_EXACT OFF (the census counts the rare cases
  in the code as it was), SASRT_HOT_LR_ELIDE ON (for #8's guard run); a race with banjo_diag_runtime_census and the
  poison of banjo_renut_deferred_narrow, then a guard run.
- Switch: cycle 3 alone (exact items) and cycle 3 + near-exact (BLUR_NEAR_EXACT ON, SASRT_HOT_LR_ELIDE ON, the
  near-exact codegen), each its own PGO training; ABBA against each other and the c2b build; the keys A/B'd in the
  same session.

## Log

- 2026-11-13 13:55: the user's choice; this doc.
- 2026-11-13 14:00-15:00: the census code and every switch written (commits on branch cycle1).

## The refilled 50 (2026-11-13 15:15, the user: "with this new information make another table and refill it to 50")

Removed (resolved): #13 and #19 found exact and built; #11, #14 (M-35), #15, #30 (M-38), #38 and #36 done before (the
console profile shows every game thread allowed on all four cores: mask 0xF); #4, #27, #29, #40 and #48 nothing to
gain; #3 dropped (round-toward-zero is used); #20 skipped. Estimates moved: #34 up (the backoff's "100 us" sleeps last
~1 ms), #26 down (the CPU is 87 % busy: a spin takes a core others want), #1, #9, #12 and #41 down.

The 15 new ones:

| # | Idea | Est. ms | Only differs when | Census / check |
|---|---|---|---|---|
| 51 | Begin*Vertices captures as deltas against the previous one (a chain of their own) | 0.1-0.3 | never (exact) | the deltas verify mode |
| 52 | Begin*Vertices vertex data written straight into the capture (no second copy) | 0.1-0.2 | never (exact) | none |
| 53 | #13 per register group (the masks' 64-byte groups), not a prefix | 0.05-0.1 | never (exact) | the NaN poison |
| 54 | Whole captures skip the device's never-read ranges (the head 1.1 KB, the private 0.9 KB) | ~0.05 | never (exact) | the deltas' narrow map |
| 55 | Clears narrowed like resolves (M-35's method on ClearStateLocked) | ~0.05 | never (exact) | read ClearStateLocked |
| 56 | Lock-free handle lookups (ObjectTable: 11,700 a second, 218 contended) | 0.05-0.2 | never (exact) | a stress test |
| 57 | Event set and wait fast paths without the mutex (8,000 signals a second with nobody waiting) | 0.1-0.3 | never (exact) | a stress test |
| 58 | The job queue's scan (sub_8227CC58: 10 queues, ~4,000 failed scans a second) as a native | 0.05-0.2 | never (exact) | the guard |
| 59 | Audio threads on a core of their own (JQ3, JQ4, the XMA decoder on core 3; the frame chain on 0-2) | 0-1.5 | never (timing) | A/B (switch_thread_cores) |
| 60 | No yield backoff for the frame-chain threads (the others back off sooner) | 0-1 | never (timing) | M-44's per-thread counts |
| 61 | Fully open voice filters as a bypass (audio) | 0.1-0.4 | never audible | coefficient census |
| 62 | The stereo pump takes the two channels directly when Blur mixes stereo (no fold, no gain scan) | ~0.05 | never (the output is the same) | none |
| 63 | Renut's per-draw counters as single-writer adds (M-39's kind) | ~0.05 | never (exact) | none |
| 64 | The mixer's row zero-fill folded into its first kernel (a store, not an add) | 0.1-0.3 | +0 against -0 (output the same) | audio dumps |
| 65 | XMA kick coalescing (M-41) | 0-0.8 | never (timing: decodes up to 0.2 ms later) | M-44's counts, "requests without data" |

## Work on the whole table (2026-11-13 14:53 onward, the user: "Work on the entire table for 24 hours please try and do all at once or as many as possible for the most efficient census possible")

Status by row of the refilled 50 (15:15):

- Built as switches or keys (off by default), with their census: #1, #2, #5, #17, #41, #42 (BLUR_NEAR_EXACT); #9
  (the near-exact codegen); #12; #21 (audio_nx_silent_eps), #23, #24, #49 (audio_nx_resample_copy); #26, #34's
  backoff numbers, #60 (switch_yield_backoff_exempt); #37 (rr6_frames_ahead may be 2; census of the game thread's
  waits by source); #65's exact part (audio_xma_kick_wake_idle_only); #8 and #10 (SASRT_HOT_LR_ELIDE,
  BANJO_GEN_LR_MOVES_ELIDE, BANJO_GEN_NV_SAVES_ELIDE in the near-exact builds, the guard run).
- Found exact and built: #53 (the narrowed captures by register groups), #54 (without the never-read ranges), #55
  (a queued clear keeps 48 bytes).
- Census only (the implementation after it): #32 (CAS words by thread), #47 (draws with indexed constants), #57 / #34
  (polls and their timeouts).
- Already the case: #33 (RtlEnter / LeaveCriticalSection are host code).
- Too small to matter: #62 (~0.01 ms), #63 (~0.01 ms).
- Second wave (after the census): #51, #52 (a scratch ring with fences), #56, #58; #6 / #7 / #31 / #39 (codegen
  analyses); #16 / #64 (the tools that read the zero-fills); the guest functions a subagent is identifying for #18,
  #22, #25, #28, #43, #44, #45, #50, #61.
- Settings A/Bs on the console, no build: #26, #34 (switch_yield_backoff_us / _after), #35, #37 (rr6_frames_ahead 2),
  #59 (switch_thread_cores), #60.

### A subagent's reading of the guest code (2026-11-13 ~15:40) and what it settles

(console samples: app/src/cpu/hot/localized_bulk.txt, 49,845 in all)

- #43 memoize: **exp (82954F28, 446 samples)** is called mostly by the audio parameter fetch 822EB500 (533) with the
  same dB offsets each frame: cpu_nx_exp_memo (exact: a pure function; built). sin (82954478, 898; physics 8250A7E8)
  has continuously changing arguments: no memo.
- #44 host 2-input math: atan2 (82954DC8, 26 samples), pow (829552D8, 36, helper 829550E0 224), fmod (82954E38, 2):
  every CRT routine is double precision, and host libm differs from them in the last bits often (not a rare case):
  **dropped**.
- #45 sprintf: the narrow and wide printf cores together ~70 samples (0.15 %): **too small**.
- #18 shift guards: the hot variable-shift readers are the LZX decoder (829D2E80 1,062 samples, 1.72 ms a frame on
  JQ1 / JQ2) and a Bink video decoder (82B043D0 436, 82AFD370 375, ~130 more): the guard is a small part of them;
  **park** (the decoders themselves are the bigger questions below).
- #28 performance markers: none run per frame (the perf-capture system has 0 samples): **nothing**.
- #50 3D panning: 822EB970 runs only when a parameter changes (~7 samples): **nothing**.
- #61 open filters: the one-pole low-pass is never fully open (b0 0.558 at its top cutoff); the high-pass at 20 Hz is
  near identity but not within a 16-bit step (it removes DC): **dropped**.
- #22 reverb tails: 822E98B0 is an FFT-partitioned convolution that runs on silence too, but the whole path is ~73
  samples (0.15 %): **too small**.
- New questions (not in the table): the Bink decoder runs during races (~940 samples, ~2 % of all; a second subagent
  is finding which movie and whether it is shown); the LZX decoder of streamed resources (M-29, an exact native) at
  1.72 ms a frame on the frame chain's job workers.

### Correction (2026-11-13 15:45): the copy list's sample counts are stale; Bink does not run in races

A second subagent attributed every Bink symbol in 7 console race profiles against their own builds' ELFs: Bink runs
only 10-30 s after launch (the boot logos 'Ui\movies\atvi.bik' and 'BizarreSplash.bik', on the Game Master) and has
0 samples in every report of the menus, the loading and the 240 s race. localized_bulk.txt's counts are kept from the
list's first version by `switch_hot_bulk.py --merge` (the Bink lines date from the boot-movie work of late October),
so they rank functions, they do not measure today's races. Checked against cycle 1's race profile (perrep.py, 5 race
reports): the exp native is inlined into the parameter fetch 822EB500, which takes 0.5-3.4 % of each audio worker
(#43's memo: ~0.1-0.3 ms a frame, not ~1 ms); the LZX decoder (829D2E80 and its siblings) takes 4-10 % of JQ Worker 1
and of JQ Worker 2 in every race report (~1.5-2 ms a frame on the frame chain's workers: M-29, an exact native).

### The PC census race (census_nx1, 2026-11-13 15:55-16:04: 480 s from boot into a race) and the guard (guard_nx1, 600 s)

Cumulative counts, boot to the race's end. Build: the PC census build (BLUR_NEAR_EXACT OFF, the LR and save elisions
of #8 / #10 ON). 0 crashes in either run; the race renders right (screenshots every 60 s).

| # | Census | What it says |
|---|---|---|
| 1 | 8 signalling NaNs at float copy stores | rare: the switch stands |
| 2 | 39,972 NaNs into float-to-integer conversions (~80 a second) | **not rare**: the switch would change real results (0 for 0x80000000); off unless the sites' consumers are shown not to care |
| 5 | 0 compares copying XER[SO] = 1 | never: the switch stands |
| 17 | 0 divisions by zero, 0 INT_MIN / -1 | never: the switch stands |
| 41 | 20,164 float compares with a NaN | not rare: the switch is right only if nothing reads cr.un after them (the static check of bun / cror readers next) |
| 42 | 63.3 M of 1,526 M vector max/min lanes differ (4.1 %) | **not rare** (mostly +0 against -0, presumably): the fast form dropped; the exact helpers stay |
| 3 | 35,405 FPSCR writes, 23,552 change the mode | the exact skip saves the other 11,853 `msr fpcr` |
| 4 | 35,405 FPSCR reads | nothing to model |
| 9 | the r1 mirror: 321 M calls checked, 0 differences | the near-exact codegen's own check comes with the next build (BLUR_NX_R1_ASSUME=1) |
| 12 | 0 key collisions | the hash-only key stands |
| 13 / 53 / 54 | **0 captures narrowed**: a bug | every shader of Blur's is an in-place (XGRegister) one, which only the render thread's map held; fixed (3c7a300b: the game threads' own map, checked on the render thread) |
| 21 | 202,828 constant gains under 2^-16 (13.97 M exactly zero) | the silent threshold catches 1.4 % more than the exact zeros |
| 23 | 21,402 gains within 1e-5 of 1 (the same within 1e-7), 1.71 M exactly 1 | small: +1.3 % unity kernels |
| 24 | 6.28 M ramps; 289 k under 1e-6, 619 k under 2^-16; 8,184 with both ends under 2^-16 | 4.6-9.9 % of ramps become constant gains |
| 32 | 2,214 of 4,029 CAS words written by one thread only; 12.0 M of 155 M CAS on them (7.7 %) | worth building only where the word is provably one thread's |
| 37 | the game thread waits at Swap's frames-ahead limit 31-54 times a second, 89-354 ms a second; fences 27-80 ms | rr6_frames_ahead 2 is the console A/B |
| 38 | 684 M card marks by the generated tree, 10.35 G run marks by the copies | the copies' marks are the target (15x) |
| 43 | exp memo: 28.3 M hits, 8.0 M misses (78 %) | built (cpu_nx_exp_memo, exact); the console A/B |
| 47 | 163 k of 727 k draws' vertex shaders index all constants; the rest read 12.5 registers | the indexed ones need the range check |
| 49 | 0 resampling blocks (the PC never resamples) | the console's census |
| 52 | ~49,500 Begin*Vertices draws and ~11 MB of vertex data every 10 s in the race | the second copy is ~1.1 MB a second |
| 57 / 34 | 5.44 M waits that succeeded, 225,876 polls, 201,974 of them timed out and yielded | the polls are the yield source; the console's M-44 counts next |
| 65 | 0 kicks skipped (the PC decodes on the calling thread, mode 0) | the console |

The guard (600 s, 116 M copy calls compared): 4 differences, all the sign of a NaN in a register (v0 or f3: 0x7FC00000
against 0xFFC00000), after a zero-length vector is normalised (frsqrte of 0, then inf x 0): sub_8233FCA8,
sub_824E90D0, sub_823262D8, sub_82309438. Both forms use the same FmaddsSingle; an arithmetic NaN's sign is the
compiler's choice (a negation folded into the multiply-add or not), so the copy and the generated code can differ there
on the PC. The copies switched themselves off for the rest of the run, as the guard does. None in any earlier run today.

- 2026-11-13 16:20: #13's fix committed; main to cycle1; the near-exact codegen (LZX hook routing, #9) and the PC build
  started, then the LZX verify race (cpu_lzx_native=2) and a census race for the narrowing (with the poison).

### The static census of the machine copies (#6, #7, #39, #31; a subagent, 2026-11-13 16:25)

Over the 4,780 unique copies (9,534 definitions in 2,881 headers; 21,247 call sites). Scripts and JSON:
the session scratchpad's agent_census (census_regs.py, census_cfg.py + cfg_live.py, census_stack.py, census_mem.py).

- **#6 scratch registers dead across calls: dropped (nothing left to gain).** 138,324 reloads of r0, r5-r12, f0,
  f2-f13 after a call; whole-function liveness: 113,466 dead (82 %: the compiler drops them already), 38 read by the
  copy's own code (36 after the stack probe sub_82954F04, which writes only r0 and r11: right to read them), 29 by a
  later copy, 15,165 only handed to a later non-copy call, 9,626 only written back at a return. r4: 5 reads, all after
  the probe. Vectors: 90,951 reloads, 67 read (52 of them v1). What the assumption would save is the hand-overs and
  write-backs: one store each, and wrong after the probe.
- **#7 CR fields after a call: nothing.** 54 reloads, 0 read before being set; 27,051 CR locals kept across calls, 0
  read before being set.
- **#39 dead stack stores: too small.** The literal rule's 6,740 overcount (3,268 are fields of a stack object passed
  by pointer, 80 stack arguments, 253 outgoing-argument stores); really unread: 2,617 back-chain stores, ~24 others
  and 3 saves (sub_82A74E10, no return path). The back chain is what stack walks read: kept.
- **#31 store-to-load forwarding: too small to build.** 951 loads in 327 functions match an earlier store at the same
  base and offset (902 on the stack; 652 integer-to-float moves through the stack), 365 more inside a wider store; the
  C++ compiler forwards most of these itself (not checked in the assembly).

## The second wave (2026-11-13 17:00-18:30, the user: "do all 13 left in the second wave ... avoid compiling a good amount"; then "add all four" (#66-#69) and "do not worth it ones if they are exact so they aren't repeated again")

The check race of the #13 fix and the LZX native (check_nx2, 16:51-16:59): every Begin*Vertices draw narrowed (69,694
in 10 s; 2,799 bytes a copy against 12,888; 0 drawn with other shaders), the race right under the NaN poison; the LZX
verify 7,874 buffers compared, 0 differ, 480 fallbacks on the error flag (fixed below); the r1 mirror with #9's
assumption 353 M calls, 0 differences.

| # | Outcome | How it was settled |
|---|---|---|
| 41 | **exact in practice: the switch stays** | a dataflow scan of every function: the 2,148 reads of a CR field's so/un bit (all `mfocrf rN,2` packs of cr6, and 8 `mfcr` of the field at entry) are reached only by vector compares, never by a float compare |
| 51 | **built** (banjo_renut_deferred_chain, a console key) | the chain existed, off for N&B's two devices; now a draw of another device while a Begin*Vertices delta waits is a whole capture outside the chain (cross-device count in the 10 s line). Blur draws on one device |
| 56 | **built, on** (kernel_handle_lookup_own_lock) | handle lookups take the table's own short lock instead of the global critical region (11,700 a second, 218 blocked); every writer takes both, global first, no call out under the table lock |
| 34 | **built** (switch_yield_wake_on_progress, a console key) | the job system read here: sub_8227CF60 runs jobs (sub_8227C5F8 after each, then the +472 / +52 decrements), sub_8227E098 pushes one (+48), sub_822862B0 waits for a group (helps, then yields through sub_829BB640); hooks on sub_8227C5F8 and sub_8227E098 bump a progress word the backoff's sleep waits on (svcWaitForAddress, the same timeout). Both functions out of the copies |
| 62 | **built, on** (audio_stereo_direct; exact) | a frame with channels 2-5 all zero converted from its front pair (the same samples into the +0 mix) |
| 66 | **built, on** (BLUR_CARD_VIEWS_ONLY; exact) | the copies already had the views rule, the check-first and the runs (VS-11 / VS-12, 2026-11-11); the 10.35 G "run marks" counted every flush, an empty one too. The generated tree's equivalent is now in every build; the census counts real marks |
| 67 | **built** (cpu_lzx_cache_kb, 0: the repeats counted) | decoded buffers kept by their compressed bytes (XXH3, compared in full); the verify mode checks a kept buffer against a fresh decode |
| 69 | **built, on** (banjo_cpu_verify_nan_sign) | the guard takes a register NaN differing only in its sign as the same (counted) |
| LZX | **fixed** | the guest's only end check is before each main symbol; its block refills check nothing (the 480 fallbacks) |
| 57 | already done | an event set with nobody waiting skips the wake (thread_signal_skip_idle); the multi-wait notify is one load when none is parked: one uncontended lock is left |
| 63 | already done | Renut's per-draw counters are single-writer adds (Bump) |
| 25 | already done by the game | the voice node sub_822E17C8 compares its start and end gains with a threshold and takes its own silent path |
| 52 | not worth it | the Begin*Vertices scratch is one block per device reused by the next Begin: leaving the data in place needs a ring the render thread frees after its draw, for ~1.1 MB/s (~0.005 ms a frame) |
| 58 | not worth it | sub_8227CC58 is 36 instructions; its cost is the yield (#34) |
| 68 | not worth it | only 3 of the 662 CAS sites have a guest fence on both sides (Blur has no isync); the rest rely on the CAS's own ordering |
| 16 | not worth it (slower) | the zero-fill before sub_822EA3C0's 1 KB copy is DC ZVA on the Switch (vec_dcbz_zva): the lines are claimed without a read, which the copy's stores would otherwise make; with overlapping buffers it would not be exact either |
| 64 | not worth it | a bus zeroes its rows with dcbzl (DC ZVA) and its children (buses, voices, effects) add into them; the first child would have to know it is first, to save one cached load a sample |
| 46 | not worth it | Blur's allocator is a small-block pool (sub_8228A3C0 / sub_8228A470, in front of the CRT heap, sub_8217C5F0 / sub_8217C870): ~50 of the copy list's 49,744 console samples, all already copies; another allocator would change the pointers |
| 47 | not precise enough | an indexing shader's reachable registers depend on its index register (the GPU's); the upload already compares the dirty groups and reuses |
| 32 | not precise enough | no per-word proof that a CAS word stays one thread's (a second thread later would lose an update); 7.7 % of the CAS operations, ~0.01 ms a frame |

The near-exact set (BLUR_NEAR_EXACT) is now #1, #17, #41, #5, #19 (#2 and #42 out by the census). One codegen and
one PC build for the whole wave (main at the second wave's commits), then check_nx3 / guard_nx2 (tools: the
scratchpad's nx_pc_wave2.sh), then the Switch near-exact pipeline.

### The second wave's PC check race (check_nx3, 2026-11-13 17:55-18:03, 480 s, census on)

- **LZX native (verify mode):** 8,345 buffers compared, **0 differ, 0 fallbacks** (the end-check fix: every buffer
  now decoded by the native). #67: 683 buffers (8 %, 9.1 MB in) were decoded before among the last 1,024; the kept
  copies checked against fresh decodes, 0 differ. cpu_lzx_cache_kb=8192 joins the console set.
- **#51 the Begin*Vertices chain:** every Begin*Vertices draw in the chain (0 whole copies), 591 bytes kept a draw on
  average (12,888 the whole device; the narrowed copies were 2,799); the deltas verify 106,703 draws checked, **0
  differ**; 0 draws of another device (Blur draws on one).
- **#66:** the copies' flushes with a run pending 3.77 G (the old count, 10.35 G, had the empty ones), 166 M of them
  on a page Renut reads, **197,750 writing a card**: the check-first leaves almost no card stores in the copies.
- **#9:** the r1 mirror 383 M calls, 0 differences. **#43:** 76 % memo hits. The copies' guard was off in this race
  (guard_nx2 next). 0 crashes; the race renders right under the narrowing poison.

### Driving (the user: hold RT in the PC races, as the console's hold ZR) and the console training race

- **check_nx4** (19:00-19:08, 480 s, RT held from 125 s; at 300 s the car at 104 mph on lap 3): LZX verify 24,851
  buffers compared (three times the standing race's: the track streams while driving), **0 differ, 0 fallbacks**;
  4,419 of them (18 %) decoded before among the last 1,024 (#67's cache matters more in a driven race). The chain
  117,198 draws verified, 0 differ, 548 bytes a draw. The r1 mirror 348 M calls, 0 differences. 0 crashes.
- **guard_nx2** (18:29-18:39, not driven): 114 M copy calls compared, **0 differences** (0 NaN-sign cases), but no
  picture from 120 s on (60 fps throughout): the known intermittent load hang of guard runs (the progress log's open
  item; a run without the guard and with the LZX native's output in use, lzx_mode1_a, loaded the race: 8,354
  buffers). guard_nx3 (driven) after the Switch build.
- **The console training race** (2026-11-13_pgo_train_nx): the first try's eight menu presses stopped one screen
  short (the race select) and its 240 s drive held ZR on the menu: that stream set aside (menu_only/); the second try
  (one more confirm, the race loading checked) in the race at 20 s; one stream of 10.2 MB, merged into 537 files, 0
  mismatch. The final build (switch-pgouse) building.

### The driven guard race (guard_nx3, 2026-11-13 19:00-19:10, RT held)

2,832 of 2,857 copies reached (the most yet: driving reaches the race code), 757 M calls compared; 51,246 register
results that differed only in a NaN's sign taken as the same (#69); **28 differences**:
- 25 are r12 (a few r11) at the return: the copy leaves the address its vector save computed (r1 + the offset) where
  the original leaves the offset (sub_82729178: `li r12,-160 ; stvx128 v127,r1,r12`), the class of sub_8231C000
  (2026-11-13 morning): a volatile register dead at the return under the ABI. 18 functions (sub_822279B8,
  sub_8275EB78, sub_825634B8, sub_82729178, sub_82725498, sub_8268EC30, sub_8271C4E8, sub_826ED530, sub_82394708,
  sub_822E8968, sub_826B2008, sub_826BFA48, sub_8266DCB8 and more); reached only in a driven race.
- 3 took another path: sub_8227CA08 (the job pop: the copy reached its call out, the original not), sub_8230C2D0
  (the audio graph: the other way round), sub_826C9900; the first two read state other threads change (job counters,
  the mixer's buffers), so a race between the guard's two runs is likely but not shown. To look at: candidates for
  `# out:` lines at the next copy refresh.
