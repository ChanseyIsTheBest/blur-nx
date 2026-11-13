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
| 1 | No signalling-NaN quieting on float stores | shared float census build | planned |
| 2 | Float-to-int without the NaN fix-ups | shared float census build | planned |
| 3 | Round-to-nearest only | the existing kCensusRound* census | planned |
| 4 | No FPU status flags | scan for the flag reads (mffs), then a counter | planned |
| 5 | No integer-overflow summary bit | scan for the overflow-recording forms | planned |
| 8 | Link-register elision (`SASRT_HOT_LR_ELIDE`) | scan for the return address used as data, then the guard | planned |
| 9 | Stack pointer preserved everywhere | scan for stack-pointer writes that aren't frame setup | planned |
| 12 | Hash-only caches (128-bit) | count collisions with the compare kept (debug) | planned |
| 13 | Smaller Begin*Vertices device copies | the snapshot-deltas verify mode | planned |
| 15 | Trust dirty-page marks over byte compares (Renut) | count compares that disagree with the marks | planned |
| 16 | Skip the zero-fill before a full overwrite (audio kernels) | prove the overwrite, compare the audio dumps | planned |
| 17 | Drop the division checks | counter of divisions by zero | planned |
| 19 | Remove trap asserts in hot code | scan for the traps | planned |
| 20 | Unguarded promoted calls | the promoted-call census | planned |
| 23 | Near-unity gains use the unity kernel (audio) | census of gain values | planned |
| 24 | Tiny ramps treated as constant (audio) | census of ramp sizes | planned |
| 26 | Spin briefly, then yield | none (timing only) | planned |
| 27 | No-op debug output and telemetry formatting | count the calls and their time | planned |
| 29 | Cheaper performance-counter reads | none | planned |
| 30 | Quiet the remaining host timers | list the timers and their wake-ups | planned |
| 34 | Waiting threads sleep until a flag changes instead of yield-spinning | yield counts per thread and site | planned |
| 35 | Higher priority for the frame-chain threads | none (timing only) | planned |
| 38 | Batch dirty-page marks per block | the store-cards verify mode | planned |
| 40 | Drop stack-cookie checks and stack probes in hot code | scan for the cookie checks | planned |
| 41 | Float compares fused into Arm branches | shared float census build | planned |
| 42 | Vector min, max and compares without NaN / -0 fix-ups | shared float census build | planned |
| 48 | Skip draws that can't change anything | count masked draws in Renut, frame hashes | planned |
| 49 | Skip resampling at a ratio of ~1 (audio) | counter of rate ratios | planned |

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

(filled in as they come)

## Log

- 2026-11-13 13:55: the user's choice; this doc.
