# CPU moonshots, 50 more (2026-11-13): M-01 .. M-50

Read-only study. Nothing was built, run or edited besides this file; the console, Ryujinx and the game were not touched.
The figures come from my own read-only scripts over the two console profiles of 2026-11-12 and their ELFs (the scripts
are in this session's scratchpad, `...\scratchpad\m50\`: `agg.py` aggregates a rex_profile.log's PC lines per symbol and
thread, `top.py` / `groups.py` / `hosttop.py` rank them, `fpcs.py` prints a symbol's hot PCs with the disassembly,
`sample_insns.py` + `classes.py` classify the instruction each sample sits on, `entry.py` / `entry2.py` count samples in
a function's first bytes, `lrleaf.py` maps the profile's `lrleaf` lines to callers, `fmatest.py` / `fmovpat.py` count
code patterns in the hot copies; they are not in tools/: copy them if the audit wants to rerun a number).

## 0. What the profile says

Profiles: **batch 4** (`out/console_runs/2026-11-12_b4ref2`, ELF `C:/blur-nx-test/builds/2026-11-12_b4_A_xsw`, reports
9-20, 155.0 ms of CPU a frame) and **CG-13** (`2026-11-12_cg13_2`, ELF `2026-11-12_cg13_A`, reports 9-20, 147.1 ms; I
paired them by name, and the hot PCs land on the instructions the functions' code says they should). CG-13 is on in
Blur now, so the disassembly and most figures below are the CG-13 build's; "% of 155" is as the brief asks. One sample
is 0.0108 ms of CPU a frame (CG-13), 0.0113 (batch 4); the profiler's ms a frame are right (progress-log, 2026-11-12
22:00 row), its fps and % read high.

1. **A third of the CPU is paced by audio time, not by frames.** The game's audio engine (guest code 0x822B0000-
   0x8233FFFF: JQ Worker 3 20.8 ms, JQ4 11.8, XThread 4.1, GM 1.0) is 38.2 ms a frame, the XMA Decoder 14.5, the
   Audio Worker and the SDK's mixer thread 0.9: **~54 ms of 147 (37 %)**. It runs per 256-sample block, 187.5 a second
   at 48 kHz, whatever the frame rate (rex_profile.log "audio:" lines: 2,036-2,212 client blocks a report in the race,
   whose reports run longer than their nominal 10 s: progress-log, 2026-11-12 19:45 row). At 30 fps
   (wall) the same work is 54 x 22.31 / 30 = 40 ms a frame, and the frame-paced rest is 147 - 54 = 93 ms: 133 ms, which
   is the brief's 30-fps budget (4 cores x 33.3 ms) on paper. The real gap is the ~18 % the cores are already idle at
   22 fps (the ct tables: 328-330 % of 400, corrected; what holds them idle is not measured, see M-44). Practical rule
   for ranking: **1 ms a frame saved in audio/XMA today
   is worth 22.3 / 30 = 0.74 ms a frame at 30 fps; 1 ms of frame-paced work (GM, JQ0-2, render thread) is worth 1 ms.**
   The estimates below are "ms a frame today", not reweighted.
2. **The XMA decoder throws away ~40 % of its decoding.** In the race windows the frame cache's own 10 s line
   (`renut_031.log`, "XMA frame cache") shows 26-58 % of all FFmpeg decodes are "decoded again to catch the codec up":
   output discarded (224,351 such decodes against 393,117 real ones over the CG-13 run; the batch-4 run the same,
   39.7 %). The FFmpeg part of the XMA thread (decode_frame 4.17, imdct 0.75, fft 1.0, fmul 0.74, frame plumbing 0.7,
   DecodePacket 0.30: ~7.8 ms) runs ~1.7 times per frame that is actually needed.
3. **Guest copies wait for memory, most visibly at function entry.** Of JQ3/4's 43.8 ms in copies, 28.3 % of samples
   sit on GPR loads through a host pointer, 9.9 % on vector loads, 4.5 % on `[x19, w, uxtw]`, 4.9 % on `[sp]`; stores
   16 %. Samples in a function's first 160 bytes: **10.9 % of JQ3/4's guest samples (4.8 ms, 3.35 on loads), 11.1 % of
   GM's, 18.2 % of XThread's**. `sub_822E17C8` has 42 % of its 304 samples there, 85 on one instruction (`ldr w2, [x23,
   #8]` at copy+0x4c: the first touch of its r3 object).
4. **Register spills are 7-10 % of guest samples** (`[sp]` loads + stores: JQ0-2 10.0 %, JQ3/4 8.8 %, GM 7.5 %), mostly
   X registers (st x 2.9 %, ld x 2.4 %, ld w 1.1 %; q 1.3 %, d 0.4 %), concentrated in the biggest copies (8230AD50 57
   samples, 8250A7E8 54, 822E17C8 40, 825123B8 28, 822BEDD0 26).
5. **What the sampler cannot see.** Kernel time of thread switches: ~5,400 XMA-worker wakes a second (one per kick,
   renut log "[xma] ... kicks"), ~4,500 `NtYieldExecution` (rex_profile "svcSleepThread: yield"), ~950 sleeps of up to
   1 ms, ~610 parked WaitMultiple, ~1,100 timestamp-timer ticks (the profiler's per-second counts read up to ~20 % high
   in races, for the reason above). And the profiler's own 1 ms stack sampler pauses a
   thread each time (`switch_perf.cpp:1071, 1115`); players run without it.
6. **Things that look hot and are not new levers here:** instruction-fetch line starts are 2-4.6 points above uniform
   (an upper bound of ~4 ms over the process, CG-25/28 and SASRT 7 own it); card stores are 0.1 % of samples now;
   `udiv` (hash buckets) 0.23 ms in all; BTI/PAC hints 13 in the whole ELF; the game's own mixer already skips zero-gain
   and unity-gain rows (`n_8230AD50.h:353-412`, so no zero-gain idea below); the 1 KB vector-add job on XThread
   (`sub_822EE628`) is already a hand native (`hand.txt:12`).

**Excluded as already covered** (each idea below says how it differs where it comes close): CG-01..CG-40 and
codegen-ideas-3 sections 4-8 (CG-13 is on; CG-06 promotions, CG-14 single-known forms, CG-16 done; CG-01/26/11
rejected); VX-01..40, VS-01..42 (dcbz runs/dead dcbz/zero-forwarding, row prefetch, card view gate and check-first for
the copies, domains, unaligned pairs, frame spills, key folding, kernel family); optimising-functions.md (incl. "stale
mentions" item 3: M-28 is one concrete instance of it, measured); the hand/machine guide; SASRT's 20 moonshots and their
audit (pinning, address-arbiter dispatchers, allocator, card logs, lock-free queue, relaxed atomics, flush scheduling,
trace prefetch, callee summaries, frame-pointer-free copies (on in Blur: `app/CMakeLists.txt:565-567`), constant window,
lane = iteration, SMT idioms, run-ahead helper, L2 streams / colouring, keys compiled in, draw plans, cohorts,
autotuning, trace JIT); SASRT's first 13 (FPSR-checked flush, stack round trips, XMA decode cache, shadow vtables,
pinned ctx/base, MMU dirty tracking, BOLT packing, clang, memoised calls, zero-copy geometry, draw-level D3D HLE, SSA
lifter, little-endian guest memory); the CC ladder (CC2 reaches Blur at the final base merge); batches 1-5 of the
progress log (dcbz guard, single floats, promotions, fmadds single, XMA cache 256 MB, lock-free kick, LiveOut fix,
2,507 copies).

**Exactness labels.** E = bit-exact against today's generated code (guest memory and registers); E\* = exact in what
the program outputs, guest memory bytes differ (the guard cannot compare); H = host-side only, with what "exact" means
for it stated (for XMA: byte-identical PCM); D = a user decision (behaviour, timing or output changes).
**Gains** are CPU ms a frame summed over threads, today's frame rate, a range with a confidence (H/M/L). The last
proposer's figures came out at less than half in audit; I have tried to price each from the samples it can touch and
say when I could not. Overlaps are noted and are not additive.

---

## A. XMA decode (host, the XMA Decoder thread: 14.5 ms)

### M-01 XMA-ONE-FRAME-CATCHUP: catch the codec up with the last answered frame only (H, exact)
- **Evidence.** `sdk/src/audio/xma_context.cpp:513-527`: after a run of frames answered from the cache, every one of
  them is fed to FFmpeg again (`for (uint64_t pending : cache_pending_) DecodePacket(...)`, output discarded) before the
  first miss. Race windows: 26-58 % of all FFmpeg decodes are these (point 2 above; 6,153-15,752 a window). Runs are
  also cut at `kMaxPending = 256` frames (`:351`), so a long cached loop (an engine sound) is forced into a 256-frame
  catch-up every 2.7 s.
