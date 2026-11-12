# Audit of the 50 CPU moonshots (M-01 .. M-50), 2026-11-13

An independent audit of `docs/cpu-moonshots-50-1113.md`. It is read-only: nothing was built, run or edited except this
file and the scratch scripts. I did not touch the console, Ryujinx or the game. I checked each claim against the
sources, the two console profiles the proposer used (CG-13: `out/console_runs/2026-11-12_cg13_2`, ELF
`C:/blur-nx-test/builds/2026-11-12_cg13_A`; batch 4: `2026-11-12_b4ref2`, ELF `2026-11-12_b4_A_xsw`), 15 more race
profiles of 2026-11-12/13, and Renut's 10 s logs.

My scripts were in the session's scratchpad (`audit50/`), not in the repository:
- `pace.py`: per run, CPU per real second and per frame for each thread group (real seconds = audout samples / 48 kHz,
  or client blocks / 187.5);
- `agg2.py`: like the proposer's `agg.py`, but each sample is weighted by its own thread's kernel tick count;
- `cmp.py`: two runs of one ELF, compared per function: ms a frame against ms a second;
- `top2.py`: top functions of an `agg2` pickle;
- `lrsite.py`: the exact call site of each memcpy/memset caller;
- `memset_sites.py`: which `bl memset` sites are card-range marks;
- pickles `cg13_2.pkl`, `b4ref2.pkl`, `b4nolock2.pkl`, `b4lock2.pkl`, `msaa2.pkl`, `cg13_1.pkl`.

All figures below are **ms of CPU a frame, at the CG-13 profile's conditions** (22.3 wall-clock fps, MSAA 1), unless a
line says otherwise. "a" means audio- or time-paced (the cost per second stays constant), "f" means frame-paced, and
"mixed" means both.

---

## 0. Summary

- **Counts.**
  - KEEP: 21.
  - REWORK: 3 (M-04, M-21, M-31).
  - CENSUS-FIRST: 19.
  - DROP: 7 (M-08, M-10, M-12, M-16, M-17, M-26, M-46).
- **Realistic total of everything KEEP/REWORK** (exact or host-only, the D items left out): about **6-8.5 ms a frame on
  paper. Expect 5-7 ms realised: +5-7 % fps** (§5).
  - At the shipped 4x MSAA every figure is about 10 % larger (§1.3).
  - 30 fps is not in reach of this list: it needs about 24 ms off the frame-paced work (§1.2).
- **Top 10 by realistic gain per effort:**

  | rank | ID | what | realistic ms | pacing |
  |---|---|---|---|---|
  | 1 | M-01 | XMA one-frame catch-up | 1.9-2.3 | a |
  | 2 | M-22 | natives' card marks inline | 0.4-0.7 | mostly f |
  | 3 | M-03 | XMA dead fills | 0.3-0.45 | a |
  | 4 | M-06 | XMA quantiser table (after M-01) | 0.25-0.35 | a |
  | 5 | M-19 | next-node prefetch in the audio graph walks | 0.5-1.0 | a |
  | 6 | M-05 | XMA codec reset instead of reopen | 0.2-0.3 | a |
  | 7 | M-04 (reworked) | FFmpeg frame-buffer churn + flat lookup | 0.3-0.6 | a |
  | 8 | M-31 (reworked) | Begin*Vertices draws into the delta chain | 0.4-0.7 | f (Game Master) |
  | 9 | M-28 | Windows-only mentions out of the copy rule | 0.1-0.2 | f |
  | 10 | M-25 | promoted targets inside the copies' closures | 0.2-0.5 | mixed |

  Next in line: M-23 (0.1-0.3), M-29 (LZX native, 0.5-0.9 but L effort), M-35, M-40.
- **D items that need the user's decision:**
  - M-49: mirror pass at half rate. Visible, 1.5-3 ms, the only big D item with a credible mechanism.
  - M-13: stereo speaker config. Most likely 0.
  - M-43: paired audio callbacks. +5.3 ms audio latency.
  - M-41: XMA kick coalescing.
  - M-42: job workers wait instead of yielding.
  - M-18: voice LOD.
  - M-11: XMA decode window.
  - M-36: bounded XMADisableContext wait. Timing only, as if the decoder were slower. The proposer called it H.
  - M-10: writing decoded game audio to the SD. Dropped anyway.
- **Where I disagree most with the proposer** (details in §6):
  1. The sampler's round-robin makes raw sample shares wrong per thread. The XMA Decoder is 11.1 ms, not 14.5, so
     every XMA figure was ×1.30 too high; the render thread is 16.4 ms, not 13.5.
  2. The "audio ms is worth 0.74" rule is right only for the whole distance to 30 fps. For ranking and for any gain of a
     few ms, audio and frame-paced savings buy the same fps (about 1 % per ms). This was measured: the XMA cache at
     256 MB gave +3.9 % for 4.3 ms of XMA.
  3. M-01 is underestimated in kind but not in size. 74-84 % of catch-up decodes come from runs cut at 256 frames, so
     M-01 removes about 93 % of them.
  4. M-04 counts the wrong costs. The XMA thread's mutex time is the per-context locks the game contends (M-36's), not
     the cache's. The cache's allocations stop once it is full (about 70 s into the race).
  5. M-13 rests on a misread. 0x00010001 fails the game's `== 1` test, and the 360's output is fixed at 6 channels.
  6. M-47's example store is `AV_WN32A` (a may_alias union), so strict aliasing cannot remove that reload.
  7. M-12 assumes idle time on core 3, which has 2.9 %.
  8. M-42's spin mostly burns idle time: its CPU is accounting, not throughput.

---

## 1. What the profiles say, checked

### 1.1 Per-thread weighting: the proposer's per-function ms are biased by thread

