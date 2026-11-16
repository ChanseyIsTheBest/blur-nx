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
