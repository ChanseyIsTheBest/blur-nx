# CPU cores, threads and functions on the shipping build, 30 fps cap (2026-11-18)

The user (08:10): "can I have a cpu core, thread, function breakdown please".

## Source

- **Build:** 2026-11-18_b3kick (main 678b5420: the map-load crash fix), the card's NRO.
- **Toml:** the shipping renut.toml (the 30 fps cap, diagnostics off), with one change: `switch_profile_stacks = true`
  (the SDK's sampler, every 5 ms). The user's shader cache and pipeline list were on the card.
- **Race:** Barcelona Oval, handheld, 4 cores, the same menus and 240 s drive as 11-17's profile (one_race.sh).
- **Window:** profiler reports 9-20, 120 s of racing.
- **Method:** as docs/cpu-threads-breakdown-1117.md (the scratchpad's cpu_table2.py), with the hand-native list
  regenerated from main's app/src/cpu/hot/*.h (39 functions; the old list had 22).
- **Comparison:** 11-17's profile (1117d_final_ctl, uncapped) run again through the same script and list.
- **ms a frame:** CPU time a frame at the profiler's frame rate (30.57 fps, 32.7 ms a frame).

## Speed

- **The race:** 29.14 fps by Renut's wall clock over the same stretch. 11-17's shipping race, uncapped, ran 31.25.
- **The cap's effect:** in each 10 s, 288 to 300 of the display's 300 slots at 30 Hz got a new image. A frame that
  misses its 33.3 ms slot shows the previous image for one more refresh. This is the first measured race with the cap:
  on 11-17 it was checked in the menus only (30.00 fps).
- **The GPU:** 28.5 ms a frame on average, the closest of anything to the 33.3 ms slot.

## Cores

106 ms of CPU a frame against 131 available: 325 % of 400 (81 %). On 11-17, uncapped: 346 % (86 %).

| Core | Busy | 11-17 | Pinned there | ms a frame |
|---|---|---|---|---|
| 0 | 79 % | 84 % | nothing: the game threads float over all four cores | 25.8 |
| 1 | 79 % | 84 % | Renut's render thread (50 % of the core) | 25.8 |
| 2 | 79 % | 84 % | a small host helper (1 %) | 25.8 |
| 3 | 88 % | 92 % | the XMA Decoder (7 %), the Audio Worker (1 %), a host helper (1 %) | 28.7 |

The profiler measures only core 3's idle time on its own, so cores 0-2 are an average.

## Threads

| Thread | CPU % of a core | 11-17 | ms a frame | 11-17 | Waiting | Hand native | Machine copy | Not optimised | Host |
|---|---|---|---|---|---|---|---|---|---|
| Game Master | 60.3 | 65.0 | 19.7 | 19.6 | 40 % (35) | 0 % | 86 % | 3 % | 11 % |
| JQ Worker 4 | 57.0 | 60.8 | 18.7 | 18.4 | 43 % (34) | 27 % | 61 % | 1 % | 12 % |
| Renut render thread (core 1) | 49.7 | 54.4 | 16.2 | 16.4 | 50 % (51) | | | | 100 % |
| JQ Worker 3 | 43.0 | 45.6 | 14.1 | 13.8 | 56 % (54) | 46 % | 42 % | 1 % | 11 % |
| JQ Worker 0 | 37.6 | 39.8 | 12.3 | 12.0 | 57 % (57) | 0 % | 90 % | 1 % | 9 % |
| JQ Worker 1 | 29.5 | 30.6 | 9.6 | 9.2 | 68 % (64) | 1 % | 87 % | 1 % | 10 % |
| JQ Worker 2 | 21.7 | 22.9 | 7.1 | 6.9 | 78 % (75) | 1 % | 83 % | 1 % | 15 % |
| XThread (the sound update) | 13.1 | 13.7 | 4.3 | 4.1 | 98 % (98) | 50 % | 36 % | 9 % | 6 % |
| XMA Decoder (core 3) | 7.3 | 7.3 | 2.4 | 2.2 | 90 % (92) | | | | 100 % |
| Audio Worker (core 3) | 1.3 | 1.4 | 0.4 | 0.4 | 98 % (98) | | 45 % | | 55 % |
| TimerQueue, main (UI), two host helpers | 3.5 together | 3.7 | 1.1 | 1.0 | 99-100 % | | | | 100 % |

## By form of code

| Form | ms a frame | Share | 11-17 |
|---|---|---|---|
| Machine copies | 61.8 | 58 % | 61.3 |
| Host code (Renut, the SDK, libnx, libc) | 29.4 | 28 % | 27.9 |
| Hand natives | 13.5 | 13 % | 13.6 |
| Not optimised (15 functions sampled) | 1.5 | 1 % | 1.6 |

## The top functions

| # | Function | Form | ms a frame | 11-17 | Main threads | What it is |
|---|---|---|---|---|---|---|
| 1 | sub_8230AD50 | hand native | 4.77 | 5.02 | JQ3 58 %, JQ4 41 % | the audio mixer's node loop |
| 2 | memcpy | host | 3.81 | 3.52 | render thread 31 %, XMA Decoder 19 % | copies |
| 3 | Renut FastDrawLocked | host | 3.42 | 2.27 | render thread | the draw path |
| 4 | sub_822E17C8 | hand native | 2.45 | 2.55 | JQ4 55 %, JQ3 44 % | the mixer's per-node step |
| 5 | sub_8251F778 | machine copy | 2.28 | 2.31 | JQ0 26 %, JQ1 23 % | a game job |
| 6 | sub_822BEDD0 | hand native | 2.23 | 2.19 | JQ3 54 %, JQ4 45 % | the audio filter / effect stage |
| 7 | Renut NewOp | host | 1.97 | 1.74 | JQ0 37 %, JQ4 20 % | recording a Direct3D call |
| 8 | Renut Replay | host | 1.93 | 1.34 | render thread | replaying recorded calls |
| 9 | sub_824F35D8 | machine copy | 1.93 | 2.11 | JQ0 45 %, JQ1 25 % | a game job |
| 10 | sub_822D2378 | hand native | 1.93 | 1.50 | XThread | the sound update |
| 11 | sub_8230EDE0 | machine copy | 1.66 | 1.54 | JQ4 63 %, JQ3 36 % | the audio resampler |
| 12 | memset | host | 1.62 | 1.74 | JQ3 48 %, JQ4 29 % | clears |
| 13 | sub_8250A7E8 | machine copy | 1.45 | 1.62 | JQ0 39 %, JQ1 33 % | a game job |
| 14 | sub_826EFA20 | machine copy | 1.31 | 0.77 | JQ0 40 %, Game Master 16 % | a game job |
| 15 | sub_82432300 | machine copy | 1.23 | 1.05 | JQ2 28 %, JQ0 25 % | a game job |

Audio altogether: about 17.0 ms a frame (16 % of the CPU). That is the six mixer and sound-update functions (14.2 ms),
the XMA Decoder (2.4) and the Audio Worker (0.4).

## What changed since 11-17

- **Work a frame:** the same. Every thread is within 0.4 ms a frame of 11-17, and all CPU is 106.2 against 104.4 ms a
  frame. The code changes since then (the crash fix, the Swap frame count, the code-load prefetch gap) add nothing
  measurable here.
- **CPU a second:** lower (325 % against 346 %), because the cap holds the frame rate. Game Master and JQ Worker 4
  now wait 40 % and 43 % of the time (35 % and 34 % before): they wait for the cap's refresh.
- **The render thread:** its total is the same (16.2 against 16.4 ms). Its time is split differently: FastDrawLocked
  2.27 -> 3.42, Replay 1.34 -> 1.93, its memcpy 1.7 -> 1.2. The single-function figures there moved, not its work.

Files: the profile and logs in out/console_runs/1118_ship_sampler (cpu_table.md is the script's full output),
screens in C:/blur-nx-test/console_runs/1118_ship_sampler.
