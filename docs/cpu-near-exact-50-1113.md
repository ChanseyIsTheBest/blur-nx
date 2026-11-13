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
| 16 | Skip the zero-fill before a full overwrite | (a subagent reads the kernels) | pending |
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
| 49 | Skip resampling at ratio ~1 | (a subagent reads the kernels) | pending |

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
