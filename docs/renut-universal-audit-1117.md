# Renut universal audit (2026-11-17): 32 CPU ideas, 6 GPU extras

The user (2026-11-17, during the 24-hour run): "I believe an audit of universal things in the renut codebase for speedups
like removing locks is in order because codegen and machine copies are getting a bit stagnant"; then "I want at least 30
unique CPU ideas if you have GPU ideas come up with them as extras".

**Source.** Batch 2 (main 42f866c2), the three two-scene races of the morning (1117_b2_2scene_1..3), racing windows 9-20
of rex_profile.log (scratchpad host_top.py, stack_race.py), the 10 s counter lines, and renut_105.log's GPU profile and
hand-over lines. Percentages are of that thread's samples. Each idea was checked against the base's keys and
docs/off-by-default.md so that nothing already done or already measured is proposed again.

**Where things stand.**
- The base already took the big lock and wake costs: lock-free guest records (port 47), events waiting on an address
  (port 65), the render queue's lock-free ring (port 105), lock-free mutant releases (port 146), notifies skipped with
  no waiter, and batch 2's lock-free binds.
- Renut's own code is now about 3-4 % of the Game Master (the critical thread). The rest of it is guest code (about
  60 %) and the job wait (24 % yielding).
- The render thread runs about 46 % of core 1 at priority 0x2C, above the game threads' 0x3B. Every millisecond it
  saves goes to the job threads that share that core.
- The GPU is at 28-29 ms of the 32.3 ms frame, so the GPU extras matter soon.

Kinds: **A/B** = a toml change only (no build); **fix** = a build; **census** = a counter first, then the fix it points to.

## CPU ideas

