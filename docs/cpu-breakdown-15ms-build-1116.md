# CPU breakdown: the 15 ms build against c2b (console, 2026-11-16)

The user (2026-11-16, after the fourth race): "I want a CPU breakdown thread, cores, functions".

**The builds.** *New*: the 15 ms batch (the 7 audio natives, the resampler's two exact kernels, memset / memcpy natives,
the flags, the 360-precision list, the exact ideas; CG-25 off) + the ultimate base's ports 107-147 (speed-ups on), nx's
near-exact options, PGO from a fresh training race. *c2b*: the shipped NRO (2026-11-13), its own toml.

**The races.** Stock clocks (CPU 1,020 MHz, GPU 460.8, memory 1,600; the console was found overclocked first, so the
earlier c2b race does not count), the usual measured race (launch, menus, 240 s with ZR held), in the order c2b, new,
new, c2b. Tables from the profiler's reports 9-20 (the racing stretch), scratchpad cpu_table2.py / breakdown_compare.py.

## Frame rate

| Race | fps (first 12 racing lines) | GPU ms a frame |
|---|---|---|
| c2b | 25.61 | 27.63 |
| new | 26.94 | 27.63 |
| new | 27.30 | 27.54 |
| c2b | 25.34 | 27.63 |

**New 27.12 against c2b 25.48: +6.5 %.** The GPU time is the same, so the gain is the CPU's. In the profiler's window
(reports 9-20) both run ~28.4 fps, and there the new build needs **334-339 % of the four cores against 355-358 %:
the same frames for ~6 % less CPU** (117 against 127 ms of CPU a frame).

## Cores (new build, race 2)

| Core | Busy | Pinned there | ms of CPU a frame |
|---|---|---|---|
| 0-2 | 80 % (c2b 87 %) | the game's threads float over all four; Renut's render thread on core 1 (47 %) | 28.2 each |
| 3 | 93 % (c2b 94 %) | the XMA Decoder (7 %, c2b 10 %), the Audio Worker | 32.7 |

## Threads (ms of CPU a second, averaged over each build's two races)

| Thread | New | c2b | Change |
|---|---|---|---|
| Game Master | 628 | 651 | -23 |
| JQ Worker 4 (audio + frame work) | 569 | 613 | -44 |
| JQ Worker 3 (audio) | 488 | 532 | -45 |
| Renut render thread (core 1) | 468 | 466 | +2 |
| JQ Worker 0 | 398 | 418 | -20 |
| JQ Worker 1 | 311 | 332 | -21 |
| JQ Worker 2 | 242 | 260 | -18 |
| XThread (the sound update) | 134 | 140 | -6 |
| XMA Decoder | 71 | 95 | -24 |
| Audio Worker, timer, UI, others | ~54 | ~54 | 0 |

By form: game code in machine copies 2,055 against 2,221 ms a second (**-166**), hand natives 405 against 425 (-20),
host code 857 against 867 (-10). **About -200 ms of CPU a second, ~7 ms a frame at 28 fps**, every game thread lighter,
the render thread unchanged.

## Functions (new build, race 2; ms a frame at 28.5 fps)

| # | Function | Form | ms | Threads | What |
|---|---|---|---|---|---|
| 1 | sub_8230AD50 | hand native | 5.78 | JQ3 69 %, JQ4 30 % | the audio node loop (now with the mixing kernels fused in) |
| 2 | memcpy | host | 3.26 | render 48 %, XMA 18 % | host copies (Renut, XMA) |
| 3 | sub_822E17C8 | hand native | 3.12 | JQ3 55 %, JQ4 44 % | the audio voice step |
| 4 | sub_8250A7E8 | machine copy | 2.52 | JQ0-2, JQ4 | game logic |
| 5 | sub_822BEDD0 | hand native | 2.42 | JQ3 67 % | the audio effect stage |
| 6 | sub_8251F778 | machine copy | 2.33 | JQ0, JQ4 | game logic |
| 7 | renut FastDrawLocked | host | 2.19 | render | draws |
| 8 | sub_824F35D8 | machine copy | 2.10 | JQ0, JQ4 | game logic |
| 9 | renut NewOp | host | 2.10 | JQ1-2 | the render op recorder |
| 10 | sub_8230EDE0 | machine copy | 1.73 | JQ3/4 | the resampler (its filter and converter natives inlined: c2b 2.14) |
| 11 | sub_8252E168 | machine copy | 1.68 | JQ0-2 | game logic |
| 12 | sub_82668520 | machine copy | 1.54 | JQ0-1 | game logic |
| 13 | sub_822E98B0 | hand native | 1.54 | JQ3 87 % | the convolution reverb |
| 14 | memset | host | 1.49 | JQ3/4 | the audio rows' clears (host memset of the natives) |
| 15 | renut Replay | host | 1.45 | render | command replay |
| 16 | sub_826EFA20 | machine copy | 1.31 | JQ0-1 | game logic |
| 17 | sub_822D2378 | hand native | 1.26 | XThread | the sound update |
| 18-25 | sub_825123B8, 8251FE38, 829D0300, 82504F90, 825087A0, 822CD2B0, 82432300 | machine copies | 1.07-1.21 each | JQ0-2, JQ3/4 (822CD2B0) | game logic, a node accessor |

The new exact natives (the resampler's filter and converter, memset, memcpy) have no line of their own: they are
inlined into their callers, whose time shows them.

**What moved, per function (ms a second, new / c2b).** The largest: the resampler 36 / 61, Renut's NewOp 51 / 106 and
FastDrawLocked 15 / 71 (port 145's stream redeem and the base's renderer work, with NoteSetWordsOf 16 / 0 new),
sub_829D0300 35 / 53, sub_8250A7E8 63 / 77, the XMA decoder's decode 12 / 17 and ffmpeg 6 / 12. Some lines are
bookkeeping, not cost: the output node 8230B778 (0 / 32) and the ramp kernel 8230CBB8 (0 / 21) are now inside the node
loop's native, and sub_8251E6F0 (0 / 56) and sub_8251F778 (75 / 39) trade samples (one now inlined in the other's copy).
Two natives look heavier per second: the voice step 822E17C8 (83 / 67) and the sound update 822D2378 (41 / 34);
attribution may explain part of it (members now counted inside them, such as 822E6E98, 0 / 8), and the console A/B
with the audio natives off, running now, measures them directly.

## Notes

- The new toml samples the profile every 5 ms (switch_profile_stacks_ms 5), c2b's every 1 ms: the new build's tables
  have fewer samples (noisier per function, the totals unaffected), and c2b pays its sampler's extra pauses.
- The first-12 fps and the profiler window differ in what they cover (the same stretch of every race vs reports 9-20);
  both show the new build ahead: +6.5 % fps, or the same fps for 6 % less CPU.
- Next: the A/B races of the batch's and the base's new items (renut_15ms_audio_natives_off and the others), each
  between control races of the new build; docs/cpu-15ms-batch-1116.md has the list.