`switch_perf.cpp:1052-1117`: every 1 ms the sampler takes the next "busy" thread in slot order (round-robin). So every
busy thread gets the same number of samples, and a thread's PC samples measure the time it was *not inside a system
call*, not the time it ran.

The proposer's `agg.py` turns samples into ms with one global factor (0.0108 ms a sample). Weighting each sample by its
own thread's kernel tick count (`svcGetInfo` ThreadTickCount, the profiler's "exact CPU") gives the figures below. They
are the same in both runs:

| thread | samples, global factor (proposer) | kernel ticks (this audit) | factor |
|---|---|---|---|
| JQ Worker 4 | 24.8 | 25.7 | 1.04 |
| Game Master | 22.9 | 24.3 | 1.06 |
| JQ Worker 3 | 22.7 | 24.2 | 1.07 |
| Renut GPU thread (0x2C, core 1) | 13.5 | 16.4 | **1.21** |
| JQ Worker 0 | 16.0 | 15.2 | 0.95 |
| JQ Worker 1 | 13.4 | 12.1 | 0.90 |
| **XMA Decoder** | **14.5** | **11.1** | **0.77** |
| JQ Worker 2 | 11.6 | 9.2 | 0.79 |
| XThread | 5.9 | 6.3 | 1.08 |

Batch 4 (b4ref2) gives the same factors (XMA 0.77, render 1.21, JQ2 0.75).

The XMA thread is over-sampled: it is often outside a system call but not running, preempted on core 3. The render
thread is under-sampled: its ioctl time is CPU, but its PC sits after the `svc`.

So the XMA pile is 11.1 ms, not 14.5. For example, `decode_frame` is 3.22 ms, not 4.17; the ct table's per-thread line
already says 3.2. The figures below use tick weighting.

### 1.2 Is the audio work audio-paced? Yes. But that does not make it worth less today.

**Pacing (verified).** These runs used the same ELF (b4_A_xsw); only MSAA and the kick lock differ. Each cell is ms a
frame / cores busy per real second.

| run | fps | XMA | JQ3 | JQ4 | XThread | Game Master | JQ0-2 | render |
|---|---|---|---|---|---|---|---|---|
| b4x_lock2 (4x, locked kick) | 18.12 | 14.8 / 0.269 | 31.1 / 0.564 | 33.3 / 0.603 | 8.1 / 0.147 | 29.3 / 0.531 | 45.2 / 0.819 | 17.8 / 0.323 |
| b4x_nolock2 (4x) | 19.16 | 12.8 / 0.246 | 29.0 / 0.555 | 30.9 / 0.593 | 7.7 / 0.147 | 27.7 / 0.531 | 43.1 / 0.826 | 17.3 / 0.332 |
| msaa2 | 21.12 | 11.3 / 0.238 | 25.3 / 0.534 | 27.7 / 0.584 | 6.7 / 0.142 | 25.8 / 0.544 | 39.7 / 0.838 | 16.7 / 0.352 |
| b4ref2 (MSAA 1) | 21.22 | 11.2 / 0.239 | 25.8 / 0.547 | 27.6 / 0.586 | 6.6 / 0.140 | 25.1 / 0.533 | 39.2 / 0.831 | 16.8 / 0.357 |

- **Audio work.** JQ3's audio-engine code (822B-8233) per second, b4x_nolock2 → b4ref2: 510 → 495 ms/s, while fps rose
  10.8 %. XMA 246 → 239 ms/s; XThread's audio code 106 → 103 ms/s. Client blocks are exactly 187.5 a second in every
  run. If the audio work were frame-paced, its per-second cost would have risen 10.8 %; it fell 3 %.
  **The proposer's claim holds.**
- **Frame-paced host code.** Renut's NewOp, FastDrawLocked and PushFloatConstants keep their ms a frame within 3 %. They
  are the controls.
- **The twist: the process is always about 3.3 cores busy.** In all 17 race profiles of 2026-11-12/13 (18-22 fps, three
  builds, MSAA 1/2/4, locked and lock-free kick) the busy share is **3.27-3.31 cores**. When the GPU gets faster, the
  guest frame code's ms a frame also falls: Game Master −9 %, JQ0-2 −10 %. Memory-bound copies get faster per unit of
  work at lower MSAA: per second, 8230AD50 −10 % and 8250A7E8 −13 %, which is GPU-bandwidth contention.

  So the model that fits every A/B is: **fps = (B − A) / W_f**, where
  - B ≈ 3.28 cores busy,
  - A ≈ 1.16 cores of audio/XMA work (52 ms a frame today),
  - W_f ≈ 95 ms of frame-paced CPU a frame.
- **What follows for ranking:**
  - A saving of δ ms a frame (measured today) gives Δfps/fps ≈ δ / W_f ≈ **1.05 % per ms, whether it is audio or
    frame-paced**, as long as it is a few ms.
  - Measured checks:
    - The XMA cache 24 → 256 MB saved 4.3 ms of XMA alone and gave +3.9 % (the model predicts +4.5 %).
    - CG-13 gave +4.6 %. Its measured 8 ms fall in CPU a frame includes the audio part shrinking per frame as fps rose;
      the direct saving was about 5 ms.
  - The proposer's 0.74 factor is the average over the *whole* road to 30 fps. Frame savings speed up hyperbolically;
    audio savings are linear. It changes nothing for a list whose total is 5-8 ms.
  - So I rank audio and frame-paced ms one to one, and mark the pacing only so that a later reader can redo the sums at
    another fps.
- **30 fps.** With B at about 3.3 cores, 30 fps needs W_f ≈ (3.28 − 1.16) / 30 = 71 ms, i.e. **about 24 ms off the
  frame-paced work**. Audio savings measured today would have to reach about 33 ms, which is impossible. The brief's
  133 ms assumes 4.0 busy cores; the race has never been above 3.31.