| # | Idea | Where | What the profile shows | Exact | Kind |
|---|---|---|---|---|---|
| 1 | Ship with the stack sampler off | dist renut.toml `switch_profile_stacks = true` (the base's default is false) | pauses one game thread every 5 ms (`svcSetThreadActivity`) to read its pc, in players' builds too | yes | A/B |
| 2 | Ship with the per-shader GPU timestamps and the GPU profile off | `banjo_renut_diag_gpu_shaders = true`, `renut_gpu_profile = 10` | a timestamp at every pixel-shader change, written by the render thread; its own comment says it costs render-thread and GPU time | yes | A/B |
| 3 | Ship with the other diagnostics off | `banjo_renut_diag_resolve_trace`, `banjo_renut_diag_terrain` (N&B's census), the 10 s report lines | a traced frame every 30 s; censuses that count every draw | yes | A/B |
| 4 | A pool of retired host textures, reused by format, size and levels | render/textures.cpp `Create` / `Retire` | resolve destinations made anew: 65 GPU mappings/s, 24 ms/s in `MapBufferEx` (2.4 % of the render thread) from `ResolveTarget -> Create -> createTexture` | yes | fix |
| 5 | Find why ~2 resolve destinations a frame are made anew, and fix the cause | textures.cpp `ResolveTarget` (a new slot each time) | the remade-with-words log stays under 8, so it is a new slot or address each frame; the reason is not counted | yes | census |
| 6 | Rumble sent from a background thread (the game thread posts the latest value) | sdk input/switch `SetDeviceVibration` | `hidSendVibrationValues` IPC on the Game Master at every change: 0.46 % of it while racing | yes (≤1 frame later) | fix |
| 7 | Guest-records registry: `std::shared_mutex` to `std::mutex` (its reads are lock-free since port 47) | gpu/guest/guest_records.cpp | libnx's rwlock unlock wakes through a condition variable (a syscall): `pthread_rwlock_unlock` is 0.51 % of the Game Master from guest code and 0.19 % from `D3DResource_Destroy_hook` | yes | fix |
| 8 | In-place shader lock: lock-free reads, or a plain mutex | d3d/hooks/shader.cpp `g_in_place_mutex` | the engine's only other `shared_mutex`; batch 1's memo cut its reads, but misses and writes still pay libnx's rwlock | yes | fix |
| 9 | Object table: lock-free handle lookups | sdk object_table (`table_mutex_`, #56) | 14,193 lookups/s, 296 contended (each a Horizon hand-off) | yes | fix |
| 10 | Address waits for semaphores, mutants and timers (port 65 did events only) | sdk threading_posix/threading_switch | the guest's other threads sit 3-4 % of their samples in the condition mutex inside `PosixConditionBase::Wait`; the timer thread 4-5 % | yes | fix |
| 11 | Render thread's sleep and wake on an address, not `g_work_cv` | backend/render_thread.cpp | the Game Master spends 0.88 % in `notify_one` from `Push` (the render thread is idle 48 %, so it is woken often) | yes | fix |
| 12 | Render thread's done count as an atomic, `g_mutex` only for a waiter | render_thread.cpp (`g_items_done += n` under `g_mutex` per batch) | a lock and unlock per batch | yes | fix (small) |
| 13 | Job-queue event waits spin briefly before they park | sdk `NtWaitForSingleObjectEx` path | the job threads are parked 49 % of the time and woken at each job the Game Master posts | yes (CPU traded for wake latency) | fix + A/B |
| 14 | 4-core layout: render thread alone on core 1, the job threads masked off it | toml `switch_thread_cores` (port 109) | priorities were tried (no gain); masks have not been raced in Blur | yes | A/B |
| 15 | Kick submit re-raced now that the GPU is near the limit | toml `banjo_deko3d_kick_submit` | 159 kickoffs/s, 9.4 ms/s (0.9 % of the render thread); fewer kicks start the GPU later | yes | A/B |
| 16 | XMA kick without its lock, re-raced on batch 2 | toml `audio_xma_kick_without_lock` | `XmaDecoder::WriteRegister` 0.7 % each of JQ 3 and 4; last measured before batches 1-2 | yes | A/B |
| 17 | Per-stage cost counters in the replay's draw, then the top stages fixed | render/draw.cpp `FastDrawLocked`, `DrawStateLocked` | 21.5 % of the render thread (12.6 % + 6.0 % + 2.9 %); no line info in the ELF to split it further | yes | census |
| 18 | Which textures upload again each frame, and why | textures.cpp `Upload`, `RefreshBindingLocked` | 1.25 % + 0.56 % `Upload` and 1.03 % `RefreshBindingLocked` of the render thread | yes | census |
| 19 | Store cards proved once per range per interval (as `KickHeld` does) | draw.cpp `CardsProve` / `PagesClean` | 1.13 % of the render thread from `WholeGeometryTry`; check first whether its callers already remember a proof | yes | fix (check) |
| 20 | Per-item cost of the render loop | render_thread.cpp `Run` | 7.46 % of the render thread in `Run` itself (dequeue, prefetch, item call) | yes | census |
| 21 | Per-op cost of command-buffer recording | gpu/guest/command_buffers.cpp `NewOp` | 1.31 % running + 0.26 % waiting of each job thread | yes | census |
| 22 | What `BeginCommandBuffer` waits for | d3d/hooks/device.cpp (`BlockOnFence`) | 0.34 % of the job threads waiting inside it; if guest fences are signalled late, signal at the point the work is done | check | census |
| 23 | Which lock the guest code takes directly | a kernel shim inlined into guest code | `pthread_mutex_lock` called from guest code: 0.42 % of the Game Master | yes | census |
| 24 | Keys read once into a plain struct, not function-local statics | renut::gpu (232 guard variables; 20+ in `DrawStateLocked`) | a guard check (acquire load and branch) per key per call on the per-draw path | yes | fix (small) |
| 25 | Render-queue items as a fixed-size inline task, not `std::function` | render_thread.cpp `Item` | a heap allocation for any capture over 16 bytes, and an indirect manager call per item | yes | fix (small) |
| 26 | Command-buffer runs with an intrusive count, not `shared_ptr` | command_buffers.cpp `NewRun` | two atomic count changes per run; `QueueRunCommandBuffer` 0.28 % + 0.28 % of the Game Master | yes | fix (small) |
| 27 | Controller reads with no allocation, devices refreshed once a frame | sdk `InputSystem::GetState` | a `std::vector`, a recursive mutex and `RefreshDevices` per read | yes | fix (small) |
| 28 | Blur's resolve diagnostic only when its diagnostic is on | textures.cpp:1031 (`g_resolved_mutex` + `unordered_map`, "not in the base") | a lock and a map write per resolve on the render thread | yes | fix (tiny) |
| 29 | The libnx counting wrappers skip their clock reads when nothing reports | sdk ui/switch_perf.cpp `__wrap_*` | 2 tick reads and atomics per wrapped call (~7,000 yields/s, kicks, cache cleans) | yes | fix (tiny) |
| 30 | Per-resolve CPU setup trimmed | draw.cpp `ResolveTargetsLocked`, textures.cpp `ResolveTarget` | 1.68 % + 0.24 % of the render thread | yes | census |
| 31 | The 10 s reports built off the game and render threads | banjo_renut_tuning.cpp and the profile writer | large string formatting every 10 s on whichever thread reports; check which thread it is | yes | check |
| 32 | Event-style waits for the render thread's GPU slot | video.cpp `WaitSlotLocked` (21 % of the render thread waiting on a fence) | while it waits, nothing else runs on that thread; work it could do then (shader lookups, upload conversion for the next frame) waits too | yes | fix (larger) |

## GPU extras

GPU frame (one racing window of 1117_b2_2scene_1): 29.1 ms, of which resolves 5.46 ms, depth resolves 1.88 ms, SMAA edges
1.66 ms (weights 0.64, blend 0.43), and present with overlays 0.51 ms.

| # | Idea | What shows it | Exact | Kind |
|---|---|---|---|---|
| G1 | Colour hand-over kept on with SMAA (`sasrt_gpu_handover_with_smaa = true`) | Blur's colour hand-over leaves **0** resolves: it switches off while SMAA is on. ~7,900 whole same-size resolves per 10 s are counted under "format or kind", the bucket this gate falls into. It also lets the scan-out hand-over work (part of present's 0.51 ms) | yes (sasrt-nx) | A/B |
| G2 | The three tile resolves of the 1280x720 colour and depth merged into one whole copy each | 3 + 3 tile resolves a frame; whole copies are what the hand-over (G1) needs, and three passes become one | yes | fix |
| G3 | SMAA's edge pass as a compute shader with luma shared in groupshared memory | 1.66 ms, bound by 5 samples a pixel (smaa.cpp's own note); shared memory needs about 1.3 loads a pixel | yes | fix |
| G4 | Review the translated GLSL of the top full-screen shaders | PS EFE544B8F1999133 1.52 ms for 1 draw a frame; PS B20CDC973266527B (Blur's composite) 1.21 ms | yes | census |
| G5 | Depth resolves: a cheaper copy | 1.88 ms; the depth hand-over takes 376 per 10 s but draws all 376 after all (a depth write follows each one) | check | census |
| G6 | The shipping diagnostics' GPU side (CPU #2) | per-shader timestamps on the GPU too | yes | A/B |

## Implementation (batch 3, 2026-11-17)

The user (09:30): "work on all the CPU and GPU ideas and implement them and test on PC". Commits 24ecc433 (the CPU
batch) and b416ed69 (#10). Every change has its own key, so the console A/Bs can turn each one off.

| # | What was done | Key (default) |
|---|---|---|
| 1-3, G6 | toml only: shipping without the sampler, the per-shader timestamps, the trace and the terrain census; the A/B races decide | (toml) |
| 4 | render-target images given back by Retire are kept once their frame is done (96 MB / 48 images at most) and Create takes a matching one (video.cpp TakePooledLocked) | `blur_renut_texture_pool` (on) |
| 5 | why ResolveTarget makes a destination (7 reasons) and which target textures are released (memory, idle, cold), with the pool's counts, in a 10 s line | (always) |
| 6 | a rumble thread (priority 0x3C) sends the latest value per pad; a failed send is redone by the game thread's next call | `input_switch_rumble_thread` (on) |
| 7, 8 | gpu/lean_mutex.h: the guest-record registry's and the in-place shaders' locks as one std::mutex each | `blur_renut_lean_rwlock` (on) |
| 9 | the handle table's own lock tries `global_lock_spin` times (CPU yields) before it blocks | `kernel_handle_lookup_spin` (on) |
| 10 | semaphores wait on an address (port 65's core; a release hands units to blocked threads first); host stress test tools/test_address_semaphore.cpp passed | `thread_semaphore_address_wait` (off: A/B) |
| 11 | the render thread's ring sleep on an address (svcWaitForAddress / svcSignalToAddress), no g_mutex either side | `blur_renut_render_address_wait` (on; the Switch) |
| 12 | the ring's batch end counts and reads the done waiters as atomics, g_mutex only to wake one | `blur_renut_done_lockfree` (on) |
| 13 | an event wait watches the event N CPU yields before it blocks | `thread_event_spin` (0: A/B) |
| 14-16 | toml A/Bs (core masks, kick submit, the XMA kick's lock) | (toml) |
| 17-22, 30 | gpu/stage_census.h: the time and calls of FastDrawLocked, DrawStateLocked, FillTextures, the constants, BindTargets, setPipeline, BindStreams, WholeGeometryTry, the host draw, CardsProve, Upload, RefreshBindingLocked, ResolveTargetsLocked, each render-thread item, NewOp and BeginCommandBuffer's fence wait | `blur_renut_diag_stage_census` (off) |
| 19 | checked: every caller proves a range only once per kick (`proven_flush != g_flush_serial`); nothing to add | - |
| 23 | found: the guest function taking a mutex and an rwlock is Renut's own BeginVertices hook (sub_82242BC0): the rwlock is the in-place shader lookup (#8 makes it a mutex) and the mutex the deferred-draw chain (g_delta / g_deferred_mutex) | - |
| 24 | DrawStateLocked's 15 one-line key statics in one struct read at the first draw (one plain check a draw) | (always) |
| 25, 26 | a replay's closure is a pooled job (one pointer, stored in the std::function), a run's control block from a free list | `blur_renut_replay_pool` (on) |
| 27 | the controller calls copy the user's device list into a per-thread vector | (always) |
| 28 | Blur's CPU-lock-of-resolve diagnostic behind a key | `blur_renut_diag_cpu_lock_resolve` (off) |
| 29 | the libnx counting wrappers skip their clock reads and atomics when the title says so | `switch_perf_count_calls` (on) |
| 31 | the 10 s report's own time on the game thread logged at the next report (it runs in the Game Master's Swap, under g_stats.mutex) | (always) |
| 32 | checked: the render thread's 21 % in WaitSlotLocked is the GPU's back-pressure (2 frames in flight, the GPU at 28 of 32 ms): no CPU work removes it; the GPU ideas are the lever | - |

GPU extras:

| # | Finding |
|---|---|
| G1 | toml A/B (`sasrt_gpu_handover_with_smaa = true`). The whole same-size 1280x720 resolves (formats 10 and 20, about half of the resolve pixels in the trace) are among those SMAA's gate now draws |
| G2 | not built: the tiles are not back to back. Each tile's resolve is followed by its clear-after and target changes (the trace), so merging them needs each tile's clear-after deferred too, with every other op checked against the pending regions; at most the 1280x720 colour tiles' copy (~10 % of the resolve pixels) |
| G3 | not built as designed: the edge pass also writes the depth mask (D16) that lets the weights and blend passes skip most pixels, which a compute shader cannot write. A luma pre-pass (R32F, exact) with gathers in the edge pass would cut 5 RGBA reads to 1 + 2 gathers, ~0.3 ms at most |
| G4 | PS EFE544B8F1999133 (1.52 ms, 1 draw): 6 taps of a 64-bit texture a pixel (5.5 M taps a frame), at the texture units' half rate for that format: fetch-bound, nothing for an ALU pass |
| G5 | the depth hand-over takes ~1.2 depth resolves a frame and draws every one after all (a depth write follows): those copies are needed; the 1.88 ms (two screens' worth) is next to split by the stage census's resolve setup and a GPU split of decompress vs copy |

### PC checks (2026-11-17 10:06-10:29, the PC build of b416ed69)

- **audit_census** (420 s, every new key at its default, the stage census on): no errors, the same warning kinds as
  with the keys off; the race renders as before (captures at 60-360 s).
  - #4/#5: 1,196 resolve destinations made per 10 s (~2 a frame), every one for "another host format" (a texture at
    one address resolved into in two formats in turn); the pool served all of them (1,196 reused, 0 made new). The
    cause would need a destination per (address, format); with the pool each remake no longer makes an image.
  - #31: the 10 s report takes 0.49-0.53 ms on the game thread: nothing to move.
  - #17-#22, #30 on the PC: the fast draw path is not taken there (FastDrawLocked 0.04 us a call, DrawStateLocked
    7.2 us a draw at 45,000 draws a second), so the console's census is the one that counts. ResolveTargetsLocked costs
    17 us of CPU a resolve (7 a frame); NewOp 1.7 us an op (33,000 a second); Upload 28 us (119 a second).
- **audit_off** (300 s, the batch's keys off): no errors; the remakes are made new (no pool), as before.
- **audit_guard** (600 s, banjo_cpu_verify): 542 million calls of the copies compared, 0 differences (934 differed once
  and not when repeated: inputs changed by another thread meanwhile); the generated-code self-checks 0 differences.
