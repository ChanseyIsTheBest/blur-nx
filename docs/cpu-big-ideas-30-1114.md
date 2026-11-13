# 30 big, risky CPU ideas (#100-#129, 2026-11-14)

The user (01:11): "Can you come up with 30 big risky challenging ideas to cut cpu time". Written in the main chat; none
repeats the near-exact 50 and its refills (#1-#69, docs/cpu-near-exact-50-1113.md), the 30 of 2026-11-13 night
(#70-#99: physics / mixer NEON natives, callee-saved registers in host registers, voice cap, half-rate quiet voices,
large pages, command-buffer reuse, the Game Master's waits, multi-track PGO, AI every other frame, tiny-function
inlining, audio / render core moves, fewer substeps when late, LZX ahead, off-screen particles, reverb half rate,
small memcpy natives, audio memset, uploads into the ring, redundant binds, XThread's 822D2378, NewOp CRC32, -O3,
GM job preference, two XMA frames a wake, critical-section spin, dcbt, NEON culling, 512-sample audio frames), the 50
moonshots (docs/cpu-moonshots-50-1113.md) or the CG / VX lists.

Where the time is (console, stock clocks 1020 / 460 / 1600 MHz, the c2b build, 2026-11-14 clean-card races): ~25 fps
= ~40 ms a frame; GPU ~28 ms a frame, so the frame is CPU-bound (the chain: Game Master + JQ0-2; the Game Master
waits ~38 % of its time on job groups). All threads ~117 ms of CPU a frame, the machine copies 84 ms of it; ~10
physics / vector functions ~16 ms; the audio mixer ~11 ms (JQ3 / JQ4). Past ~12 ms cut from the chain the GPU (28 ms)
becomes the limit, so the ideas that cut draws (#100, #101, #102, #122, #123, #127, #128) count twice: CPU and GPU.

Exact: same output. Timing: same results, different thread timing. Visual / audio / gameplay: output changes (the
user's decision). Estimates are ms a frame on the chain unless noted, unmeasured unless noted.

| # | Idea | Where the time goes | Est. saving | What can go wrong | Exact? | Difficulty |
|---|---|---|---|---|---|---|
| 100 | Single player with the split-screen detail settings (Blur's 2-4 player modes have their own LOD / draw-distance / effects tables) | culling, the game's draw submission, Renut's recording (~1,700 draws a frame) | 3-8 (+GPU) | shorter draw distance, LOD pops; the tables may be keyed per viewport | Visual | Medium-hard (find the per-viewport-count settings) |
| 101 | The car-reflection map (if dynamic) updated at a fraction of the rate, one face a frame | a whole scene resubmission per face | 2-6 (+GPU) if it exists | reflections lag; first a census of render-target passes a frame | Visual | Medium |
| 102 | Shadow map: static casters cached, only cars redrawn (Renut-level, by buffer identity) | the shadow pass's draws | 1-4 (+GPU) | wrong shadows when the cascade moves without snapping | Visual if wrong | Very hard |
| 103 | Far, unseen AI cars on rails (kinematic path following; full physics when near or visible) | AI + physics on JQ0-2, per car | 3-8 | race outcomes change; handover glitches at the switch | Gameplay | Very hard (find the per-car update) |
| 104 | Wheel ground-contact raycasts reused for 2-3 frames while a car moves little | collision queries (20 cars x 4 wheels x substeps) | 1-3 | handling changes over bumps and edges | Gameplay | Hard |
| 105 | The HUD's (Flash / Scaleform-type) movie advanced at half rate, drawn every frame | the UI VM on the Game Master | 0.5-3 if it is Flash | HUD animations at 15 Hz | Visual | Medium (find the advance call) |
| 106 | The job system's six guest hardware-thread workers folded onto three host workers (worker count patched at init) | context switches, contention, cache misses | 0-3 | per-hardware-thread arrays or affinities assumed: deadlock | Timing | Hard |
| 107 | The whole job system as a host native: per-worker lock-free deques, work stealing, futex wakes (sub_8227CF60 / CC58 / E098 / 862B0) | the GM's group waits, the workers' yield loops | 1-4 | job semantics (groups, dependencies) must match exactly | Timing | Very hard (#34 / #58 were slices) |
| 108 | Tiny jobs (< ~20 us) run inline on the pushing thread when it will wait for the group anyway | wake latency per job | 0.5-2 | a job that blocks on another job: deadlock | Timing | Hard |
| 109 | Renut's translation (NewOp, set_words, captures) on a host thread on core 3; the game threads only append raw packets plus snapshots of what they reference | recording on JQ0 / JQ1 | 2-5 | a referenced range missed in the snapshot = a wrong frame | Exact if complete | Very hard |
| 110 | Vertex / index data read by the GPU in place (Renut's address window: 0 draws use it now), the byte swap in the vertex shader | the stream cache's swap-copies | 0.5-2 | a buffer written while the GPU reads it (needs its write tracking) | Exact | Medium-hard (the path exists) |
| 111 | MMU write-watch instead of card marks: GPU-visible guest pages read-only after each snapshot, the first write faults, marks, unprotects | the mark instructions in every store of the copies (10 G run marks in a PC race) | 2-6 (all threads) | fault cost if many pages a frame; heap permission changes on Horizon; resuming from the handler | Exact | Very hard |
| 112 | The copies built with Clang / LLVM (devkitA64's sysroot) instead of GCC | scheduling and register allocation on huge functions | -2 to +8 % | toolchain bring-up, mixing with GCC objects, a new PGO format | Exact | Hard |
| 113 | BOLT-style post-link layout of the NRO from the console's sampled PCs (llvm-bolt, --emit-relocs) | I-cache / iTLB misses across 50+ MB of copies | 2-6 % | BOLT on a static AArch64 ELF with hand assembly | Exact | Hard |
| 114 | Render and simulation pipelined: guest heap pages forked copy-on-write at frame end, the game's render traversal for frame N on another thread while N+1 simulates | the render traversal's share of the chain | 5-12 | any write the fork misses = wrong frame; the moonshot of moonshots | Exact if complete | Extreme |
| 115 | The guest stack kept little-endian (no swaps on r1-relative accesses; sub-word accesses address-XORed) | byte swaps on stack loads and stores | 1-3 % of copies | a stack buffer handed to the kernel, a file read or the GPU; mixed widths | Exact if no escape | Hard |
| 116 | The whole audio graph mixed at 24 kHz, upsampled at the output | half the mixer (JQ3 / JQ4) | 3-5 (off the chain) | the top octave lost | Audio | Medium-hard |
| 117 | The audio graph in a GPU compute shader | most of the mixer's ~11 ms | 5-10 (off the chain) | latency; the GPU has ~30 % headroom only while CPU-bound | Exact-ish | Very hard |
| 118 | The render-queue sort seeded with last frame's order (nearly sorted: insertion / merge) | a per-frame qsort of draw keys, if there is one | 0.3-1.5 | ties reorder (opaque: invisible) | Exact if stable | Medium (find the sort) |
| 119 | Argument-value specialization: hot copies cloned for their most frequent constant arguments, behind a guard | branches and loops the constants fold | 1-3 % | the guard's own cost where values vary | Exact | Medium-hard (census + tool) |
| 120 | False-sharing census (lines written by two cores: queue heads, counters, reference counts) and padding of those objects | cache-line ping-pong between JQ0-2 | 0.5-2 | layouts the game assumes | Exact | Hard |
| 121 | Guest dispatcher objects (events, semaphores) entirely in guest memory: atomics + RexSwitchWaitIfEqual, no XObject / handle table / host mutex (extends #57) | kernel waits and sets (~8,000 sets a second) | 0.5-2 | APCs, alertable and multi-object waits, timeouts | Exact | Hard |
| 122 | Renut-level occlusion: a draw whose last-frame query drew 0 samples is skipped (re-tested every N frames) | recording + GPU per hidden draw | 1-3 (+GPU) | one-frame pop-in | Visual | Very hard |
| 123 | AI cars beyond N m at their lowest LOD (fewer draws a car: body, wheels, glass, lights, decals) | per-car draws | 1-3 (+GPU) | visible LOD switch | Visual | Medium |
| 124 | An offline visibility table per track segment (built on the PC), fed to the game's static culling | static-object culling | 1-3 | must be conservative; per-track data | Exact if conservative | Very hard |
| 125 | fmul + fadd pairs contracted into fmadd across the copies (-ffp-contract=fast semantics) | float-heavy copies (physics) | 1-3 % | last-bit differences grow in physics | Gameplay (tiny) | Easy-medium |
| 126 | Scalar fdiv / fsqrt in the physics copies as estimate + one Newton step | A57's non-pipelined divides | 0.5-2 | precision loss in physics | Gameplay (tiny) | Medium |
| 127 | Fewer AI cars a race (20 -> 12, the grid size) | AI, physics, draws, audio voices, all per car | 5-10 (+GPU) | a game-design change | Gameplay | Easy-medium (find the grid size) |
| 128 | CPU-side dynamic detail: past the 33 ms budget the LOD bias / draw distance steps down, back up with headroom (#100 / #123's knobs) | as #100 | 2-6 | visible detail changes under load | Visual | Medium-hard |
| 129 | The collision broadphase kept incrementally (persistent sweep-and-prune) instead of rebuilt each frame | broadphase rebuild | 0.5-2 | must give the same pair set | Exact | Hard (find it) |

## Where to start (cheapest first look, biggest payoff)

1. A PC census of Renut's passes a frame (render-target switches, draws per pass): tells at once whether #101 and
   #102 exist and how big #100 / #123 / #127 are. No build of the game code, a log read.
2. #100 / #127: find the split-screen settings and the grid size in the guest code (a reading); one toml key each.
3. #111 and #113: the biggest exact ones (card marks and code layout).