- **Mechanism.** In `xmaframes` mode the WMA Pro decoder's state after frame k is a function of frame k's bits alone:
  `decode_frame` resets `reuse_sf` for every channel at each frame (`libavcodec/wmaprodec.c:1495-1501`), so scale
  factors never come from the previous frame; the carried state is the second half of this frame's IMDCT output
  (`out[spf .. 1.5 spf)`, copied down at `:1516-1521`: the subframes' IMDCT regions tile `[spf/2, 1.5 spf)`,
  `:1261-1266`, and the first subframe's window `wmapro_window`, `:1170-1192`, ends below `spf`) plus `prev_block_len`
  (set to the last subframe's length, `:1191`); `save_bits(..., 0)` (`:2092`) starts every frame with a fresh bit
  buffer; `skip_frame` is 0 for the XMA codec ids (`:426-427`); `packet_loss` is only read by the packet parsers, not
  this path. So decoding only the **last** pending frame gives the codec exactly the state the full catch-up gives. Then
  `kMaxPending` can go (a catch-up costs one decode however long the run).
- **Exactness.** H, exact by the argument above; needs the proof by test (below), because one unnoticed field would
  make the next frames differ.
- **Gain.** Catch-up ≈ 0.41 x ~8.3 ms (FFmpeg + its frame/alloc share) ≈ 3.4 ms; one decode per run stays (run count
  unknown: if runs average 5-10 frames, 2.0-2.7 ms saved). **2.0-3.0 ms (1.3-1.9 %), conf M.** Audio-paced (x0.74 at
  30 fps).
- **Effort** S-M (1-2 days with the verify mode). **Risk** low-medium (an FFmpeg-internal state I missed).
- **Verify.** PC: a verify key that keeps a second AVCodecContext doing the full catch-up and memcmp's every following
  frame's PCM (plus the existing `banjo_xma_frame_cache_verify`); the 10 s line's "decoded again" count must fall to
  about the number of runs; console A/B (XMA Decoder ms in the CPU table).
- **Not a repeat of** SASRT-1112 #3 / Blur's frame cache (they decide *what* is answered); this removes the cost of
  answering.

### M-02 XMA-PAIR-KEY: key the frame cache by (format, previous packet, packet) instead of the chain (H, exact)
- **Evidence.** The cache is a tree of chains from the codec's open (`xma_context.cpp:337-347`, `FindNode` `:412`): the
  same frame reached through a different history (a sound started or re-opened at another point, a context that fell
  out of the cache, see M-09) is a miss and is decoded again. The answered share stalls at 45-70 % once warm.
- **Mechanism.** By M-01's argument a frame's PCM is a function of (frame k-1's bits, frame k's bits) — the carry and
  `prev_block_len` come from k-1 only — and of the format; the first frame after an open uses the zero carry and
  `prev_block_len = spf` (`wmaprodec.c:490`), i.e. a root key. Key nodes by `hash(format, packet k-1, packet k)`, compare
  both packets' bytes. A miss then needs the codec to hold frame k-1's state: decode k-1 (if it was answered) and k.
  The chain bookkeeping, `cache_pending_` and the epoch restarts go away.
- **Exactness.** H, exact (same argument; verify mode as M-01).
- **Gain.** Unknown share of misses that are "known pair, unknown chain": **0.3-1.5 ms (0.2-1.0 %), conf L.** First a
  census: count, at each miss, whether the pair key would have hit (one counter, PC or console).
- **Effort** M. **Risk** low (memory: two packets per node, or one packet plus the parent packet's hash and a pointer).
- **Verify.** As M-01; answered share in the 10 s line.

### M-03 XMA-DEAD-FILLS: drop the fills that are overwritten before they are read (H, exact)
- **Evidence.** Each `XmaContext::Decode` zeroes `input_buffer_` (4 KB, `:1315`) and then copies one or two packets
  over all of it (`:1390-1391`, `:1429-1430`, `:1434`), zeroes the 4,097-byte `xma_frame_` (`:1439`) before
  `stream.Copy` writes the frame's 1-2 KB into it, and zeroes `raw_frame_` (2 KB, `:1444`) which `DecodeFrame` then
  overwrites. ~4,200 Decode calls a second (answered + decoded). The XMA thread's memset is 0.93 ms and memcpy 1.10;
  `lrleaf` puts 89 of the thread's 188 leaf samples under `Decode`.
- **Mechanism.** Remove the `input_buffer_` fill when both halves are copied (keep it for the one-packet case's second
  half); zero only `xma_frame_` bytes past the copied bits (the padding FFmpeg may read, `AV_INPUT_BUFFER_PADDING_SIZE`
  worth); drop the `raw_frame_` fill on paths that write it whole.
- **Exactness.** H, exact: the PCM written to the guest ring is unchanged (the bytes FFmpeg can read are the same).
- **Gain.** **0.3-0.6 ms (0.2-0.4 %), conf M.** **Effort** S. **Risk** low (the padding length must be FFmpeg's).
- **Verify.** `audio_dump_xma_s` dumps of a PC race, before/after, byte-compared; console CPU table.

### M-04 XMA-FLAT-CACHE: an allocation-free, lock-free decode path (H)
- **Evidence.** XMA thread: `_free_r` 0.46, `_malloc_r` 0.38, `mutexUnlock` 0.61 (57 of its 99 samples are this
  thread's), `mutexLock` 0.20, `av_frame_unref` 0.36, `avcodec_default_get_buffer2` 0.14, `ff_get_buffer` +
  `pool_release_buffer` 0.10, recursive libc locks 0.13 ≈ **2.4 ms**. `AddNode` (`:427-454`) mallocs a
  `std::vector` for the packet, a deque slot, an `unordered_map` node and a `vector<uint32_t>` per key; `FindNode`
  walks bucket -> vector -> node -> packet vector (the `udiv` bucket modulo at `DecodeFrame+0xc5c`, then three
  dependent loads with 6-7 samples each at `+0xc64..+0xc70`); `DecodeFrame` takes the cache mutex per frame although in
  `audio_xma_in_worker` mode 1 one thread decodes.
- **Mechanism.** Nodes in one pre-sized arena (packet bytes and PCM inline, an index, no `std::vector`), an
  open-addressing table with the 64-bit key inline (power-of-two mask, no `udiv`), a custom `get_buffer2` that hands
  FFmpeg a fixed per-context buffer (the decoder sets `AV_CODEC_CAP_DR1`), no `av_frame_unref` churn, and the cache's
  mutex taken only when a second decoding thread exists (mode 2 or the kick-thread fallback).
- **Exactness.** H, exact (the same bytes are compared and returned). The memory governor reads `mallinfo`: the arena
  must be reported (SASRT audit of its idea 3).
- **Gain.** **0.8-1.4 ms (0.5-0.9 %), conf M.** **Effort** M. **Risk** low-medium (memory accounting, the mode-2
  locking).
- **Verify.** `banjo_xma_frame_cache_verify`; console CPU table (malloc/free/mutex rows of the XMA thread).

### M-05 XMA-CODEC-RESET: reset an open codec instead of freeing and reopening it per sound (H, exact if equal)
- **Evidence.** `ResetDecoderState` (`:898-915`) sets `sample_rate = channels = 0`, so the next `PrepareDecoder`
  (`:1238-1260`) frees the context and runs `avcodec_alloc_context3` + `avcodec_open2` again: a zeroed WMA Pro context
  of ~140 KB (`frame_data[32 KB]`, eight 12 KB channel buffers) and `decode_init`'s MDCT set-up per new sound. `lrleaf`:
  19 leaf samples under `avcodec_alloc_context3` (its memset), `xmaframes_decode_init` 0.09 ms, `av_opt_set_defaults2`
  0.02, plus a share of malloc/free.
- **Mechanism.** Keep one opened codec per (rate, channels) per context and reset only the state a frame carries
  (`channel[c].out` zero, `prev_block_len = spf`, the fields `decode_init` sets), verified equal to a fresh open.
- **Exactness.** H, exact when the reset state equals the opened one (test: memcmp of the non-pointer fields).
- **Gain.** **0.2-0.4 ms (0.1-0.3 %), conf M.** **Effort** S-M. **Risk** medium (an incomplete reset = wrong PCM until
  the next open; the test catches it).
- **Verify.** Unit test reset vs fresh open; M-01's verify mode.

### M-06 XMA-QUANT-TABLE: the band quantiser from a table (H, exact)
- **Evidence.** `wmaprodec.c:1402-1410`: per band, per channel, per subframe `quant = ff_exp10(exp / 20.0)` and a
  `vector_fmul_scalar` call over the band. In the ELF this is `decode_frame+0x1788..+0x18d4`: two double `fdiv`
  (`+0x1820`, `+0x1870`), `frintm`, the exp2 polynomial and a `blr x5` per band: **62 samples, 0.67 ms**, plus
  `ff_vector_fmul_scalar_neon` 0.43 ms (40 samples, short bands: the call dominates).
- **Mechanism.** `exp` is an integer in a small range (quant step minus scaled scale-factor differences); a table of
  floats filled at init by the same expression (`ff_exp10(i / 20.0)` for every i in range, the same compiled function)
  gives the same bits. Multiply in the coefficient pass instead of a call per band (one multiply per coefficient either
  way, same operands).
- **Exactness.** H, exact (table filled by the same function; fallback to the formula outside the range).
- **Gain.** **0.4-0.7 ms (0.3-0.45 %), conf M-H.** **Effort** S. **Risk** low.
- **Verify.** A test over the whole range comparing table and formula bitwise; PCM compare of dumped streams.

### M-07 XMA-BITREADER: FFmpeg's cached 64-bit reader for wmaprodec; VLC entries in one load (H)
- **Evidence.** In `decode_frame` 171 samples (1.84 ms) sit on the bit-reader and VLC-table loads: `ldr w1, [x15, w1,
  uxtw]; rev w1; lsl; lsr #23` then two `ldrsh` of the VLC entry (`+0x16a8..+0x16c4`, 58 samples on one `ldrsh`).
  `get_bits.h:57-58` has `CACHED_BITSTREAM_READER` (default 0): the 32-bit refill with a byte swap at every read.
- **Mechanism.** `#define CACHED_BITSTREAM_READER 1` before `get_bits.h` in wmaprodec (a 64-bit cache refilled every
  ~32-57 bits); the VLC `(code, len)` int16 pair read as one 32-bit load. Optionally (D) `UNCHECKED_BITSTREAM_READER`:
  no bounds test, safe for valid streams, an over-read of the padding on corrupt ones.
- **Exactness.** H, exact (same bits, same symbols); the unchecked reader is the decision part.
- **Gain.** **0.2-0.45 ms (0.1-0.3 %), conf L-M** (the misses on the VLC tables stay). **Effort** S. **Risk** low.
- **Verify.** PCM compare of dumped streams; console XMA ms.

### M-08 XMA-DIRECT-OUT: no AVFrame copy, no output copy for catch-up decodes (H, exact)
- **Evidence.** `decode_frame` copies `out[]` into the AVFrame for every channel (`wmaprodec.c:1511-1514`) and then
  `ConvertFrame` (`xma_context.cpp:488`) reads the AVFrame into BE int16; for catch-up decodes the output is discarded
  anyway. `lrleaf`: 30 leaf samples (memcpy) under `decode_frame`.
- **Mechanism.** A decoder option that lets `ConvertFrame` read `s->channel[c].out` directly (or converts in the
  decoder), and skips the output copy entirely when the caller says the frame is a catch-up (only the carry shift
  matters).
- **Exactness.** H, exact (same floats converted).
- **Gain.** **0.15-0.3 ms (0.1-0.2 %), conf M**; less if M-01 removes most catch-ups. **Effort** S-M (an FFmpeg patch).
  **Risk** low.
- **Verify.** PCM compare.

### M-09 XMA-CACHE-EVICTION: keep caching after the cache is full (H, exact)
- **Evidence.** The cache reaches `banjo_xma_frame_cache_mb` (256.0 MB, 119,527 nodes) in the race's 7th 10 s window and
  then stays there with "0 restarts" (renut log). When full, `AddNode` returns 0 while anything is pending
  (`:432-436`), and `DecodeFrame` then sets `cache_node_ = 0` (`:546-547`): that context neither looks up nor adds until
  its codec reopens. So after ~70 s every context that meets a new frame drops out of the cache for the rest of its
  sound.
- **Mechanism.** Eviction instead of "freeze or restart": per-root (per-sound) LRU by last hit, evicting whole
  subtrees of cold roots (with M-02's pair keys, per-node clock eviction); a context whose chain is evicted re-enters
  at its next frame (with M-02 it never loses its place).
- **Exactness.** H, exact (what is answered is still compared byte for byte).
- **Gain.** **0.3-1.0 ms (0.2-0.65 %), conf L** (how much of today's 30-55 % misses are "fell out" rather than "new
  audio" is not logged: add the counter first). **Effort** M. **Risk** low.
- **Verify.** Answered share after 70 s in the 10 s line; verify mode.

### M-10 XMA-WARM-CACHE: start the race with a warm cache (H, exact; needs the user's OK)
- **Evidence.** Answered share in the race's first windows: 9 %, 17 %, 10 %, 1 % (renut log), 45-70 % only after
  ~60 s. Blur plays the same sound banks every race.
- **Mechanism.** Either persist the hottest nodes (by hit count, with their keys) to the SD at exit and load them at
  boot, or pre-decode during the race-load screen the streams the previous race decoded (keys known). Both exact (the
  same key compare). The file is decoded game audio on the user's own SD: **the user's call** (the rule that game data
  is never copied anywhere).
- **Gain.** Steady-state **0.3-1.0 ms (0.2-0.65 %)**, the first minute much more; conf L. **Effort** M. **Risk** low
  (load time, SD space ~256 MB).
- **Verify.** Answered share from the first window; verify mode.

### M-11 XMA-DECODE-WINDOW: decode no further ahead than the game will consume (D)
- **Evidence.** The worker fills each kicked context's output ring as far as it has room (`Decode`/`Consume`,
  `:1180-1236`); voices that are stopped, cleared or re-pitched early leave decoded blocks unread. Not measured.
- **Mechanism.** Census first: count blocks written vs blocks the game's read offset passed before a Disable/Clear.
  If the waste is large, cap the look-ahead (N blocks ahead of the read pointer, refilled at the next kick).
- **Exactness.** D: timing (the game sees `output_buffer_valid` later; the hardware fills the whole ring), underrun
  risk.
- **Gain.** **0-1.0 ms, conf L** (unknown until the census). **Effort** M. **Risk** medium (starvation, audible gaps).
- **Verify.** Census; audio "requests without data" counter; listening.

### M-12 XMA-IDLE-PREDECODE: decode a looping voice's next frames in idle time (H, timing)
- **Evidence.** The cores are ~82 % busy at 22 fps (point 1): idle exists between dependency waits. Engine sounds are
  long cached loops (M-01's 256-frame runs).
- **Mechanism.** When the XMA worker finds no kick pending, it decodes ahead (into the cache, keyed as usual) the next
  frames of contexts that are mid-run, at the lowest priority, stopping at the first kick.
- **Exactness.** H (exact output: answers are keyed and compared); the timing of the work changes.
- **Gain.** **0-1.0 ms, conf L** (zero if the frame becomes fully saturated: it only moves work into idle). **Effort** L.
  **Risk** low-medium (it can steal a core the system wanted on core 3).
- **Verify.** Answered share; console A/B.

## B. The audio engine (guest code on JQ3/JQ4/XThread: 38.2 ms)

### M-13 AUD-STEREO-CONFIG: tell the game the output is stereo (D, a one-line experiment)
- **Evidence.** `sdk/src/kernel/xboxkrnl/xboxkrnl_audio.cpp:187-189` returns `0x00010001` from
  `XAudioGetSpeakerConfig` (Xenia's value: as I read the XDK's flags, analog stereo plus the digital Dolby Digital
  bit). XDK audio code branches on it (`sub_827E99E8`, `renut_recomp.58.cpp:24727`: `== 1` picks other byte settings at
  +41/+43 of an engine object; `sub_827E99B0`, `renut_recomp.182.cpp:23876`). The Switch output is folded 6 -> 2
  anyway (`switch_audio_system.cpp:127`, `sequential_6_BE_to_interleaved_2_LE`). The mixer zeroes and mixes 1 KB
  rows per channel and bus (`n_8230AD50.h` zero loop, `r31 + 22` rows).
- **Mechanism.** A cvar for the returned value; one PC run counting how many of the six channels the guest's frames
  carry non-zero, and one console race with JQ3/4 ms. If XAudio/the engine builds 2-channel buses for a stereo config,
  every per-channel stage shrinks.
- **Exactness.** D: the game mixes for stereo (its own downmix instead of the SDK's fold; handheld is stereo anyway;
  docked over HDMI the port outputs stereo today too).
- **Gain.** **0 if the engine always mixes six channels; up to ~5-8 ms (3-5 %) if the channel-proportional stages
  shrink by two thirds. Conf L, but the test is minutes.** **Effort** S. **Risk** low (audio mix difference; a cvar).
- **Verify.** Channel census in the fold; JQ3/4 ms; listening on the console.

### M-14 AUD-IIR-LANES: run independent filter channels in NEON lanes (E)
- **Evidence.** `sub_822BEDD0` (3.76 ms; "a jump-table dispatcher over 26 entries calling 14 functions",
  hand-natives-batch3b.md:79): fmul 42 + fmadd 29 of 349 samples, in serial scalar chains (`fmul s31, s28, s31;
  fmadd s31, s27, s23, s31` x8 at copy+0x9648..+0x9668; `fmadd s27, s23, s28, s27` with 9 samples at +0x3658): a
  latency-bound recurrence per sample.
- **Mechanism.** Find the jump-table entries that run the same filter over several channels with separate state; a
  hand native computes four channels at once, each lane doing exactly the scalar's operations in the scalar's order
  (`fmla .4s` is the same fused single rounding as `FmaddsSingle`'s `fmaf`).
- **Exactness.** E (no reassociation inside a lane). **Not** SASRT 12 (lane = iteration over time, dropped: here lane
  = channel).
- **Gain.** **0.3-1.0 ms (0.2-0.65 %), conf L** (depends on what the entries are; reading the dispatcher comes first).
  **Effort** L. **Risk** medium (state layout per channel).
- **Verify.** Guard (PC); forms self-check of the lane kernel against the scalar on random and edge inputs.

### M-15 AUD-ROW-BATCH: one native call per node instead of one per 1 KB row (E)
- **Evidence.** The mixer's row loop (`n_8230AD50.h:318-419`) reloads `r26[44]`, `r26[45]`, `r31[40]`, `r31[42]`,
  `r31[22]` and recomputes the row address per row, round-trips the gain through the stack (`SingleToDoublePure`,
  `QuietSingleBits`, a store at r1+80), stores r3-r10 to ctx and calls one of three natives (add, `s*b + a`,
  crossfade), each of which re-checks its gates (`On()`, `Straight()`) and reloads its arguments from ctx.
- **Mechanism.** A `NativeRows(count, stride)` entry for `822E9F20` / `822EA500` / `8230CBB8` and a localize rule that
  turns this loop shape into one call per node, the row loop inside the native; the per-row operations unchanged.
- **Exactness.** E (same stores, same order; the guard compares).
- **Gain.** **0.1-0.4 ms (0.1-0.25 %), conf L** (rows per callback not logged). **Effort** M. **Risk** low.
- **Verify.** Guard; console JQ3/4 ms.

### M-16 AUD-HOST-ENDIAN-ROWS: keep the DSP chain's private rows in host byte order (E\*)
- **Evidence.** Every mixer kernel swaps on load and store: in the mixer's inlined kernel 12 `rev32` per 8 `stp q`
  (`copy+0x970..+0x9c8`, 16 + 11 + 9 + 8 + 7 samples on the stores); 822E17C8 stores through `fmov w0, s31; rev w0;
  str w0` (`+0xa58`). The rows are scratch of the audio engine (zeroed by 8230AD50, read and written by the natives,
  summed by 822EE628 on XThread).
- **Mechanism.** Census (PC load/store watch) of every function that touches the row memory; if only the natives and
  the final submix do, the natives keep rows in host order and the boundary (the render-client frame) swaps once.
- **Exactness.** E\*: the audio output is identical if the census holds; the guest memory image of the rows differs
  (the guard cannot compare; a debug mode swaps back at each native's exit and compares).
- **Not** SASRT-1112 #13 (all guest memory little-endian by address XOR) nor VS-18 (domain inside one lane-wise
  chain): a buffer-level domain across kernels.
- **Gain.** **0.5-1.5 ms (0.3-1.0 %), conf L** (the kernels are partly bandwidth-bound). **Effort** L-XL. **Risk**
  medium-high (one guest reader missed = wrong audio).
- **Verify.** The swap-back debug mode; audio dumps compared; listening.

### M-17 AUD-GRAPH-NATIVE: one native that walks the mixer's node graph (E, moonshot)
- **Evidence.** 8230AD50 (7.74 ms) is an interpreter: 8-byte node entries `(type, offset)` at r24
  (`n_8230AD50.h:205-433`, `r24 += 8` at `:431`),
  `r3 = r26[24] + entry` per node, a `ctr`-decrement switch over types 0-5, then the kernels; with 822E17C8 (3.28),
  8230B778 (1.75), 8230AAD0 (0.81), 822EB500 (0.77) and the natives inside them the chain is ~15 ms of JQ3/4.
- **Mechanism.** The end point of M-15, M-16 and M-19: a native graph walker with the node types as C++ (each a copy
  of today's exact native or copy), host-order rows, the next node's state prefetched, no ctx traffic between nodes.
- **Exactness.** E per node (guarded against the copy's original statements).
- **Gain.** **2-4 ms (1.3-2.6 %), conf L.** **Effort** XL (months: every node type, exactness per type). **Risk**
  high.
- **Verify.** Guard per node type; audio dumps; console.

### M-18 AUD-VOICE-LOD: stop mixing voices the listener cannot hear (D)
- **Evidence.** The game already skips rows whose gain is exactly 0 (`n_8230AD50.h:369-412`: unchanged gain, == 1.0
  -> add, == 0 -> skip, else `s*b + a`; changing gain -> crossfade). Inaudible but non-zero voices (twenty cars' engines,
  distance attenuation) are still decoded (XMA) and mixed.
- **Mechanism.** Census of per-voice final gains (in the 822EA500 native: the `s` argument) in a race; a threshold
  (say -70 dB) treated as 0 by the native, and the XMA kick of such voices skipped while they stay below it.
- **Exactness.** D (audible in principle; the user decides the threshold).
- **Gain.** **0-3 ms, conf L** (unknown until the census). **Effort** M. **Risk** medium (pops when a voice crosses the
  threshold: fade at the boundary).
- **Verify.** Census; listening A/B.

## C. Memory, cache and TLB

### M-19 AUD-NEXT-NODE-PF: prefetch the next DSP node's state in the graph walks (E)
- **Evidence.** Point 3: in the audio family the first 160 bytes hold 822E17C8 127 of 304 samples (1.37 ms, 85 on
  `ldr w2, [x23, #8]`), 8230AD50 49 (0.53, 44 on `ldrh w1, [x3, #38]`), 822CBAD0 25 (0.27), 822EB500 24 (0.26),
  823016D0 15, 8230B778 14, 8230E9B0 12, 822CD2B0 11: **~2.9 ms of first touches of the node's state**, touched once
  per 5.3 ms callback and cold each time.
- **Mechanism.** At the dispatch loops (8230AD50's node loop `loc_8230AE3C`: the next entry's `r26[24] + r24[+8]` is
  known one iteration ahead; the job lists that call 8230AD50 and 822E17C8) a `REX_PREFETCH_GUEST` of the next node's
  first two lines (the offsets the callee touches first: +8/+44 for 822E17C8, +22..+46 for 8230AD50), placed by a
  localize rule for these named loops.
- **Exactness.** E (a hint). **Differs from** idea 52 (on in Blur: 369 sites, next pointer of a list loop), CG-23
  (prefetch behind a pointer just loaded, same function) and SASRT 8 (trace-derived strides): here the callee's entry
  offsets, one call ahead, at three hand-named loops; it is the cheap measured instance of SASRT 8's `f(list[i])`.
- **Gain.** **0.7-1.5 ms (0.45-1.0 %), conf M** (the ceiling is the 2.9 ms; a prefetch one node ahead hides part of a
  DRAM miss). **Effort** S-M. **Risk** low.
- **Verify.** Console A/B; the entry-sample share of the named functions (entry.py) must fall.

### M-20 LIST-JUMP-POINTERS: history-based prefetch k nodes ahead in list walks (E)
- **Evidence.** 822CBAD0 on XThread (0.81 ms): 64 of 75 samples on loads, in dependent pairs `ldr w2, [x21, w22,
  uxtw]; rev; ldr w3, [x21, w3, uxtw]` (`+0x8c..+0x98`, `+0x160..+0x16c`, `+0x234..+0x240`); 823016D0 similar. The
  lists are walked 187.5 times a second with nearly the same members.
- **Mechanism.** A small host table per loop site (node address -> the node k steps later, written during the walk:
  `jump[node[i-k]] = node[i]`), and a prefetch of `jump[node]` each iteration. A stale entry only prefetches a useless
  line.
- **Exactness.** E (hints only). **Differs from** idea 52 (distance 1) and SASRT 8 (static strides from PC traces):
  dynamic, learned from the previous walk.
- **Gain.** **0.2-0.6 ms (0.1-0.4 %), conf L.** **Effort** M. **Risk** low (table cost per node ~6 instructions).
- **Verify.** Console A/B of the named sites; then widen to idea 52's 369 sites if it pays.

### M-21 CALLEE-ENTRY-PF: prefetch a callee's first lines where its r3 is computed (E)
- **Evidence.** Point 3 outside the audio dispatchers: entry loads (first 160 bytes) GM 1.36 ms, JQ0-2 0.86, JQ3/4's
  non-audio copies ~0.5, XThread 0.8; e.g. 824F35D8's 37 samples on one flag load after its visibility test
  (`copy+0x580`).
- **Mechanism.** A whole-program summary "the first guest offsets each function loads from r3 (r4) before any branch"
  (from the generated statements); at each direct call site in a copy, a `prfm` of `base + r3 + off` placed right after
  the statement that last writes r3, when at least ~8 statements separate it from the call (else nothing).
- **Exactness.** E (hints). **Differs from** CG-23 (intra-function, pointer just loaded) and M-19 (hand-named loops).
- **Gain.** **0.6-1.5 ms (0.4-1.0 %), conf L** (lead time is short in many callers). **Effort** M. **Risk** low (code
  size; a PGO cycle).
- **Verify.** Console A/B; entry-sample shares per thread group.

## D. Generated code, post_codegen passes and the machine copies

### M-22 NATIVE-CARD-MARKS: the natives' card marks inline, view-filtered and check-first (E for the cards Renut reads)
- **Evidence.** The hand natives mark their stores with `banjo::hot::Card` (`app/src/cpu/banjo_hot_common.h:114-116`) =
  `rex::memory::MarkStoreCardsAt` (`sdk/include/rex/system/xmemory.h:72-77`), a loop over pages that GCC turns into a
  `memset` call. The copies hold **350 `bl memset` sites (137 in sub_8250A7E8's copy alone); at least 136 are this
  card range** (a `lsr #12` page computation just before; e.g. `copy+0x2180..+0x21c4`: store a word, then `memset(base +
  (ea >> 12), 0, last - first + 1)`). memset's small path (< 16 bytes) carries 59 samples, 0.64 ms, nearly all on JQ0-2
  and JQ4. These marks are also unfiltered and unconditional, where VS-11/VS-12 made the copies' own marks
  view-filtered and check-first (`banjo_hot_prelude.h:36-41`); `REX_GUEST_CAS_C` (`:61-62`) still writes its page's card
  unfiltered too.
- **Mechanism.** `Card()` for a range of at most two pages as two `rex_card_mark_`-style tests (view filter, write only
  if non-zero), no loop; the same for `REX_GUEST_CAS_C`'s own page. Host writers (XMA output, memcpy natives) keep
  `MarkStoreCardsAt`.
- **Exactness.** E for every card Renut reads (VS-11's argument: the other cards are never read); the guard compares
  guest memory, which the card table is not part of.
- **Gain.** **0.4-0.8 ms (0.25-0.5 %), conf M** (the 0.64 ms in memset plus the call's caller-saved spills in big copies).
  **Effort** S. **Risk** low.
- **Verify.** `banjo_renut_store_cards_verify` on a PC race; count of `bl memset` with a page shift in the copies
  (136 -> 0); console A/B.

### M-23 FLOAT-LS-AFTER-CG13: the float loads and stores CG-13 freed for the vector unit (E)
- **Evidence.** `tools/float_loads.py` (docstring): "Not rewritten: loads that feed an any-mode helper ... stores".
  With CG-13 on, the any-mode helpers are plain operations, but those loads keep the integer form `ldr w; rev w; fmov
  s, w` (the 5-cycle GPR -> FPR move on the A57, the docstring's own figure), e.g. 822E17C8 `+0xab4..+0xabc`; stores go
  `fmov w, s; rev w; str w` (822E17C8 `+0xa44..+0xa4c`, 822BEDD0 `+0xfa8..+0xfb0`). In the 60 hottest copies: **310 such loads and 368
  such stores**; samples on those windows 0.50 + 0.59 ms.
- **Mechanism.** Under `BLUR_FPCR_ONE_MODE` the pass also rewrites loads that feed any-mode helpers (`REX_LOAD_F32`:
  `ldr s; rev32 v.8b`) and a store form `REX_STORE_F32` (`rev32 v.8b; str s`); the macros fall back to today's forms
  when the switch is off, so the generated tree serves both.
- **Exactness.** E (the same four bytes; `sdk/tests/float_loads_test.cpp` covers the load form).
- **Gain.** **0.2-0.5 ms (0.1-0.3 %), conf M.** **Effort** S. **Risk** low.
- **Verify.** Forms self-check (add the store form); guard; console.

### M-24 FMADDS-KNOWN: CG-14's proof applied to the fmadds operands (E)
- **Evidence.** `REX_FMADDS` (fma_single.py, after CG-14) tests all three inputs with `IsExactSingle` at run time
  (`sdk/include/rex/ppc/fma_single.h`); GCC folds the test where it sees the origin, not elsewhere: in the top 40
  copies 1,209 test instructions remain (822BEDD0 318, 8251F778 178, 82455348 126; the `fcvt; fmov; fcvt; fmov; cmp;
  b.ne` before `fmadd s` at 822BEDD0 `+0x10a0..+0x10b8`). The hot headers hold 3,862 `REX_FMADDS` and no known form.
- **Mechanism.** `single_floats.py`'s dataflow marks the three operands; `REX_FMADDS_KNOWN` = `double(fmaf(float(a),
  float(b), float(c)))` with no test.
- **Exactness.** E (the test's true branch, proven).
- **Gain.** **0.1-0.25 ms (0.1-0.15 %), conf M** (only 16 samples sit on the tests in the top 40; the rest is code size).
  **Effort** S. **Risk** low.
- **Verify.** Forms self-check (KnownSingle); guard.

### M-25 PROMOTED-TARGETS-IN-CLOSURE: promoted indirect calls inlined like direct ones (E)
- **Evidence.** CG-06 (batch 3) promoted 570 indirect sites "which a copy's closure can inline"; in the generated
  headers all **1,172 `REX_PROMOTED_CALL` sites call the target as a real function** (`sub_X(ctx, base)` with the full
  ctx hand-over), none as an `f_sub_X` member: e.g. `n_822CBAD0.h:97`, three promoted targets each with
  `ctx.r1/r3/r4/r8/r9/r10` stored and reloaded. The census put 848 M indirect calls in 600 s on the PC, 75 % caught by
  promotions; the busiest are audio's 822E17C8, 822EB500, 822CBAD0.
- **Mechanism.** `localize_native.py` treats a promoted target as a direct callee for closure membership (size limits as
  usual); the guard branch keeps the indirect fallback.
- **Exactness.** E (as any closure member).
- **Gain.** **0.2-0.5 ms (0.1-0.3 %), conf L-M.** **Effort** M. **Risk** low (code size).
- **Verify.** Guard; NRO size; console.

### M-26 GUEST-VECTOR-PCS: the AArch64 vector calling convention for generated functions (E)
- **Evidence.** Point 4: q/d spills 1.7 % of guest samples (~1.8 ms), in the VMX copies (8250A7E8, 8252E168, 825123B8)
  whose calls to other guest functions must spill every live q register (AAPCS64 keeps only the low halves of v8-v15).
- **Mechanism.** `__attribute__((aarch64_vector_pcs))` (GCC 9+) on the generated functions' type (PPC_FUNC, the
  indirect table's function type, the copies), so q8-q23 are callee-saved in full: VMX callers keep vectors across calls,
  callees that use those registers save them.
- **Exactness.** E (a register convention; the guest semantics are unchanged). **Not** CC2 (guest-register hand-over)
  nor CG-36 (GPR callee saves).
- **Gain.** **0.2-0.6 ms (0.1-0.4 %), conf L** (it moves saves into callees; a net only where callers hold vectors).
  **Effort** M (every declaration and pointer type must agree, or the ABI breaks silently). **Risk** medium.
- **Verify.** Build-time check that every indirect call target has the attribute; guard; console.

### M-27 HOT-LOOP-OUTLINE: hot inner loops of the giant copies as functions of their own (E)
- **Evidence.** Point 4: GPR spills 5.3 % of guest samples, in the largest copies (829D0300 90 KB, 8250A7E8 76 KB,
  8230EDE0 59 KB, 822BEDD0 51 KB, 822E98B0 41 KB); the register allocator works over the whole function's live values.
- **Mechanism.** A pass that outlines a sampled inner loop (counted, few live-ins) into a static function taking its
  live-ins as arguments and returning its live-outs, so the loop gets the allocator's full register file; `[[gnu::hot]]`
  and `noinline` to keep it separate.
- **Exactness.** E (the same statements in the same order).
- **Differs from** CG-27 (`-fsched-pressure` flags): restructuring, not flags.
- **Gain.** **0.5-1.5 ms (0.3-1.0 %), conf L.** **Effort** L. **Risk** low-medium (call overhead per loop entry).
- **Verify.** Spill count per function (objdump), guard, console.

### M-28 HOOKS-PLATFORM-SCOPE: a Windows-only diagnostic keeps three hot functions out of the copies (E)
- **Evidence.** The ct tables' top "not optimised" are `sub_8216AE98` (0.45 ms, the cube filler), `sub_8216CA28`
  (0.16) and `sub_8256D6D8` (0.05): the copy tool skips "anything an app or SDK source mentions"
  (`tools/switch_hot_bulk.py:14`, `:209`) and `app/src/blur_write_watch.cpp:34-36` names them, inside `#if
  defined(_WIN32)` (`:15`; the header's Switch side is inline no-ops). 0.66 ms of generated code that the Switch never
  hooks.
- **Mechanism.** The tool reads a mention only outside `#if defined(_WIN32)` blocks (or the diagnostic moves to a
  PC-only list file); then the next batch copies them. (optimising-functions.md item 3 names the general problem; this
  is a measured instance with a one-line cause.)
- **Exactness.** E (copies as usual).
- **Gain.** **0.2-0.35 ms (0.1-0.2 %), conf M.** **Effort** S. **Risk** none.
- **Verify.** Guard; CPU table's "not optimised" list.

## E. The job-queue threads' work (JQ0-2, render/scene jobs)

### M-29 JQ-LZX-HLE: the game's LZX decoder as a host native (E)
- **Evidence.** `sub_829D0300` (1.67 ms, JQ1 65 / JQ2 56 / JQ4 34 samples) with its 19 inlined callees is an LZX block
  decoder: the constants 249 (the secondary length tree) and 20 (the pre-tree) at `n_829D0300.h:221, 454, 2808, 3698`;
  it sits with `sub_829D2E80` ("a Huffman/LZ decoder", hand_jq012.draft.txt:11, declined) in the xcompress library range,
  called through `sub_829CDF48` from `sub_829CD598` / `sub_829CD690`. Its samples are byte-table loads (`ldrb w4, [x1,
  w4, uxtw]` at `+0xfe8`, 13). The SDK already links libmspack's LZX decoder (`mspack` in `app/CMakeLists.txt`'s SDK
  target list).
- **Mechanism.** Replace the decompressor entry (the context create/decompress pair) with a native over
  `lzxd`/an equivalent, keeping the decompression context's state in host memory keyed by the guest context address,
  and marking the output range's cards once (M-22) instead of per byte store.
- **Exactness.** E: LZX is deterministic; the guard compares output bytes and the guest context words the game reads
  back (if any).
- **Differs from** CG-29 (a native for one Huffman function, declined for its table misses): the whole decoder,
  host-endian tables, a library implementation.
- **Gain.** **0.8-1.2 ms (0.5-0.8 %), conf M** (a native LZX at 2-3x the copy's speed). **Effort** L (the xcompress
  framing and streaming state). **Risk** medium.
- **Verify.** PC differential (both run on the same input, output and context compared); guard; console.

### M-30 JQ-LZX-BLOCK-CACHE: do not decompress the same block twice (E, census first)
- **Evidence.** The LZX decoder runs all race long (1.67 ms a frame = ~37 ms a second): Blur streams the track while
  racing, and a lap streams the same track sections as the last.
- **Mechanism.** Census: hash of each compressed input block and its output size over a 3-lap race; if blocks repeat,
  a host cache (input hash + byte compare -> output bytes, bounded in MB) answers them, writing the output and its
  cards as the decoder would.
- **Exactness.** E (byte-compared key; output bytes identical).
- **Gain.** **0-1.0 ms (0-0.65 %), conf L** (zero if every block is new). **Effort** M (S for the census). **Risk** low
  (memory).
- **Verify.** Census; PC differential.

## F. Renut's GPU (render) thread and the recording side

### M-31 REN-DEFERRED-CHAIN-PER-DEVICE: Begin*Vertices draws into a per-device delta chain (E)
- **Evidence.** renut_031.log, race windows: "**19,665-39,907 Begin*Vertices draws as whole copies**" per 10 s, each a
  12,888-byte device copy (`d3d.h:302`, `kDeviceSnapshotSize = 0x3258`) on the Game Master (`Capture`,
  `queued.cpp:649-667`), ~250-515 MB per 10 s; `lrleaf` puts 35 of GM's 78 leaf samples (memcpy) under `Capture`, and
  the render thread reads each snapshot. `banjo_renut_deferred_chain` (`queued.cpp:89-96`) would put them in the delta
  chain but is off because the chain is one for every device and N&B drew on two devices from two threads.
- **Mechanism.** One delta chain per device (shadow, generation, lock), then the deferred chain on for Blur.
- **Exactness.** E for the rendered result (`banjo_renut_snapshot_deltas_verify` compares the rebuilt device with the
  whole copy, byte for byte).
- **Gain.** **0.4-0.8 ms (0.25-0.5 %), conf M** (GM's memcpy share plus the render thread's reads). **Effort** M.
  **Risk** low-medium (ordering under the chain lock; the verify key covers it).
- **Verify.** The deltas verify key on a PC race; the 10 s "whole copies" count -> ~0; console A/B (GM memcpy row).

### M-32 REN-EXTENT-MEMO-WAYS: the recording side's extent memo misses less (H)
- **Evidence.** `SlotExtentOf` (`command_buffers.cpp:1400-1432`): a per-slot last entry, then a direct-mapped
  256-entry memo by the top 8 bits of a hash; a miss runs `GetTextureDataSizes` -> `GetTextureLevelLayout` per mip
  (`texture_layout.cpp:231-249`): 0.16 ms of `GetTextureLevelLayout` and 0.04 of `GetTextureDataSizes` on JQ0-2/4.
- **Mechanism.** Two or four ways per set, or a 1,024-entry table.
- **Exactness.** H, exact (memo of a pure function of six words).
- **Gain.** **0.05-0.15 ms (0.03-0.1 %), conf M.** **Effort** S. **Risk** none.
- **Verify.** A hit counter; console.

### M-33 REN-TEX-PAGE-HASH: rehash only the written pages of a streamed texture (H)
- **Evidence.** Race windows: "textures: 57,797-69,877 checks proven, 439-926 hashed" per 10 s (renut log), each hash
  over the whole texture when one of its pages' cards shows a write; the render thread's `ReadLevel` carries 35 leaf
  samples (0.38 ms) of memcpy; the LZX decoder (M-29) writes streamed data during the race.
- **Mechanism.** Per-page hashes kept with the texture; a written page is hashed alone; unchanged pages are proven by
  their stored hash; re-upload of the changed rows only where the format's tiling allows.
- **Exactness.** H, exact as today's hash-and-compare (same pages proven equal or re-read).
- **Gain.** **0.1-0.3 ms (0.1-0.2 %), conf L.** **Effort** M. **Risk** low-medium (tiled formats span pages).
- **Verify.** Frame hashes; `banjo_renut_store_cards_verify`.

### M-34 REN-DRAW-MERGE: merge consecutive draws that differ only in index range (E for pixels, census first)
- **Evidence.** A race frame has 925-1,064 draws (`2026-11-12_drawlog/renut_032.log`, frames 3765-3766), ~230,000 per
  10 s; the render thread spends ~13.5 ms, ~13 us a draw. The draw log shows repeated (vs, ps, target, blend) runs
  (e.g. "3 x3" rows at draws 3-5).
- **Mechanism.** Census of consecutive draws with identical state whose index ranges are contiguous in one buffer;
  merge them into one `drawIndexed` at record time.
- **Exactness.** E for the rendered result when state and buffers are identical and the ranges contiguous (verified by
  frame hashes); D if anything order-dependent (blending within a merged draw keeps primitive order, so it is the same).
- **Gain.** **0-2 ms (0-1.3 %), conf L** (the census decides; FastDrawLocked's epoch path already makes identical-state
  draws cheap). **Effort** L. **Risk** medium.
- **Verify.** Census; frame hashes; fast-draw verify.

## G. memcpy and memset (the rest is under M-03, M-08, M-22, M-31)

### M-35 REN-RESOLVE-NARROW: a resolve keeps the registers it reads, not the whole device (H)
- **Evidence.** `QueueResolve` copies the whole device snapshot per resolve (`queued.cpp:980`, `args->state.assign(device,
  device + kDeviceSnapshotSize)`, 12.9 KB); `lrleaf` puts 17 of GM's leaf samples (~0.18 ms, memcpy) under `Resolve`.
- **Mechanism.** `ResolveStateLocked` reads the surface, colour/depth info, viewport/scissor and MSAA words: copy those
  chunks (or a delta against the chain) and give it a narrowed view.
- **Exactness.** H, exact if `ResolveStateLocked` reads nothing else (a verify mode: run it on both and compare its
  emitted commands).
- **Gain.** **0.1-0.2 ms (0.06-0.13 %), conf M.** **Effort** S-M. **Risk** low.
- **Verify.** The verify mode; frame hashes.

## H. Kernel / HLE and synchronisation

### M-36 XMA-DISABLE-BOUNDED-WAIT: XMADisableContext waits for one frame, not a whole decode (H)
- **Evidence.** The lock path (`xma_decoder.cpp:567-594`) takes the context's mutex in `Disable()` and again in
  `Block(false)`; the guest's audio jobs wait **365-690 ms per 10 s** for it, up to 22-43 ms at a time (renut log "[xma]
  ... locks ... waiting"): the worker holds the context for its whole multi-frame decode.
- **Mechanism.** A per-context "disable requested" flag the worker checks between frames, releasing the context at the
  next frame boundary (the hardware stops at a block boundary too).
- **Exactness.** H: the context ends in a state the decode reaches (decoded up to frame k), as with today's timing
  variations.
- **Gain.** CPU **0.1-0.3 ms (0.06-0.2 %), conf L** (fewer blocking hand-offs); mainly latency on JQ3/4. **Effort** M.
  **Risk** medium (the worker's StoreContextMerged must still run once).
- **Verify.** The lock-wait line; audio counters; listening.

### M-37 HOST-TICKS: the tick counter instead of steady_clock in the hot host paths (H)
- **Evidence.** `steady_clock::now()` 0.15 ms in all (14 samples, all entry-located), called per kick and lock
  (`xma_decoder.cpp:521, 572`) and per decoded frame for a 10 s report check (`xma_context.cpp:493-501`);
  `UpdateGuestClock` 0.07, `clock_gettime` 0.04.
- **Mechanism.** `armGetSystemTick()` (cntvct, 52 ns per the profile header) for measurements and the report timer; the
  report check once per Work sweep, not per frame.
- **Exactness.** H, exact (measurement only).
- **Gain.** **0.1-0.2 ms (0.06-0.13 %), conf M.** **Effort** S. **Risk** none.
- **Verify.** Console CPU table.

### M-38 TIMESTAMP-ON-DEMAND: the 1 kHz timestamp timer (D unless the game never reads it)
- **Evidence.** `xboxkrnl_module.cpp:191-209`: a repeating 1 ms host timer updates `KeTimeStampBundle`; 1,085-1,118
  ticks a second in the race; the TimerQueue thread is 0.49-0.7 ms a frame, plus the wakes' kernel time (point 5).
  `kernel_timestamp_interval_ms` (`:41`) exists.
- **Mechanism.** First find out whether Blur reads the bundle (the variable import's address in the generated code /
  a PC census); if not, stop the timer (H, free); if it does, an A/B of 2-4 ms.
- **Exactness.** H if unread; D otherwise (coarser tick count).
- **Gain.** **0.1-0.4 ms (0.06-0.25 %), conf L.** **Effort** S. **Risk** low.
- **Verify.** Census; A/B with the existing cvar.

### M-39 HOST-COUNTERS-LOCAL: diagnostic counters out of the hot paths' shared atomics (H)
- **Evidence.** Per-kick and per-lock relaxed `fetch_add`s and a CAS max (`xma_decoder.cpp:554-559, 589-593`), per-frame
  cache counters under the cache mutex, Renut's per-draw source counters; the A57 has no LSE, each is an
  `ldxr/stxr` loop on a line several cores write.
- **Mechanism.** Thread-local counters summed at report time; compiled out in the player configuration where the report
  is off.
- **Exactness.** H, exact.
- **Gain.** **0.05-0.2 ms (0.03-0.13 %), conf L.** **Effort** S-M. **Risk** none.
- **Verify.** Console CPU table.

### M-40 GM-DRAW-LOCK-BATCH: the Game Master's per-draw locks per batch (H)
- **Evidence.** GM: `mutexLock` 0.17, `mutexUnlock` 0.12, `pthread_mutex_lock` 0.05 ms; `QueueDraw` takes
  `g_delta.mutex` per draw (`queued.cpp:850-852`) and `render_thread::Push` its own lock per item; ~1,000 draws a frame.
- **Mechanism.** Keep the chain lock across consecutive draw hooks of one device (until a non-draw hook or N draws), and
  push captures to the render queue in small batches under one lock.
- **Exactness.** H, exact (same order). **Not** SASRT 5 (lock-free queue, dropped as already batched on the consumer
  side): this is the producer side.
- **Gain.** **0.1-0.2 ms (0.06-0.13 %), conf L.** **Effort** M. **Risk** low-medium (latency of the first draw of a
  batch).
- **Verify.** The deltas verify key; console.

## I. Scheduling across the four cores

### M-41 XMA-KICK-COALESCE: one worker wake per burst of kicks (D, timing)
- **Evidence.** ~5,400 kicks a second, each `work_event_->Set()` (`xma_decoder.cpp:531`) waking the worker on core 3
  for one context ("worker 52,324 contexts in 3,779 ms" per 10 s: 72 us a context); the switches' kernel time is
  invisible to the sampler (point 5); visible: `PosixEvent::Set` 0.15, the worker loop 0.13, `Work` 0.28 ms.
- **Mechanism.** The kick marks pending and wakes the worker only if no wake is pending and the worker is parked;
  optionally a 100-200 us coalescing delay after the first kick of a burst (the game's audio job issues several per
  callback).
- **Exactness.** D (decode starts up to ~0.2 ms later; the game polls for done).
- **Gain.** **0.3-1.0 ms (0.2-0.65 %), conf L** (rests on the unmeasured switch cost: M-44 first). **Effort** S-M.
  **Risk** medium (audio starvation if the delay is too long).
- **Verify.** M-44's switch counts; "requests without data"; listening.

### M-42 JOB-IDLE-NO-SPIN: the job workers' yield loop skipped when the cores are full (D, timing)
- **Evidence.** ~4,500 `NtYieldExecution` a second (rex_profile "svcSleepThread: yield 3,916-5,590"), from the job
  queue's pop (`sub_8227CCE8` -> `__imp__sub_8227CC58` -> `sub_829BB640` -> NtYieldExecution, `n_8227CCE8.h:7`,
  `renut_recomp.137.cpp:29237`); 8227CCE8 0.70 ms and its copy loop 8227CA08 0.73 ms of CPU, plus kernel time.
- **Mechanism.** The HLE of the guest's yield wrapper counts consecutive empty pops per worker and, past a small
  count, goes straight to the event wait the pop path already has (`sub_8223D080` -> NtWaitForSingleObjectEx), so a
  worker with nothing to do stops spinning.
- **Exactness.** D (wake latency of an idle worker). **Not** CG-04 (spin before wait: the opposite direction).
- **Gain.** **0.3-0.8 ms (0.2-0.5 %), conf L-M.** **Effort** S-M. **Risk** medium (a job that arrives while the worker
  parks waits for the wake).
- **Verify.** Yield counter; console A/B (fps, JQ ms).

### M-43 AUD-PAIRED-CALLBACKS: run the audio callbacks two at a time (D)
- **Evidence.** Point 3: the audio engine's node states are cold at every 5.3 ms callback (~2.9 ms of entry samples in
  the audio family, more inside); the SDK pumps one 256-sample block per callback (`switch_audio_system.cpp:69`,
  `kPumpIntervalNs`).
- **Mechanism.** The pump calls the guest's render client twice back to back every 10.7 ms (one more block of output
  buffering): the second traversal finds the states the first left in the 2 MB L2.
- **Exactness.** D: +5.3 ms audio latency; the game's parameter updates land at 10.7 ms granularity.
- **Gain.** **0.5-2.0 ms (0.3-1.3 %), conf L.** **Effort** M (the client contract: the guest must be ready for a second
  call at once). **Risk** medium (guest timing assumptions).
- **Verify.** Console A/B of JQ3/4 ms and the entry-sample shares; audio counters; listening.

### M-44 SWITCH-CENSUS: count what the sampler cannot see (measurement)
- **Evidence.** Point 5; the profiler samples user PCs only.
- **Mechanism.** Per thread per 10 s: voluntary waits, wakes received, yields, sleeps (counters at the SDK's wait and
  wake wrappers), and the thread's CPU time from the kernel against its sampled busy share: the difference is kernel
  time. One race.
- **Exactness.** H, measurement. **Gain** none itself; it prices M-41, M-42, M-38 and M-43.
- **Effort** S. **Risk** none.

## J. Compiler and link

### M-45 EARLY-RA-SHARDS: GCC's early register allocator back on the shards it does not miscompile (E if clean)
- **Evidence.** `-mearly-ra=none` for everything (`app/CMakeLists.txt:12-29`) because of Peggle 2's miscompile of the
  two-register `tbl` (`vperm`); the pass exists for FP/SIMD allocation. q/d spills 1.7 % of guest samples (point 4).
- **Mechanism.** `-mearly-ra=all` per source on the bulk shards, kept only where the existing check (no `tbl vA, {vB},
  vB`; the 159-lookup routine complete) passes on the built object.
- **Exactness.** E if no miscompile; the risk is exactly a miscompile the PC cannot see (clang builds the PC).
- **Gain.** **0.2-0.8 ms (0.1-0.5 %), conf L.** **Effort** S (+ checks). **Risk** medium.
- **Verify.** The object check; forms self-check and a guarded console run; console A/B.

### M-46 NO-LOOP-DISTRIBUTE: no memset/memcpy calls invented in the copies (E)
- **Evidence.** M-22's card loops are one case of GCC's loop distribution (`-ftree-loop-distribute-patterns`, on at
  -O3) making a library call of a 1-2 iteration loop; the copies hold 350 `bl memset` sites, 214 not card ranges.
- **Mechanism.** `-fno-tree-loop-distribute-patterns` on the bulk shards (the large zero runs are explicit
  `REX_DCBZ` / `dc zva` code, not loops), checked by counting the calls.
- **Exactness.** E.
- **Gain.** **0.05-0.3 ms (0.03-0.2 %) beyond M-22, conf L.** **Effort** S. **Risk** low (a big loop may get slower:
  check).
- **Verify.** objdump counts; console.

### M-47 FFMPEG-STRICT-ALIASING: FFmpeg as upstream builds it (H, exact)
- **Evidence.** `sdk/CMakeLists.txt:142-143` adds `-fno-strict-aliasing` (and `-ffp-contract=off`) to every SDK target;
  the PGO-use build.ninja shows all 212 libavcodec/libavutil objects with it. FFmpeg is written for strict aliasing
  (`av_alias` unions for its punning). In `decode_frame`'s coefficient loop a field is stored back every iteration
  (`str w0, [x19, #400]` at `+0x1674`) next to float stores GCC must assume can alias it.
- **Mechanism.** `-fstrict-aliasing` on the two FFmpeg targets (keep `-ffp-contract=off`: contraction would change the
  PCM, a decision). Depends on M-48: under LTO the flag may be merged program-wide.
- **Exactness.** H, exact (conforming code; PCM compare to prove it).
- **Gain.** **0.3-0.7 ms (0.2-0.45 %), conf L-M.** **Effort** S. **Risk** low-medium (a punning bug in FFmpeg's C would
  show as different PCM: the compare catches it; nfsmw-nx's xxHash story is the precedent).
- **Verify.** PCM dumps compared; console XMA ms.

### M-48 LTO-FLAG-SCOPE: find out what -fno-strict-aliasing on the LTO link does to host code (H/E)
- **Evidence.** The LTO link line repeats `-fno-strict-aliasing` (`app/CMakeLists.txt:680-681`); Renut's 45
  `banjo_renut_gpu` objects compile without it, the 45 `renut` app objects, the generated code and the SDK with it.
  GCC's manual says such options "are passed through to the link stage and merged conservatively for conflicting
  translation units"; GCC also keeps per-function optimisation options. Which applies to Renut's render code (13.5 ms on
  the render thread, ~5 ms recording on JQ) is not known from here.
- **Mechanism.** Disassemble one Renut hot function from a build with and one without the link flag; if the link
  flag wins, keep `-fno-strict-aliasing` per object (the generated code's and the app's) and drop it from the link line,
  so Renut (and FFmpeg, M-47) keep strict aliasing in LTRANS.
- **Exactness.** E if the host code is clean (verify: frame hashes, fast-draw verify).
- **Gain.** **0-0.8 ms (0-0.5 %), conf L.** **Effort** S (the check). **Risk** low-medium.
- **Verify.** The disassembly diff first; then frame hashes and a console A/B.

## K. Anything else

### M-49 SECONDARY-VIEWS-HALF-RATE: the mirror and reflection passes every other frame (D)
- **Evidence.** The race draw log (frames 3765-3766): **214-340 of 925-1,064 draws (23-32 %)** go to 512x256 and
  256x256 targets (130 + 84; 131 + 131 + 78 draws), besides 422-440 in the 1280x720 scene; the 512x256 view is the rear
  mirror (resolved as a 512x64 strip, progress-log 2026-11-13 23:00 row); the reflection target already alternates
  between frames. Every draw costs on the GM (D3D calls, captures), the recording jobs and the render thread (~13 us a
  draw there).
- **Mechanism.** Find the guest call that renders the mirror pass (from the pass's vs/ps hashes in the draw log to the
  D3D calls' callers) and skip it on odd frames, keeping last frame's texture.
- **Exactness.** D (the mirror updates at half the frame rate).
- **Gain.** **1-4 ms (0.65-2.6 %), conf L** (draw-proportional CPU ~20-25 ms a frame x ~12 % for half the mirror pass;
  plus the scene jobs' culling for that view, unmeasured). **Effort** M. **Risk** medium (game state tied to the pass).
- **Verify.** Draw counts per frame; console A/B; looking at the mirror.

### M-50 SAMPLER-TAX: measure what the 1 ms stack sampler costs the measured races (measurement)
- **Evidence.** Every A/B so far ran with `switch_profile_stacks = true` (configs/renut_profile.toml via
  `make_blur_tomls.py:188`); the sampler pauses one thread per millisecond (`svcSetThreadActivity` paused ->
  `svcGetThreadContext3` + an x29 walk -> runnable, `switch_perf.cpp:1071-1115`); the player tomls have it off
  (`make_blur_tomls.py:140, 160`).
- **Mechanism.** One race pair with stacks on and off (Renut's wall-clock fps, same session); if the tax is >= 1 %, the
  30-fps target should be judged on the off figure and the sampler run at 4 ms for A/Bs.
- **Exactness.** H, measurement. **Gain** none for players (they do not pay it); it corrects the baseline by an amount
  I estimate at 0.5-1.5 % (thread pauses are tens of us each). **Effort** S. **Risk** none.

---

## 3. All fifty at a glance

Gains: ms of CPU a frame today (and % of 155), range; conf H/M/L; effort S (≤ 1 day), M (2-5 days), L (1-3 weeks),
XL (months). "a" = audio-paced (x0.74 at 30 fps).

| ID | title | class | gain ms | % of 155 | conf | effort |
|---|---|---|---|---|---|---|
| M-01 | XMA one-frame catch-up | H | 2.0-3.0 a | 1.3-1.9 | M | S-M |
| M-02 | XMA pair-keyed cache | H | 0.3-1.5 a | 0.2-1.0 | L | M |
| M-03 | XMA dead fills | H | 0.3-0.6 a | 0.2-0.4 | M | S |
| M-04 | XMA flat cache (alloc/lock/hash) | H | 0.8-1.4 a | 0.5-0.9 | M | M |
| M-05 | XMA codec reset instead of reopen | H | 0.2-0.4 a | 0.1-0.3 | M | S-M |
| M-06 | XMA quantiser table | H | 0.4-0.7 a | 0.3-0.45 | M-H | S |
| M-07 | XMA cached bit reader, VLC pair load | H (+D) | 0.2-0.45 a | 0.1-0.3 | L-M | S |
| M-08 | XMA direct output | H | 0.15-0.3 a | 0.1-0.2 | M | S-M |
| M-09 | XMA cache eviction | H | 0.3-1.0 a | 0.2-0.65 | L | M |
| M-10 | XMA warm cache (SD / load screen) | H, user OK | 0.3-1.0 a | 0.2-0.65 | L | M |
| M-11 | XMA decode window | D | 0-1.0 a | 0-0.65 | L | M |
| M-12 | XMA idle pre-decode | H | 0-1.0 a | 0-0.65 | L | L |
| M-13 | stereo speaker config | D | 0 or 5-8 a | 0 or 3-5 | L | S |
| M-14 | filter channels in NEON lanes | E | 0.3-1.0 a | 0.2-0.65 | L | L |
| M-15 | one native call per node | E | 0.1-0.4 a | 0.1-0.25 | L | M |
| M-16 | host-endian DSP rows | E\* | 0.5-1.5 a | 0.3-1.0 | L | L-XL |
| M-17 | native graph walker | E | 2-4 a | 1.3-2.6 | L | XL |
| M-18 | voice LOD | D | 0-3 a | 0-2 | L | M |
| M-19 | next-node prefetch (audio) | E | 0.7-1.5 a | 0.45-1.0 | M | S-M |
| M-20 | learned jump pointers | E | 0.2-0.6 | 0.1-0.4 | L | M |
| M-21 | callee-entry prefetch | E | 0.6-1.5 | 0.4-1.0 | L | M |
| M-22 | natives' card marks inline | E | 0.4-0.8 | 0.25-0.5 | M | S |
| M-23 | float loads/stores after CG-13 | E | 0.2-0.5 | 0.1-0.3 | M | S |
| M-24 | fmadds known-single | E | 0.1-0.25 | 0.1-0.15 | M | S |
| M-25 | promoted targets in the closure | E | 0.2-0.5 | 0.1-0.3 | L-M | M |
| M-26 | vector PCS for guest functions | E | 0.2-0.6 | 0.1-0.4 | L | M |
| M-27 | hot loops outlined | E | 0.5-1.5 | 0.3-1.0 | L | L |
| M-28 | Windows-only mentions out of the copy rule | E | 0.2-0.35 | 0.1-0.2 | M | S |
| M-29 | LZX decoder as a native | E | 0.8-1.2 | 0.5-0.8 | M | L |
| M-30 | LZX block cache | E | 0-1.0 | 0-0.65 | L | M |
| M-31 | per-device delta chain | E | 0.4-0.8 | 0.25-0.5 | M | M |
| M-32 | extent memo ways | H | 0.05-0.15 | 0.03-0.1 | M | S |
| M-33 | per-page texture hashes | H | 0.1-0.3 | 0.1-0.2 | L | M |
| M-34 | draw merging | E | 0-2 | 0-1.3 | L | L |
| M-35 | narrowed resolve snapshot | H | 0.1-0.2 | 0.06-0.13 | M | S-M |
| M-36 | bounded XMADisableContext wait | H | 0.1-0.3 a | 0.06-0.2 | L | M |
| M-37 | tick counter for host timing | H | 0.1-0.2 | 0.06-0.13 | M | S |
| M-38 | timestamp timer on demand | H/D | 0.1-0.4 | 0.06-0.25 | L | S |
| M-39 | thread-local diagnostic counters | H | 0.05-0.2 | 0.03-0.13 | L | S-M |
| M-40 | GM draw locks per batch | H | 0.1-0.2 | 0.06-0.13 | L | M |
| M-41 | XMA kick coalescing | D | 0.3-1.0 a | 0.2-0.65 | L | S-M |
| M-42 | job workers stop spinning | D | 0.3-0.8 | 0.2-0.5 | L-M | S-M |
| M-43 | paired audio callbacks | D | 0.5-2.0 a | 0.3-1.3 | L | M |
| M-44 | switch census | measurement | 0 | 0 | - | S |
| M-45 | early RA on clean shards | E | 0.2-0.8 | 0.1-0.5 | L | S |
| M-46 | no loop-distribution calls | E | 0.05-0.3 | 0.03-0.2 | L | S |
| M-47 | FFmpeg strict aliasing | H | 0.3-0.7 a | 0.2-0.45 | L-M | S |
| M-48 | LTO flag scope | H/E | 0-0.8 | 0-0.5 | L | S |
| M-49 | mirror pass at half rate | D | 1-4 | 0.65-2.6 | L | M |
| M-50 | sampler tax | measurement | 0 (baseline) | - | L | S |

**Honest total.** The exact and host-only ideas, all done and none overlapping, add to roughly 13-20 ms on paper; with
the overlaps taken out (M-01/M-02/M-09/M-10 share the XMA misses; M-15/M-16/M-19 are parts of M-17; M-19/M-21/M-43 share
the entry misses; M-22/M-46 share the memset calls) about **9-13 ms**, and at the last proposer's audit ratio expect
**5-8 ms (3-5 %)**, a little over half of it audio-paced. The XMA pile alone (M-01..M-08) is the most certain: about
4-6 ms of the XMA thread's 14.5. The decisions (M-13, M-18, M-41-43, M-49) are where anything larger can come from;
M-13 is the cheapest experiment in this list.

## 4. Top 10 by expected gain per effort

| rank | ID | why first | first step |
|---|---|---|---|
| 1 | **M-01** XMA one-frame catch-up | ~40 % of FFmpeg decodes thrown away, measured; a short exactness argument; days | the dual-decoder verify key, then change the catch-up loop (`xma_context.cpp:513-527`) |
| 2 | **M-06** XMA quantiser table | 0.67 ms of two `fdiv` + exp2 per band, measured; exact by construction; hours | table + bitwise test over the range |
| 3 | **M-03** XMA dead fills | three fills per Decode overwritten before use; hours | remove, compare `audio_dump_xma_s` dumps |
| 4 | **M-22** natives' card marks | 136 card-range `memset` calls in the copies, 0.64 ms in memset's small path; hours | `Card()` as two `rex_card_mark_` tests; store-cards verify |
| 5 | **M-13** stereo speaker config (D) | one cvar decides whether a third of JQ3/4's mixing exists for nothing | a PC channel census, then one console race |
| 6 | **M-19** next-node prefetch | ~2.9 ms of first-touch samples in the audio family; three loops | prefetch at 8230AD50's node loop first; entry.py before/after |
| 7 | **M-47** FFmpeg strict aliasing (with M-48's check) | a build flag against upstream's own setting; exact by PCM compare | the LTO disassembly check, then the flag |
| 8 | **M-04** XMA flat cache | ~2.4 ms of malloc/free/locks/frame plumbing on the XMA thread | custom `get_buffer2` and the arena |
| 9 | **M-23** float loads/stores after CG-13 | 678 integer-path float accesses in the 60 hottest copies; an existing pass's exclusion now obsolete | extend float_loads.py under `BLUR_FPCR_ONE_MODE` |
| 10 | **M-28** Windows-only mentions | 0.66 ms of generated code kept from copies by a PC diagnostic | scope the tool's mention scan |

Next in line: M-31 (Begin*Vertices whole copies, measured, a switch exists), M-29 (LZX native, larger but well
bounded), M-42 (yield spinning, a decision), M-44 (the census that prices the scheduling ideas).

**Genuinely big (≥ 3 ms a frame):** M-01 at the top of its range (its ceiling is ~3.4 ms); M-13 if Blur's engine adapts
to a stereo configuration (unknown until the test); M-17 (a months-long native audio graph, 2-4 ms); M-49 (a visible
decision, 1-4 ms). Taken together the XMA ideas M-01..M-08 are ~4-6 ms of one thread. Nothing else here is credibly
above 1.5 ms on its own.
