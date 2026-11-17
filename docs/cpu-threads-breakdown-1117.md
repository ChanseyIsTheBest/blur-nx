# CPU cores, threads and functions on the shipping build (2026-11-17)

The user: "could you give me a cpu core thread function breakdown please".

## Source

- **Profile:** the console race 1117d_final_ctl (2026-11-17 20:31), the shipping build 2026-11-17_b3menu3 (main 7fc2a973:
  batch 3, the G1 fix, the menu backdrop fixes), the control toml (the sampler on, every 5 ms).
- **Race:** Barcelona Oval, handheld, 4 cores.
- **Window:** reports 9-20, 120 s of racing.
- **Speed:** 31.27 fps by the wall clock, 32.0 ms a frame.
- **Method:** as docs/cpu-threads-audio-breakdown-1116.md. Every sample's address is mapped to a function in the
  build's ELF (the scratchpad's cpu_table2.py), and each guest function is classed by the form it ran in: hand native,
  machine copy, or not optimised. Host code is Renut, the SDK, libnx and the C library.
- **ms a frame:** CPU time a frame. One core gives 32.0 ms a frame at this frame rate.

## Cores

104 ms of CPU a frame against 128 available (4 x 32.0): 81 % busy. The 11-16 race was 117 ms at 27.8 fps, 81 % busy
too. The profiler measures only core 3's idle time on its own, so cores 0-2 are an average.

| Core | Busy | Pinned there | ms a frame |
|---|---|---|---|
| 0 | ~80 % | nothing: the game threads float over all four cores | 25.5 |
| 1 | ~80 % | Renut's render thread (51 % of the core) | 25.5 |
| 2 | ~80 % | a small host helper (1 %) | 25.5 |
| 3 | 87 % | the XMA Decoder (7 %), the Audio Worker (1 %); the game threads also run here (banjo_system_core 2) | 27.8 |

## Threads

"Waiting" is the share of its samples a thread spent blocked in the kernel. The four columns after it split its busy
samples by the form of the code.

| Thread | CPU % of a core | ms a frame | Waiting | Hand native | Machine copy | Not optimised | Host code |
|---|---|---|---|---|---|---|---|
| Game Master | 61.4 | 19.6 | 35 % | 0 % | 87 % | 2 % | 11 % |
| JQ Worker 4 | 57.4 | 18.4 | 34 % | 24 % | 63 % | 1 % | 12 % |
| Renut render thread (core 1) | 51.4 | 16.4 | 51 % | | | | 100 % |
| JQ Worker 3 | 43.1 | 13.8 | 54 % | 47 % | 42 % | 0 % | 11 % |
| JQ Worker 0 | 37.6 | 12.0 | 57 % | 0 % | 87 % | 2 % | 11 % |
| JQ Worker 1 | 28.9 | 9.2 | 64 % | 0 % | 84 % | 2 % | 13 % |
| JQ Worker 2 | 21.7 | 6.9 | 75 % | 1 % | 80 % | 3 % | 16 % |
| XThread (the sound update) | 12.9 | 4.1 | 98 % | 41 % | 40 % | 9 % | 9 % |
| XMA Decoder (core 3) | 6.9 | 2.2 | 92 % | | | | 100 % |
| Audio Worker (core 3) | 1.3 | 0.4 | 98 % | | 18 % | | 82 % |
| TimerQueue, main (UI), two host helpers | 3.4 together | 1.0 | 99-100 % | | | | 100 % |

**Who does what:**
- **Game Master** is the game loop.
- **JQ Workers 0-2** run the game's jobs: physics, AI, animation, the scene.
- **JQ Workers 3 and 4** run the audio engine's mixer jobs besides other jobs: most of their hand-native time is the
  mixer.
- **The render thread** turns the recorded Direct3D calls into deko3d commands.

## By form of code

| Form | ms a frame | Share |
|---|---|---|
| Machine copies (localized guest functions) | 61.9 | 59 % |
| Host code (Renut, the SDK, libnx, libc) | 27.9 | 27 % |
| Hand natives (the audio mixer, the sound update, libc natives) | 13.0 | 12 % |
| Not optimised (plain generated code) | 1.6 | 2 % (19 functions sampled) |

## The top functions

| # | Function | Form | ms a frame | Main threads | What it is |
|---|---|---|---|---|---|
| 1 | sub_8230AD50 | hand native | 5.02 | JQ3 64 %, JQ4 35 % | the audio mixer's node loop (each child's channels into its output rows through the gain matrix, the stereo fold) |
| 2 | memcpy | host | 3.52 | render thread 41 %, XMA Decoder 14 % | copies |
| 3 | sub_822E17C8 | hand native | 2.55 | JQ4 53 %, JQ3 46 % | the mixer's per-node step (clears its block first) |
| 4 | sub_8251F778 | machine copy | 2.31 | JQ1 28 %, JQ2 21 % | a game job |
| 5 | Renut FastDrawLocked | host | 2.27 | render thread | the draw path |
| 6 | sub_822BEDD0 | hand native | 2.19 | JQ4 55 %, JQ3 44 % | the audio filter / effect stage (a 26-way switch over filter types) |
| 7 | sub_824F35D8 | machine copy | 2.11 | JQ0 46 %, JQ1 21 % | a game job |
| 8 | memset | host | 1.74 | render thread 34 %, JQ3 34 % | clears |
| 9 | Renut NewOp | host | 1.74 | JQ1 34 %, JQ2 27 % | recording a Direct3D call for the render thread |
| 10 | sub_8250A7E8 | machine copy | 1.62 | JQ0 27 %, JQ1 27 % | a game job |
| 11 | sub_8230EDE0 | machine copy | 1.54 | JQ3 55 %, JQ4 44 % | the audio resampler |
| 12 | sub_822D2378 | hand native | 1.50 | XThread | the sound update |
| 13 | Renut Replay | host | 1.34 | render thread | replaying recorded calls |
| 14 | sub_825123B8 | machine copy | 1.30 | JQ0 34 %, JQ1 28 % | a game job |
| 15 | sub_822E98B0 | hand native | 1.26 | JQ3 77 %, JQ4 22 % | the convolution reverb |

Audio altogether is about 16.7 ms a frame: the six mixer and sound-update functions above (14.1 ms), the XMA Decoder
(2.2) and the Audio Worker (0.4). That is 16 % of the CPU.

## What this means

- **Headroom:** the frame is CPU-bound, but with headroom. 81 % busy, with every game thread waiting 34-75 % of the
  time: the frame follows the chain Game Master -> jobs -> the render thread, not the cores' total.
- **Effect of the 30 fps cap:** it leaves the CPU about 4 % more idle (31.3 -> 30 fps). The GPU (28.6 ms a frame)
  stays under the cap's 33.3 ms.