### 1.3 The profile's conditions

- Both of the proposer's profiles ran at MSAA 1, with the scene mostly undrawn (progress log 22:00).
- At the shipped 4x MSAA the same build's CPU a frame is about 10 % higher (b4x_nolock2 against b4ref2: 171.6 against
  154.7 ms a frame), mostly memory contention.
- Every gain below scales about the same way in the shipping configuration. A/Bs must compare like with like.

### 1.4 The XMA facts behind M-01 .. M-09 (renut_031.log, the 22 race windows)

- **Catch-up decodes.** In the race windows: 481,508 frames answered, 385,134 decoded, 224,333 decoded again (catch-up).
  So catch-ups are **36.8 %** of FFmpeg decodes (batch 4: 40.2 %).
- **Where the catch-ups come from.** "verify N checked" counts runs cut at `kMaxPending = 256` (verify mode is off), and
  each cut forces a 256-frame catch-up. 647 cuts × 256 = 165,632, i.e. **74 % of all catch-ups** (batch 4: 84 %). The
  runs are long: engine loops.
- **FFmpeg's cost** on the XMA thread (tick-weighted): `decode_frame` 3.22 ms, IMDCT/FFT 1.37, `fmul_scalar` and
  `fmul_window` 0.57, frame/packet plumbing 0.6, and its leaf memcpys 0.3: **about 6.4 ms** for the decoded plus
  re-decoded frames.
- **The fills.** `Decode`'s three fills are `+0x70` (`input_buffer_`, 24 leaf samples), `+0x2f4` (`xma_frame_`, 22) and
  `+0x41c` (`raw_frame_`, 13): 0.49 ms together.
- **Codec reopen.** `avcodec_alloc_context3`'s `av_mallocz` is 19 leaf samples (0.16 ms).
- **Mutexes.** `mutexUnlock` 0.47 and `mutexLock` 0.16 on this thread. The cache's own mutex is uncontended (one
  decoding thread in mode 1); the contended ones are the per-context locks the guest's XMADisableContext takes: 365-799
  ms of waiting per 10 s (`[xma] ... locks` lines).
- **A full cache.** It reaches 256 MB at about 70 s. From then on, `AddNode` returns 0 whenever any context has frames
  pending (always: "0 restarts"), so no allocation happens in the cache any more. A context that misses drops out
  (`cache_node_ = 0`) until its codec reopens. The answered share still holds at 44-75 %.
