# CPU cores, threads and functions, with audio in detail (2026-11-16)

The user: "could you give me a breakdown please of CPU cores and threads and functions especially focusing on audio
functions".

**Source:** the latest full race profile, the nx build's race abN5_1 (2026-11-13 19:44). Barcelona Oval, handheld,
CPU 1020 MHz, 4 cores. Reports 9-20 (120 s of racing), 27.8 fps by the wall clock (35.9 ms a frame). The shipping
c2b build is about 5 % slower, with the same threads.

**Method:**

- The profiler samples every thread each millisecond. Each sample's address maps to a function in the build's ELF
  (the scratchpad's audio_split.py, from cpu_table2.py).
- **Audio** means three things:
  - guest code in the game's audio engine, 0x822B0000-0x82320000 (the node-graph mixer that tools/stereo_mix.py maps);
  - the host's audio code (XMA, ffmpeg, the audio driver, the stereo fold);
  - the memsets and library calls on the audio-only threads.

**ms a frame:** CPU time a frame, all threads together. One core gives 35.9 ms a frame at this frame rate.

## Cores

117 ms of CPU a frame against 144 available (4 x 35.9): 81 % busy. The profiler measures only core 3's idle time on
its own, so cores 0-2 are an average.

| Core | Busy | Pinned there |
|---|---|---|
| 0 | ~81 % | nothing: the game's threads float over all four cores |
| 1 | ~81 % | Renut's render thread (47 % of the core) |
| 2 | ~81 % | a tiny host helper (data-cache cleans) |
| 3 | 84 % | XMA Decoder 9 %, Audio Worker 1 %, audio out 1 %; game threads fill the rest |

## Threads

| Thread | Core | ms a frame | Waiting | What it does | Audio ms |
|---|---|---|---|---|---|
| Game Master | any | 21.2 | 38 % | the game's main loop; waits on its jobs | 0.4 |
| JQ Worker 4 | any | 19.4 | 40 % | job worker: ~45 % audio jobs, the rest frame jobs (physics, Renut's NewOp) | 8.8 |
| Render thread (Renut) | 1 | 16.7 | 58 % | the game's GPU commands to deko3d (FastDrawLocked, Replay, FillTextures) | 0 |
| JQ Worker 3 | any | 16.7 | 51 % | job worker: nearly all audio graph jobs | 16.2 |
| JQ Worker 0 | any | 13.8 | 55 % | frame jobs (physics sub_8250A7E8 and more), Renut's NewOp | 0 |
| JQ Worker 1 | any | 11.4 | 60 % | frame jobs, NewOp, LZX decompression (DecodeBuffer) | 0 |
| JQ Worker 2 | any | 8.8 | 68 % | frame jobs, NewOp, LZX decompression | 0 |
| XThread CEBE65A0 | any | 3.2 | 91 % | the game's sound update (sub_822D2378) | 3.2 |
| XMA Decoder | 3 | 3.2 | 90 % | software XMA decoding (ffmpeg) and the frame cache | 3.2 |
| XThread CB3C68E0 | any | 0.9 | 98 % | the XDK's audio library side (reads XMA registers) | 0.9 |
| Audio Worker | 3 | 0.5 | 98 % | calls the game's render-driver callback each audio frame | 0.5 |
| Audio out (host) | 3 | 0.3 | 100 % | hands the mixed PCM to the Switch (audout) | 0.3 |
| main (UI) | 0-2 | 0.3 | 99 % | the ImGui fps overlay | 0 |
| The other 20 threads | - | ~0.8 | - | an XNotify thread, a sleeper, TimerQueue, the cache helper, loaders, the log writer | ~0 |

**The frame chain:** the Game Master 21.2, JQ0-2 34.0, JQ4's frame half ~10.6 and the render thread 16.7, about 83 ms.
**Audio:** about 33 ms.

## All the CPU by kind

| Kind | ms a frame | Share |
|---|---|---|
| Game code, not audio (machine copies mostly) | 55.6 | 48 % |
| Audio engine (game code) | 27.1 | 23 % |
| Renut renderer (host) | 20.3 | 17 % |
| Locks, kernel, libc | 4.5 | 4 % |
| memcpy / memset | 4.0 | 3 % |
| Audio (host: XMA, ffmpeg, driver) | 2.9 | 2 % |
| XDK / CRT library (game code) | 1.5 | 1 % |
| LZX decompression (host) | 1.1 | 1 % |

## Audio

**Total ~33 ms a frame (28 % of all CPU).** Audio runs on its own clock (48 kHz), so it is a fixed ~0.92 s of CPU
every second, nearly one of the four cores. Its share of a frame shrinks as the frame rate rises.

### The pipeline, in order

1. **Sound update** (XThread CEBE65A0, 3.2 ms): the game decides which sounds play and updates their voices and
   positions. sub_822D2378 1.07, sub_822CBAD0 0.63 and sub_823016D0 0.42, both pointer-list walks.
2. **Audio Worker** (host, core 3, 0.5 ms): every audio frame (two back to back with M-43) it calls the game's
   callback sub_829996E0. The callback kicks the engine, waits for the frame and submits it
   (XAudioSubmitRenderDriverFrame).
3. **The mix jobs** (JQ3 and JQ4, ~25 ms): the engine walks its node graph. Each node does its work in turn: voices,
   resampling, filters, reverb. Then the node loop mixes every child into its parent through a gain matrix. The jobs
   also kick XMA decodes (XmaDecoder::WriteRegister, 0.22).
4. **XMA decoding** (host, core 3, 3.2 ms): compressed sounds decoded by ffmpeg, or answered from the frame cache
   (most of the memcpy).
5. **Audio out** (host, core 3, 0.3 ms): the master ring is folded to stereo and handed to audout
   (SwitchAudioSystem::FillAndAppend, MixFrameInto).

### Audio by thread

| Thread | Audio ms | Of which |
|---|---|---|
| JQ Worker 3 | 16.2 | engine 15.0, memset 0.5, library 0.4, host 0.3 |
| JQ Worker 4 | 8.8 | engine 8.4, memset ~0.4 |
| XThread CEBE65A0 | 3.2 | the sound update |
| XMA Decoder | 3.2 | XmaContext 1.1, ffmpeg 0.8, memcpy 0.56, locks and malloc 0.4, the thread loop 0.2, memset 0.1 |
| XThread CB3C68E0 | 0.9 | XDK library 0.5, engine 0.3 |
| Audio Worker | 0.5 | the callback 0.14, waits and locks |
| Game Master | 0.4 | calls into the sound engine |
| Audio out | 0.3 | fold and audout |

### The audio functions, biggest first

148 audio functions were sampled, 29.4 ms a frame among them (the memsets and library calls come on top). Roles come
from the stereo mapping, the moonshots audit and the near-exact census. "Not mapped" means no one has read it yet.

| Function | ms | Threads | What it does |
|---|---|---|---|
| sub_8230AD50 | 5.01 | JQ3 64 %, JQ4 35 % | the node loop: mixes each child's channels into its output rows through the gain matrix (the stereo fold too); the biggest single function in the game |
| sub_822BEDD0 | 2.32 | JQ3 62 %, JQ4 37 % | the filter / effect stage: a 26-way switch over filter types |
| sub_822E17C8 | 2.19 | JQ3 58 %, JQ4 41 % | a per-node step that clears its block first: the game's biggest memset caller (~0.85 ms more in memset) |
| sub_8230EDE0 | 2.06 | JQ3 57 %, JQ4 42 % | the resampler (rate / pitch conversion) |
| sub_822E98B0 | 1.22 | JQ3 77 %, JQ4 22 % | the convolution reverb (FFT-partitioned; it runs on silence too) |
| sub_822D2378 | 0.98 | XThread 100 % | the sound update |
| sub_8230B778 | 0.96 | JQ3 66 %, JQ4 33 % | node dispatch (with 8230AAD0 and 822EB500, the graph walk) |
| sub_8230AAD0 | 0.74 | JQ3 67 %, JQ4 32 % | the recursive graph walk: a switch on the node type |
| sub_8230CBB8 | 0.67 | JQ3 52 %, JQ4 47 % | the mix-row kernel (a hand-written native) |
| sub_82306F88 | 0.59 | JQ3 60 %, JQ4 39 % | vector math for the graph (called virtually) |
| sub_822CBAD0 | 0.58 | XThread 100 % | a pointer-list walk in the sound update |
| sub_822CD2B0 | 0.57 | JQ3 55 %, JQ4 44 % | a small node accessor (38 instructions; the first touch of a node's state) |
| sub_82302700 | 0.55 | JQ3 45 %, JQ4 27 %, XThread 27 % | field loads, then virtual calls |
| sub_822D9640 | 0.53 | JQ3 52 %, JQ4 47 % | a 4-instruction helper called very often |
| sub_822EB500 | 0.52 | JQ3 69 %, JQ4 30 % | node processing (one of the three hot node callers) |
| XmaContext::Decode | 0.52 | XMA 100 % | the decode loop |
| decode_frame (ffmpeg) | 0.46 | XMA 100 % | the WMA Pro / XMA frame decoder |
| sub_82302950 | 0.45 | JQ3 43 %, JQ4 39 %, XThread 16 % | not mapped |
| sub_823016D0 | 0.38 | XThread 100 % | a pointer-list walk in the sound update |
| sub_8230DB80 | 0.37 | JQ3 60 %, JQ4 40 % | not mapped (20 direct callees) |
| sub_822CBB80 | 0.37 | JQ3 65 %, JQ4 35 % | not mapped (makes a virtual call) |
| sub_8230E9B0 | 0.34 | JQ3 58 %, JQ4 41 % | not mapped (the first touch of a node's state) |
| sub_822E8968 | 0.34 | JQ3 80 %, JQ4 19 % | not mapped |
| XmaContext::Work | 0.30 | XMA 100 % | one context's turn |
| XmaDecoder::WriteRegister | 0.22 | JQ3 58 %, JQ4 37 % | the game kicking an XMA decode |
| XmaContext::DecodeFrame | 0.21 | XMA 100 % | one frame through ffmpeg |
| XmaDecoder thread loop | 0.20 | XMA 100 % | the decoder thread's wait and dispatch |
| sub_829996E0 | 0.18 | Audio Worker | the game's render-driver callback |
| BlurStereoFoldBits | 0.14 | JQ3 93 % | our stereo fold of the gain rows |
| ff_imdct_half_neon, fft* | ~0.2 | XMA 100 % | ffmpeg's transforms |
| FillAndAppend + MixFrameInto | 0.23 | audio out | the 5.1 ring to stereo PCM for audout |

### Correction

docs/cpu-near-exact-50-1113.md counted "audio (JQ Workers 3 and 4) 36 ms" for this race. JQ4 is only ~45 % audio,
because it runs frame jobs too (sub_8250A7E8, NewOp, sub_825087A0 and more). So audio on the two workers is ~25 ms,
and ~33 ms counting every thread. The doc now says so.

### Audio ideas already on the lists

- **#116** (mix at 24 kHz) and **#117** (the graph on the GPU): docs/cpu-big-ideas-30-1114.md.
- **M-14** (filters in vector lanes, a census of 822BEDD0's entries first).
- **#22** (reverb tails: 822E98B0 runs on silence).
- **#59** (audio on its own core): raced, and its 'cores2' variant was slower.
