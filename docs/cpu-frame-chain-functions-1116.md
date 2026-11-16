# The frame chain by function: Game Master and the frame jobs (console, 2026-11-16)

The user (2026-11-16): "can I have function break down of game master serial work and the frame jobs". From the 15 ms
build's two console races at stock clocks (1116_new_a, 1116_new_b), the profiler's racing reports 9-20 of each pooled
(24 reports, ~28.4 fps): each function's share of a thread's samples times that thread's measured CPU a frame
(scratchpad thread_top.py). The new toml samples every 5 ms, so one sample is ~0.02 ms a frame: rows under ~0.2 ms are
a handful of samples each; the totals are firm.

## Game Master (the serial part)

**22 ms of CPU a frame (63 % of a core; it waits the other 37 % on the jobs), 413 functions, none above 2 %.**

| Form | ms a frame |
|---|---|
| machine copies | 18.1 |
| host code (locks, malloc, Renut's D3D recording, memcpy) | 2.2 |
| not optimised (the generated original runs) | 0.7 |
| hand natives | 0.1 |

| # | Function | Form | ms a frame | % of the thread |
|---|---|---|---|---|
| 1 | sub_826A7858 | machine copy | 0.41 | 1.9 |
| 2 | sub_827E40D0 | machine copy | 0.41 | 1.9 |
| 3 | sub_8248DBB0 | machine copy | 0.34 | 1.5 |
| 4 | sub_82523550 | machine copy | 0.29 | 1.3 |
| 5 | sub_826EFA20 | machine copy | 0.27 | 1.2 |
| 6 | sub_822958B0 | machine copy | 0.27 | 1.2 |
| 7 | sub_821A0220 | machine copy | 0.27 | 1.2 |
| 8 | sub_8249CB30 | machine copy | 0.24 | 1.1 |
| 9 | sub_82242BC0 | **not optimised** | 0.24 | 1.1 |
| 10 | memcpy | host | 0.22 | 1.0 |
| 11-20 | sub_824F7288, 8251F778, 825123B8, 8227CA08, 826A5548, 822A8FE0, 8252D4B0, 82210998, 82504F90, 8250A7E8 | machine copies | 0.17-0.19 each | 0.8-0.9 each |
| 21-30 | mutexLock, sub_8261A4C8, renut CaptureDelta, sub_8227E098 (**not optimised**), sub_82563860, mutexUnlock, sub_821FD548, 8218EF40, 82575F88, 8252E168 | | 0.15-0.17 each | 0.7-0.8 each |
| | 383 other functions | | 15.6 | 71 |

**By address range** (the game's code is laid out by module): 0x826 3.5 ms, 0x825 3.2, 0x824 2.9, 0x822 2.6, 0x821 2.0,
0x827 1.9, 0x823 1.7, 0x828 0.9: the whole game, a little of everything a frame.

**Host code on Game Master** (2.2 ms): memcpy 0.22, mutexLock / mutexUnlock / pthread mutexes ~0.45 (the game's critical
sections), Renut's D3D calls ~0.7 (CaptureDelta, FlushDeferredVertices, the render-state and surface setters, QueueDraw),
malloc / free ~0.2.

**Not optimised** (0.7 ms): sub_82242BC0 0.24, sub_8227E098 0.17, sub_8235B400 0.07, then 13 more at ~0.02: functions
the copy tool left as generated code.

## The frame jobs (JQ Workers 0, 1, 2, and 4 without its audio)

**46.6 ms of CPU a frame over four threads, 289 functions; 40 % in the top 16.**

| Form | ms a frame |
|---|---|
| machine copies | 39.9 |
| host code (Renut's op recording, memcpy) | 5.8 |
| not optimised | 0.6 |
| hand natives | 0.3 |

| # | Function | Form | ms a frame | % of the jobs | Known from earlier analyses |
|---|---|---|---|---|---|
| 1 | sub_8251F778 | machine copy | 2.31 | 5.0 | now holds sub_8251E6F0 (a vector solver's step: dependent float chains, spills) |
| 2 | sub_8250A7E8 | machine copy | 1.93 | 4.1 | 982 instructions, 23 callees (eight of sub_8251F630, sines, sub_82521E68's angle vectors) |
| 3 | renut NewOp | host | 1.64 | 3.5 | Renut recording the game's draw calls |
| 4 | sub_824F35D8 | machine copy | 1.46 | 3.1 | each object against twelve planes (culling / collision): a dependent chain and object loads |
| 5 | sub_8252E168 | machine copy | 1.36 | 2.9 | |
| 6 | sub_829D0300 | machine copy | 1.16 | 2.5 | |
| 7 | sub_825123B8 | machine copy | 1.11 | 2.4 | |
| 8 | sub_82668520 | machine copy | 1.11 | 2.4 | |
| 9 | sub_8251FE38 | machine copy | 1.07 | 2.3 | |
| 10 | sub_82432300 | machine copy | 1.05 | 2.3 | |
| 11 | sub_82504F90 | machine copy | 1.04 | 2.2 | |
| 12 | sub_825087A0 | machine copy | 1.03 | 2.2 | 1,872 instructions, nine callees, flat |
| 13 | sub_826EFA20 | machine copy | 0.97 | 2.1 | |
| 14 | sub_826803B0 | machine copy | 0.82 | 1.8 | |
| 15 | sub_82662AC0 | machine copy | 0.72 | 1.5 | |
| 16 | sub_8250C1A8 | machine copy | 0.65 | 1.4 | 8-26 callees, flat |
| 17 | memcpy | host | 0.65 | 1.4 | |
| 18-20 | sub_826A7858, sub_826EFE18, sub_82525FA8 | machine copies | 0.53-0.64 each | | 82525FA8: callees and flush-mode switches |
| 21-30 | sub_82451530, renut NoteSetWordsOf, renut RecordExtents, renut FinishResources, sub_82455348, 8219CCE0, 8250BFD0, 82432A28, 826E72C0, 824EA1E0 | | 0.42-0.53 each | | |

The 0x8250-0x8252 cluster (8251F778, 8250A7E8, 8252E168, 825123B8, 8251FE38, 825087A0, 8250C1A8, 82525FA8 and their
callees: sines, linear solves, angle vectors) is the cars' simulation: ~11 ms of the jobs. Renut's recording of the
game's draws runs on these threads too: NewOp, NoteSetWordsOf, RecordExtents, FinishResources ~3.1 ms.

## What it says

- **Game Master has no hot spot.** 22 ms over 413 functions, the largest 0.4 ms: only broad gains move it (better
  copies everywhere, the generated code's forms, PGO), or the little items: its two not-optimised functions (0.4 ms
  together) and its locks and allocations (~0.6 ms).
- **The frame jobs are more concentrated**: the simulation cluster ~11 ms and the top 16 functions 40 %. Every one is a
  machine copy already; the earlier analyses left them as copies because they call other game functions and are flat
  inside. Hand natives can now call game functions (hand.txt calls=, made for the audio natives), which reopens them:
  8250A7E8 and 8251F778 (with 8251E6F0) first.
- Renut's own work on the game's threads is ~3.1 ms of the jobs and ~0.7 ms of Game Master.