- **The exactness argument of M-01 / M-02, which I re-read in `wmaprodec.c`.**
  - `decode_frame` resets `reuse_sf` for every channel each frame (`:1500`).
  - The carry is `out[spf .. 1.5 spf)` copied down (`:1518`). The subframes' IMDCT writes tile `[spf/2, 1.5 spf)`, and
    `wmapro_window` (`:1170-1192`) only mixes a frame's first subframe with the carry, inside `[0, spf)`.
  - `xmaframes_decode_packet` calls `save_bits(..., 0)` (`:2092`): a fresh bit buffer every frame.
  - Untouched channels are zeroed, not left stale (`decode_subframe`'s memset).
  - One more carried field the proposer did not name: `scale_factor_idx` (which of the two `saved_scale_factors`
    buffers is current) plus the stale other buffer. It cannot leak: at a channel's first subframe of a frame
    (`cur_subframe == 0`, `reuse_sf == 0`) all bands are read fresh (`:1050-1058`), and later subframes read only the
    buffer written in this frame.
  - So the state after frame k depends on frame k's bits alone, and PCM_k depends on (format, bits k-1, bits k). The
    argument holds as far as the source shows; the dual-decoder verify key settles it.

### 1.5 Other facts checked

- **memset in the copies (M-22, M-46).**
  - The largest memset caller in the copies is `822E17C8+0x198`: 76 leaf samples, 0.85 ms. It is **not a card mark**: it
    is the second of a pair, `memset(base + (ea >> 12), 0, pages)` (the card range, 2 samples) and then
    `memset(base + ea, 0, rows × 1 KB)`, the real zeroing of the DSP rows. The same holds for `8230AD50+0x1cfc` (14) and
    `+0x12a0` (7).
  - Card-range memsets in the copies are about 54 leaf samples (about 0.5 ms), spread over 8250A7E8, 822E17C8, 8230AD50,
    823BFB58 and others. So M-22's figure holds, but it rests on different sites. M-46's blanket flag would turn the real
    row zeroing back into loops.
- **The speaker config (M-13).** `sub_827E99E8` stores 0 at +41/+43 only when the config `== 1`; with 0x00010001 it
  stores 1. `sub_827E99B0` returns `config == 1`; `sub_82999AB8` tests bit 31.
- **`REX_PROMOTED_CALL` (M-25)** calls the real `sub_X(ctx, base)` with the full ctx hand-over (`n_822CBAD0.h:97`).
- **The Windows-only mentions (M-28).** `blur_write_watch.cpp` names `__imp__sub_8216AE98` / `8216CA28` / `8256D6D8`
  inside `#if defined(_WIN32)`, and `switch_hot_bulk.py:14, 209` skips any mentioned address. `sub_8216AE98` is 0.31 ms
  (tick).
- **The deferred chain (M-31)** is off for N&B's two devices (`queued.cpp:90-96`). Renut's race windows show about
  25,000-32,000 Begin*Vertices whole copies (12,888 bytes each) against about 12,500 delta draws per 10 s, so about 68 %
  of snapshot draws are whole copies. About 0.4 ms of Game Master's memcpy is under `Capture`.
- **The job-queue spin (M-42).** `8227CA08` 0.71 ms plus `8227CCE8` 0.69 ms a frame, spread over the Game Master and the
  JQs.

---

## 2. The fifty, one by one

"ms" is my realistic gain (ms of CPU a frame today), with the pacing.

**Effort:**
- S: at most 1 day;
- M: 2-5 days;
- L: 1-3 weeks;
- XL: months.

**Verification:**
- **G**: the PC guard/verify run (`tools/run_pc.ps1` with `--banjo_cpu_verify`);
- **F**: the forms self-check;
- **P**: a PCM byte-compare of `audio_dump_xma_s` dumps from a PC race, plus `banjo_xma_frame_cache_verify`;
- **R**: Renut's own verify keys (`banjo_renut_snapshot_deltas_verify`, `banjo_renut_store_cards_verify`, frame hashes);
- **A/B**: a console race A/B against a same-session control (ABBA).

### A. XMA decode (host, the XMA Decoder thread: 11.1 ms)

| ID | verdict | ms (pace) | class, risk | effort | deps | verify |
|---|---|---|---|---|---|---|
| M-01 one-frame catch-up | **KEEP** | **1.9-2.3 a** | H exact; low-medium | S-M | none | a dual-decoder verify key (full catch-up on a second codec, memcmp of every later frame) + P; "decoded again" falls to about the run count; A/B |
| M-02 pair-keyed cache | CENSUS-FIRST | 0-0.8 a | H exact; low | M | M-01; census | counter at each miss: would the (prev, packet) key hit? |
| M-03 dead fills | **KEEP** | 0.3-0.45 a | H exact; low | S | none | P (+ guest output ring compare); A/B |
| M-04 flat cache | **REWORK** | 0.3-0.6 a | H exact; low | M | M-05 overlap | P; A/B |
| M-05 codec reset | **KEEP** | 0.2-0.3 a | H exact if equal; medium | S-M | none | unit test reset vs fresh open (non-pointer fields), P |
| M-06 quantiser table | **KEEP** | 0.25-0.35 a after M-01 (0.4-0.55 alone) | H exact; low | S | M-01 shrinks it | exhaustive table-vs-formula bit test; P |
| M-07 cached bit reader | **KEEP** (low) | 0.05-0.15 a after M-01 | H exact (checked reader kept); low | S | M-01 | P |
| M-08 direct output | **DROP** | ≤0.1 after M-01 | H; low | S-M (FFmpeg API patch) | M-01 removes most of its target | - |
| M-09 eviction | CENSUS-FIRST | 0-0.6 a | H exact; low | M | M-02 (pair keys make eviction simple) | counter: misses with `cache_node_ == 0` after the fill |
| M-10 warm cache | **DROP** | about 0 steady-state | H, but the user's call on decoded game audio on the SD | M | - | - |
| M-11 decode window | CENSUS-FIRST (D) | 0-0.5 a | D timing; medium | M | census | blocks written vs consumed before Disable/Clear |
| M-12 idle pre-decode | **DROP** | about 0 | H timing | L | - | - |

Notes:
- **M-01.**
  - Mechanism: the codec's state after frame k is a function of frame k alone (§1.4), so at a miss only the last
    answered frame is decoded, and `kMaxPending` can go.
  - Size: 74-84 % of catch-ups come from the 256-frame cuts, so about 93 % of catch-ups go: 0.93 × 0.368 × 6.4 ms
    ≈ 2.2 ms.
  - Implementation: keep a copy of the last answered packet in the context, so the cache never waits on "pending".
  - Side effects: it shortens the worker's context holds, which helps M-36's lock waits for free.
  - Risk: an FFmpeg state I missed; the dual-decoder key catches it on the PC.
  - Not a repeat of anything done: the frame cache decides what is answered, M-01 removes the cost of answering.
- **M-02 / M-09.**
  - Exact by the same reading (PCM_k = f(format, k-1, k)).
  - Upside: most of the 30-55 % misses after warm-up are probably new audio (music, speech), which no key can answer.
    How many are "same pair, other chain" or "context frozen after the fill" is not logged.
  - Plan: one census in cycle 1 (two counters), then pair keys with clock eviction as one change, if together they
    exceed about 10 % of misses.
- **M-03.** The three fills are 0.49 ms; keep zeroing for the padding FFmpeg may read, and for the second half of
  `input_buffer_` in the one-packet case.
- **M-04, reworked.**
  - Drop the mutex part (the cache mutex is uncontended; the 0.63 ms is the per-context locks, M-36's), and the arena
    (no allocations in the cache once it is full).
  - Keep the custom `get_buffer2` and no `av_frame_unref` churn (0.47 ms of plumbing), and an open-addressing
    `FindNode` (part of `DecodeFrame`'s 0.71 ms self time).
- **M-05.** 0.16 (`av_mallocz`) + 0.07 (`decode_init`) + 0.04 + 0.02 + a malloc/free share.
- **M-06.** Verified: two `fdiv`, `frintm` and exp2 per band at `decode_frame+0x17e0..+0x1888` (62 samples, 0.51 ms
  tick). The table must be filled through the same non-inlined helper the formula uses.
- **M-07.** The 58 samples on `ldrsh` at `+0x16c0` are mostly the VLC table load, which stays. The unchecked reader is
  D: do not take it.
- **M-10.** Cold only in the first window or two of a race (9 %, then 45 %).
- **M-11.** Engine loops rarely stop early.
- **M-12.** XMA is pinned to core 3 (`0x8`), which is 2.9 % idle (`system core 3: idle 2.9%`), and SASRT 14 (run-ahead
  on core 3) was dropped for the same reason.

### B. The audio engine (guest code on JQ3/JQ4/XThread: about 41 ms tick-weighted)

| ID | verdict | ms (pace) | class, risk | effort | deps | verify |
|---|---|---|---|---|---|---|
| M-13 stereo speaker config | CENSUS-FIRST (D) | probably 0; at most 2-4 a | D (the game's own downmix); low | S | census | a channel census in the SDK's 6→2 fold with each config value (PC), then one console race; listening |
| M-14 filter channels in lanes | CENSUS-FIRST | 0.2-0.6 a | E; medium | L | read 822BEDD0's 26 jump-table entries first | G, F (lane kernel vs scalar) |
| M-15 one native call per node | CENSUS-FIRST (likely DROP) | 0-0.2 a | E; low | M | rows-per-callback counter | G |
| M-16 host-endian rows | **DROP** | 0.3-0.8 a | E*, medium-high | L-XL | - | - |
| M-17 native graph walker | **DROP (park)** | 2-4 a | E; high | XL | - | - |
| M-18 voice LOD | CENSUS-FIRST (D) | 0-2 a | D (audible); medium | M | gain histogram in the 822EA500 native | listening A/B |

Notes:
- **M-13.**
  - On 0x00010001 the game takes the "not == 1" path in `sub_827E99E8` / `sub_827E99B0`. The 360's render-driver frame is
    6 × 256 samples whatever the config.
  - Whether the engine builds 2-channel buses for config 1 is unknown. The cheap test: a PC channel census of the
    render-client frames with each value. Only if channels 2-5 go silent *and* the mixer's row count falls is it worth a
    console race.
  - It is the user's call because the game's own stereo mix replaces the SDK's fold.
- **M-14.** 822BEDD0 is 4.0 ms tick; the gain depends on entries that run one filter over several channels with separate
  state.
- **M-16 / M-17.** The `rev32` per vector is hidden under the kernels' bandwidth limits, and one missed guest reader
  means wrong audio. M-17 is months: park it beyond this 48 h window.
- **M-18.** The game already skips zero gains.

### C. Memory, cache and TLB

| ID | verdict | ms (pace) | class, risk | effort | deps | verify |
|---|---|---|---|---|---|---|
| M-19 next-node prefetch | **KEEP** | 0.5-1.0 a | E (hints); low | S-M | find the callers of 822E17C8 / 8230AD50 / 822EB500 | G; entry-load share falls (entry.py on the new profile); A/B |
| M-20 learned jump pointers | CENSUS-FIRST | 0.1-0.3 a | E; low | M | list lengths and stability per walk | A/B |
| M-21 callee-entry prefetch | **REWORK** | 0.2-0.5 f/mixed | E; low | M | M-19's result | G; entry shares per thread group; A/B |

Notes:
- **M-19.**
  - Tick-weighted entry loads (first 160 bytes) in the audio family: 822E17C8 1.28 ms (`ldr w2, [x23, #8]`, the first
    touch of its r3 object, 85 samples), 8230AD50 0.50, 822CBAD0 0.31, 823016D0 0.19, 822EB500 0.18. The ones M-19's
    three loops reach total about 2.0 ms, and a prefetch one node ahead typically hides 30-50 % of them.
  - Prefetch both of the object's first lines (+8 and +44..+59 for 822E17C8).
  - Distinct from idea 52 (next pointer), CG-23 (same function) and SASRT 8 (traces).
- **M-21, reworked.** A whole-program pass with an 8-statement lead mostly prefetches too late or prefetches lines that
  are already cached, and costs code size in every caller. Instead, take M-19's method to the call sites of the top
  about 10 entry-miss callees outside audio (Game Master 1.44 ms of entry loads, JQ34 non-audio, XThread), named by
  hand from entry.py.
- **M-20.** 822CBAD0 (0.92 ms) and 823016D0 (0.6) are dependent-pointer walks. Worth it only if the lists are long and
  stable between callbacks.

### D. Generated code, post_codegen passes and the copies

| ID | verdict | ms (pace) | class, risk | effort | deps | verify |
|---|---|---|---|---|---|---|
| M-22 natives' card marks inline | **KEEP** | 0.4-0.7 mostly f | E for every card Renut reads; low | S | - | R (store-cards verify on a PC race); G; count of card-range `bl memset` → 0; A/B |
| M-23 float loads after CG-13 | **KEEP** | 0.1-0.3 mixed | E; low | S | CG-13 on (it is) | F (add the forms), G, A/B |
| M-24 fmadds known-single | **KEEP** (low) | 0.05-0.15 mixed | E; low | S | single_floats.py's dataflow | F (KnownSingle), G |
| M-25 promoted targets in the closure | **KEEP** | 0.2-0.5 mixed | E; low (code size) | M | overlaps SASRT CC2 (port 121, at the final base merge) | G; NRO size; A/B |
| M-26 vector PCS | **DROP** | sign unknown | E; medium (ABI) | M | - | - |
| M-27 hot loops outlined | CENSUS-FIRST | 0-0.5 mixed | E; low-medium | L | objdump: are the spills inside the hot loops? | G, A/B |
| M-28 Windows-only mentions | **KEEP** | 0.1-0.2 f | E; none | S | the next machine-copy refresh | G; the "not optimised" list |

Notes:
- **M-22.** The card-range memset calls also force caller-saved spills around them (`stp q30, q29, [sp]` at 822E17C8
  +0x17c). Inline two-page tests with the view filter and check-first, as VS-11/12 did for the copies' own marks; leave
  the real row zeroing alone.
  - Not a repeat of codegen-ideas-3 §4: "card marks in the copies" covered the copies' own marks, not the natives'
    `Card()`.
- **M-23.** Take the load half: the A57's 5-cycle GPR→FPR move comes off a dependency chain. The store half saves one
  instruction off any latency path; float_loads.py's docstring is right that it is about a wash.
- **M-24.** Only 16 samples sit on the tests in the top 40 copies; mostly code size.
- **M-25.** About 1 M promoted calls a second in the PC census. At 10-30 cycles of hand-over each, that is 0.45-1.3 ms;
  inlining keeps perhaps half of it.
- **M-26.** It makes every FP-using callee save full q8-q23, while the gain is only in VMX callers.
- **M-27.** GCC's IRA is already regional per loop; check whether the spills in 8230AD50 / 8250A7E8 are in their hot
  loops first.
- **M-28.** The copy tool should read mentions only outside `#if defined(_WIN32)`. Then the standing machine-copies rule
  picks the functions up.

### E. The job-queue threads' work

| ID | verdict | ms (pace) | class, risk | effort | deps | verify |
|---|---|---|---|---|---|---|
| M-29 LZX decoder as a native | **KEEP** (last) | 0.5-0.9 a (stream-paced: its cost per second is constant across fps, `cmp.py` 1.04) | E; medium (xcompress framing) | L | - | a PC differential (both decoders on the same input, output and context compared); G; A/B |
| M-30 LZX block cache | CENSUS-FIRST | 0-0.5 a | E; low (memory) | M (S census) | census of repeated block hashes over 3 laps | PC differential |

Note on M-29: 829D0300 is 1.48 ms tick-weighted (the proposer had 1.67), JQ1/JQ2/JQ4. A native at 2-3× is 0.5-0.9 ms.
It is an extension of CG-29 (declined), not a repeat.

### F/G. Renut's GPU thread, recording side, memcpy

| ID | verdict | ms (pace) | class, risk | effort | deps | verify |
|---|---|---|---|---|---|---|
| M-31 Begin*Vertices into the delta chain | **REWORK** | 0.4-0.7 f (Game Master + render) | E for the picture; low-medium | S if one device, M if two | device census | R (snapshot deltas verify); "whole copies" → about 0; A/B |
| M-32 extent memo ways | **KEEP** (low) | 0.03-0.08 f | H exact; none | S | - | hit counter |
| M-33 per-page texture hashes | CENSUS-FIRST | 0-0.3 f | H exact; low-medium | M | time spent in the texture hash per 10 s | R |
| M-34 draw merging | CENSUS-FIRST (likely DROP) | 0-1 f, probably about 0 | E; medium | L | census of mergeable runs in a draw log | frame hashes |
| M-35 narrowed resolve snapshot | **KEEP** (low) | 0.1-0.15 f | H exact if ResolveStateLocked reads nothing else; low | S-M | - | a verify mode comparing emitted commands; frame hashes |

Notes:
- **M-31, reworked.** First count the distinct devices that draw in a Blur race (one counter in `Capture`). If Blur
  draws on one device, `banjo_renut_deferred_chain` alone does the job (S), checked by the deltas verify key on a PC
  race. Per-device chains (M) only if it draws on two.
- **M-34.** The fast-draw epoch path already makes identical-state draws cheap.

### H. Kernel / HLE and synchronisation

| ID | verdict | ms (pace) | class, risk | effort | deps | verify |
|---|---|---|---|---|---|---|
| M-36 bounded XMADisableContext wait | **KEEP** (A/B, D timing) | CPU 0.1-0.3 a; fps unknown, possibly +1-2 % | **D** (timing only: the guest sees the context as a slower decode would leave it); low-medium | M | M-01 (shorter holds already) | the lock-wait line; "requests without data"; listening; A/B |
| M-37 tick counter | **KEEP** (low) | 0.05-0.1 | H; none | S | - | A/B |
| M-38 timestamp timer on demand | CENSUS-FIRST | 0-0.5 | H if unread, D if read | S | does the title import xboxkrnl ordinal 0xAD (log it at variable import resolution) | the census; A/B with `kernel_timestamp_interval_ms` |
| M-39 thread-local counters | **KEEP** (low) | 0.03-0.1 | H; none | S-M | - | A/B |
| M-40 Game Master draw locks per batch | **KEEP** (low) | 0.1-0.15 f | H; low-medium | M | after M-31 (same lock) | R; A/B |

Notes:
- **M-36.** The guest waits 365-799 ms per 10 s, up to 43 ms at a time, in JQ3/JQ4 job workers that also run frame
  jobs. The kick-side lock removal gave +5.9 % (lock-free kick), so the disable side deserves an A/B.
- **M-38.** The TimerQueue thread is 0.7 ms a frame.
- **M-40.** Not SASRT 5 (the consumer side).

### I. Scheduling across the four cores

| ID | verdict | ms (pace) | class, risk | effort | deps | verify |
|---|---|---|---|---|---|---|
| M-41 XMA kick coalescing | CENSUS-FIRST (D) | 0-0.8 | D timing; medium | S-M | M-44 | M-44's switch counts; "requests without data"; listening |
| M-42 job workers stop spinning | CENSUS-FIRST (D) | CPU 1.4+ ms of accounting; fps 0-0.3 % | D (wake latency); medium | S-M | M-44 | yield counter; A/B on fps, not CPU |
| M-43 paired audio callbacks | CENSUS-FIRST (D) | 0-3 a | D (+5.3 ms latency, 10.7 ms parameter granularity); medium-high | M | does the guest client handle two requests back to back? `PumpTick` already requests two when the queue empties | A/B on JQ3/4; listening |
| M-44 switch census | **KEEP** (measurement) | 0 | H | S | - | - |

Notes:
- **M-41.** The game polls the context with 50 us sleeps, so delaying the decode adds polls (more switches).
- **M-42.** All game threads are 0x3B, and a yield hands the core to any ready one. The spin fills idle time, so removing
  it lowers "CPU a frame" without raising fps unless it delays ready work. Judge it by fps.
- **M-43.** The only large audio lever (warm L2 on the second traversal), but it is a guest contract question first.
- **M-44.** It prices M-38/41/42/43. The tick-vs-sample gap of §1.1 is *not* kernel time (samples also count preempted
  time); count waits, wakes and yields directly.

### J. Compiler and link

| ID | verdict | ms (pace) | class, risk | effort | deps | verify |
|---|---|---|---|---|---|---|
| M-45 early RA on clean shards | CENSUS-FIRST | 0-0.5 | E if no miscompile; medium (the PC cannot see GCC miscompiles) | S + checks | compile one bulk shard both ways: spill counts + the `tbl` check | F on the console, a guarded console run, A/B |
| M-46 no loop distribution | **DROP** | ≤0.2 beyond M-22 | E; low-medium | S | - | - |
| M-47 FFmpeg strict aliasing | CENSUS-FIRST | 0-0.3 a | H exact (P proves it); low-medium | S | M-48 | P; A/B |
| M-48 LTO flag scope | CENSUS-FIRST | 0-0.6 f | H/E; low-medium | S (one extra link) | - | disassembly diff of NewOp/FastDrawLocked between two links of the same objects; then R and A/B |

Notes:
- **M-46.** The big memsets are real row zeroing, where newlib's `dc zva` path wins; the 1-2-page ones are M-22's.
- **M-47.** The store in `decode_frame`'s coefficient loop is `AV_WN32A` (an `av_alias32` may_alias union). The reload of
  `gb->index` (`[x19, #400]`) stays under strict aliasing. The gain is unproven.
- **M-48.** GCC merges `-fno-strict-aliasing` conservatively at the LTO link but also keeps per-function optimisation
  nodes, so only a diff of two links says which applies.

### K. Anything else

| ID | verdict | ms (pace) | class, risk | effort | deps | verify |
|---|---|---|---|---|---|---|
| M-49 mirror pass at half rate | **KEEP (D: the user's decision)** | 1.5-3 f (+ GPU time) | D (visible); medium | M | find the guest call that renders the 512x256 pass | draw counts per frame; looking at the mirror; A/B |
| M-50 sampler tax | **KEEP** (measurement) | 0 for players; corrects the A/B baseline | H | S | - | one race pair, stacks on and off, the same NRO |

Notes:
- **M-49.**
  - Size: about 130 draws a frame go to the 512x256 mirror target. Halving them is about 6.5 % of draws: render thread
    about 1.1 ms, Game Master and recording about 0.9, plus the mirror camera's share of the scene jobs (unmeasured,
    perhaps 0.5-1).
  - Check: the 256x256 reflection already alternates.
  - It is the only D item with both a credible mechanism and a size above 1.5 ms.
- **M-50.** The sampler thread itself (0x2A on core 3) is 0.5 ms a frame; add the pauses.

---

## 3. Implementation plan: build cycles

Each cycle is one instrumented build, one console training race, one use build and the A/B races (about 3-4 h). The
standing rules apply to every cycle:
- refresh `localized_bulk.txt` from the latest profile;
- merge exactly one PGO stream and change nothing between the training race and the use build;
- run a same-session control in ABBA;
- no screenshots during measured races;
- judge each cycle by wall-clock fps and the CPU table, at the same MSAA as the control.

**Cycle 1: the exact XMA batch, the card marks, and every census.** Expected 3.6-4.6 ms (about +4 %).
- Changes:
  - M-01 (with its dual-decoder verify key);
  - M-03;
  - M-05 (with the reset-vs-open unit test);
  - M-06 (with the exhaustive table test);
  - M-04 reworked (custom `get_buffer2`, no frame churn, open-addressing lookup);
  - M-22;
  - M-28 (then the copies refresh picks the three functions up).
- Census counters, no behaviour change:
  - M-44 (waits, wakes and yields per thread);
  - M-02 (pair-key would-hit) and M-09 (frozen-context misses);
  - M-31 (devices drawing);
  - M-38 (ordinal 0xAD imported?);
  - M-13 (non-zero channels in the 6→2 fold);
  - M-15 (native row calls per second);
  - M-11 (blocks written vs consumed);
  - M-18 (voice gain histogram);
  - M-30 (LZX block hashes).
- Also in the A/B session: M-50 (one race with `switch_profile_stacks` off).
- Verification:
  - PC: a race with the M-01 key, `banjo_xma_frame_cache_verify` and `audio_dump_xma_s` before/after, byte-compared
    (0 differences);
  - the guard (`--banjo_cpu_verify`) plus `banjo_renut_store_cards_verify` for M-22/M-28;
  - console A/B.

**Cycle 2: guest-code passes.** Expected 1.2-2.4 ms (about +1.5 %).
- Changes:
  - M-23 (load forms);
  - M-24;
  - M-19 (the three named loops);
  - M-21 reworked (about 10 hand-named call sites);
  - M-25;
  - M-07 (a one-line FFmpeg define).
- Verification: F (new forms), G, entry.py shares before/after on the new profile, PCM for M-07, A/B.

**Cycle 3: Renut and the Game Master (frame-paced).** Expected 0.7-1.2 ms.
- Changes:
  - M-31 in the form the census picked (cvar, or per-device chains);
  - M-35;
  - M-40;
  - M-32;
  - M-37 and M-39 (host hygiene);
  - M-38 if the census says the bundle is unread.
- The extra link for **M-48**'s disassembly check rides along. Same objects, same profile, only the link flag differs.
  If strict aliasing is lost on Renut, the next cycle drops the link-line flag (and M-47 follows).
- Verification: R (deltas verify, frame hashes), A/B.

**Cycle 4: XMA cache v2 and the disable wait (only if cycle 1's census is positive).** Expected 0.3-1.2 ms, plus M-36's
fps effect.
- Changes:
  - M-02 + M-09 as one change (pair keys + clock eviction);
  - M-36, with the user's OK, since it is D timing;
  - M-47/M-48 if cycle 3's check found strict aliasing lost.
- Verification: P with the verify keys; the lock-wait line; listening; A/B.

**Cycle 5: LZX native (M-29).** Expected 0.5-0.9 ms.
- This is L effort and may not fit the 48 h window. Build the PC differential first.
- Then, with what cycles 1-4 measured:
  - M-14 if the dispatcher census shows multi-channel entries;
  - M-45 if a test compile of one shard shows fewer q/d spills;
  - M-27 if the spills sit in the hot loops.

**D experiments (the user's decisions, each one cvar and one A/B, any cycle):**
- M-13: one race, if the census shows the engine adapts.
- M-49.
- M-43, M-41, M-42: after M-44's counts.
- M-18 and M-11: after their censuses.

---

## 4. D items that need the user's decision

| ID | what changes | my view |
|---|---|---|
| M-49 | the rear mirror updates every other frame | the only big one (1.5-3 ms + GPU); visible |
| M-13 | the game mixes for stereo instead of 5.1 folded by the SDK | probably 0; test only after the PC census |
| M-43 | +5.3 ms audio latency; parameter updates at 10.7 ms granularity | potentially 0-3 ms; guest contract risk |
| M-36 | the guest sees a context as a slower decode would leave it | low risk; equivalent to hardware timing; needs an A/B and a listen |
| M-41 | decodes start up to about 0.2 ms later | wait for M-44; may add game polls |
| M-42 | an idle job worker parks instead of yielding | judge by fps; the spin mostly fills idle |
| M-18 | voices below a threshold are not mixed or decoded | audible in principle; census first |
| M-11 | the output ring is filled only N blocks ahead | starvation risk; census first |
| M-10 | decoded game audio written to the SD | dropped; would be the user's call |

---

## 5. Realistic total

KEEP and REWORK, exact or host-only (the D items, CENSUS-FIRST items and M-49 left out), at the midpoints:

| group | ideas | midpoint |
|---|---|---|
| XMA | M-01 2.1, M-03 0.38, M-04 0.45, M-05 0.25, M-06 0.3, M-07 0.1 | **3.6 ms** |
| Guest code | M-19 0.75, M-21 0.35, M-22 0.55, M-23 0.2, M-24 0.1, M-25 0.35, M-28 0.15 | **2.45 ms** |
| LZX native | M-29 | **0.7 ms** |
| Renut and host | M-31 0.55, M-32 0.05, M-35 0.12, M-37 0.07, M-39 0.06, M-40 0.12 | **0.97 ms** |

- **On paper: about 7.7 ms (range 6-8.5 ms).**
- **Expect 5-7 ms realised.** Overlap: M-19/M-21 share entry misses, M-25 overlaps CC2, M-29 may not fit the window.
  Implementation shortfall: prefetches and the reset path.
- **In fps: +5-7 %** by the model of §1.2 (about 1.05 % per ms, audio or frame), i.e. **CG-13's 22.1 → about 23.2-23.6
  fps** at its conditions, and about 10 % lower figures at 4x MSAA.
- **If the user takes the D items:** M-49 adds 1.5-3 ms (about +2-3 %). M-43, M-13 and M-36 are unknowns that could each
  add 0-3 ms.
- **30 fps** needs about 24 ms off the frame-paced work at today's 3.3 busy cores. Nothing in this list closes that.

The proposer's honest total was 9-13 ms on paper, 5-8 ms expected. **The bottom line agrees; the composition does not:**
- the XMA pile is 3.6 ms, not 4-6;
- M-04's mutex/malloc costs are not the cache's;
- M-13 is probably 0;
- M-12 and M-08 vanish;
- M-31 may be a cvar after a census;
- audio and frame ms rank one to one at this size.

---

## 6. Where I disagree most

1. **Sample weighting (all 50 ideas).** The sampler's round-robin gives every busy thread the same number of samples, so
   raw shares are not CPU shares.
   - The XMA Decoder is 11.1 ms (×0.77), so every XMA figure was 30 % high.
   - The render thread is 16.4 ms (×1.21), so the Renut figures were low.
   - JQ2 is ×0.79.
   - The ct table's per-thread lines were right; its FUNCTIONS table and the proposer's ms are not.
2. **The 0.74 audio rule.** The audio-pacing claim is true (the same ELF at 18.1-21.2 fps: audio ms a second constant,
   Renut host ms a frame constant). But every race runs at 3.27-3.31 busy cores, and at the margin an audio ms and a
   frame ms buy the same fps. The measured XMA-cache A/B (+3.9 % for 4.3 ms of XMA alone) settles it. The 0.74 applies
   only to the full 30-fps distance.
3. **M-04** (0.8-1.4 → 0.3-0.6). The XMA thread's mutex time is the per-context locks the game contends (M-36), and the
   cache allocates nothing once it is full, which is most of the profiled race.
4. **M-01's mechanism is stronger than claimed.** The runs are not 5-10 frames: 74-84 % of catch-ups come from 256-frame
   cuts. The saving is about 93 % of catch-ups, and the size lands at the low end of the proposer's range only because of
   the ×0.77 thread weighting.
5. **M-13** (a "one-line experiment" of up to 5-8 ms). The current config already fails the game's `== 1` test, and the
   360's render-driver frame is 6 channels regardless. A PC census decides it before any console race.
6. **M-22 / M-46.** The single biggest memset caller in the copies (822E17C8, 0.85 ms) is real DSP row zeroing, not a card
   mark. M-22 stays at about 0.5 ms on other sites; M-46's blanket flag would slow the real zeroing.
7. **M-47.** The cited store goes through `AV_WN32A` (may_alias), so `-fstrict-aliasing` cannot remove the reload it
   blames; census with M-48 first.
8. **M-12 and M-42 assume idle CPU.** Core 3 is 2.9 % idle (M-12). M-42's spin runs only when its own worker has nothing
   to do and yields to any ready 0x3B thread, so saving it is CPU accounting, not fps.
9. **M-36 is D (timing), not H.** It may be worth more than its 0.1-0.3 ms of CPU: the lock-free kick precedent was +5.9 %.
