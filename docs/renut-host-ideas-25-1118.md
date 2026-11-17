# Renut host functions: 35 optimisations (2026-11-18)

The user (09:00): "can we get a table of 25 optimisations for host functions please for renut stuff", after the CPU
breakdown (docs/cpu-threads-breakdown-1118.md) named NewOp (1.97 ms a frame), Replay (1.93) and FastDrawLocked (3.42).

## Source

- **Profile:** console race 1118_ship_sampler. Build 2026-11-18_b3kick, the shipping toml with the SDK sampler on,
  Barcelona Oval, reports 9-20: the functions' own time (no callees), in ms a frame.
- **Stage census:** console race 1118_ship_census, the same toml with `blur_renut_diag_stage_census = true` (29.04 fps).
  Inclusive times, each stage with the stages it calls. The census's own clock reads are in them, about 0.5-1 us of a
  draw's 9.5 us.
- **Counters:** both runs' 10 s [renut] lines (278-300 frames a window).
- **Earlier work:** docs/renut-universal-audit-1117.md (32 ideas, done in batch 3) and docs/off-by-default.md. Nothing
  there is proposed again.

## Where the render thread's time goes

The render thread is 16.2 ms of CPU a frame (50 % of core 1). About 1,100-1,200 draws a frame, 99.8 % of them on the
fast path.

| Stage (census, inclusive) | us a call | calls a frame | ms a frame |
|---|---|---|---|
| FastDrawLocked | 9.4 | ~1,100 | ~10.3 |
| ... FillTextures (with RefreshBindingLocked and Upload) | 2.7 | ~760 | ~2.0 |
| ... WholeGeometryTry (with CardsProve) | 1.1 | ~1,100 | ~1.2 |
| ... the constants (two float pushes and the shared push) | 0.35 | ~3,300 | ~1.1 |
| ... the host draw (plume's draw call) | 0.62 | ~1,100 | ~0.7 |
| ... BindStreams | 2.1 | ~270 | ~0.55 |
| ... setPipeline | 1.0 | ~360 | ~0.36 |
| ResolveTargetsLocked | 15 | ~62 | ~0.93 |
| Upload (textures; inside RefreshBindingLocked) | 200-450 | ~1.5 | 0.2-0.9 |
| NewOp (the job threads) | 4.6 | ~920 | ~4.2 |

Sampled own time on the render thread besides: Replay 1.93, memcpy about 1.2 (31 % of 3.81), PushFloatConstants 1.10
(two clones), ConvertVertices 0.44, PagesClean 0.44, armDCacheClean about 0.22, BuildInheritRuns 0.18. On the job
threads: FinishResources 0.44, RecordExtents 0.39, CopyState 0.31.

## A finding: two record-time features do nothing in Blur

Blur's toml has `banjo_renut_replay_inherits = true`. The log says "replays with the inherited words as recorded: 0 of
131072", so every replay runs with `inherits` set. Replay then uses neither:
- the resource objects: `use_resources = !inherits && ...`. Their 10 s line says 0 texture slots answered by an object
  while 1,600-1,700 objects are made every 10 s;
- the slot extents: `g_draw_slot_extents = extents.mask && !inherits ? ...`.

NewOp still builds both for every op: PlanResources, FinishResources and RecordExtents. The render thread still applies
and prefetches them.

## The 25

Kinds: **A/B** = a toml line, no build. **fix** = a build. **census** = a counter first, then the fix it points to.
Savings are rough estimates of CPU time a frame at ~29 fps; the A/Bs and censuses decide. Every idea is exact (the same
pixels) unless it says otherwise.

| # | Idea | Where | Evidence | Saves (est.) | Thread | Kind |
|---|---|---|---|---|---|---|
| 1 | Resource objects off in Blur (`banjo_renut_resource_objects = false`); then NewOp skips them by itself whenever replays inherit | NewOp's PlanResources, FinishResources; Replay's ApplyResources and prefetches | 0 answers in Blur (above); FinishResources 0.44 ms | 0.6-0.9 | jobs, render | A/B, then fix |
| 2 | Record extents off in Blur (`banjo_renut_record_extents = false`), the same way | RecordExtents; Replay's ApplyExtents | unused while replays inherit; RecordExtents 0.39 ms | 0.4-0.5 | jobs, render | A/B, then fix |
| 3 | Restore the inherited words only in the chunks an op applied, not every inherited run at every op | Replay: `for (inherit_runs) memcpy(state, running)` when `op.plain_dirty` | the runs are every inherited word the buffer did not set (up to ~3.5 KB: the fetch constants' 768 bytes and the 2.8 KB tail), copied again at each op that applies a chunk (~900 ops a frame); the render thread's memcpy is ~1.2 ms | up to ~0.8 | render | census (bytes), then fix |
| 4 | Skip WholeGeometryTry when the draw's streams, index buffer and declaration are the previous draw's in the same kick | FastDrawLocked -> WholeGeometryTry, CardsProve | 1.1 us on every draw; BindStreams runs on only ~25 % of draws (8k/s of 32k draws/s) | 0.6-0.8 | render | fix |
| 5 | A draw-level texture-set cache: the whole set of slot words and effects by (mask, the slots' addresses and fetch words), for A-B-A patterns | FillTextures | 2.7 us a call on ~70 % of draws; 48 % of slots answered by the slot's last, 48.5 % by the per-slot memo | 0.5-1.0 | render | fix |
| 6 | Resolve setup kept per (source, destination, rectangle): framebuffer, pipeline, viewport, descriptors | ResolveTargetsLocked | 62 a frame at 15 us; ~50 colour resolves drawn a frame, likely the same ones each frame | ~0.45 | render | census (sub-stages), then fix |
| 7 | Find the textures that upload again (~1.5 a frame at 200-450 us); upload only their written rows, or convert on the cleaner thread | textures.cpp Upload via RefreshBindingLocked | Upload 0.2-0.9 ms a frame, spiky; 3 textures hashed a frame | 0.3-0.6 and the spikes | render | census, then fix |
| 8 | The Game Master's fence waits: how long each waited-for fence's work sat unsubmitted; kick at the fence's insert when the game waits on it | backend video / the D3D fence hooks | 28 waits a second, 67.6 ms a second (2.4 ms a frame) on the critical thread | unknown, up to ~1 on the critical path | Game Master | census, then fix |
| 9 | PushFloatConstants' "all held" test as 64-bit group masks made once per shader, not the 8-word loop and RegisterGroups at each call | PushFloatConstants | 1.10 ms own time, two calls a draw | 0.3-0.4 | render | fix |
| 10 | One pending-work mask in plume's command list: a draw with nothing pending skips the six flush and note calls with one test | Deko3DCommandList::draw*Instanced | 0.62 us a draw | 0.2-0.3 | render | fix |
| 11 | Every upload-ring clean on the cleaner thread; the render thread cleans only what a submit cannot wait for | upload_ring.cpp HandOff / Publish | 835 of 1,946 ranges cleaned by the render thread a 10 s; 1.44 MB cleaned a frame | ~0.2 | render | fix |
| 12 | Begin*Vertices and Draw*UP vertex data converted on the job thread that records it | ConvertVertices | 0.44 ms on the render thread; the data is known at record time | 0.2-0.4 moved off | render -> jobs | fix |
| 13 | BuildInheritRuns over 32-bit words of set_words (countr_zero runs), not one word at a time | command_buffers.cpp BuildInheritRuns | 0.18 ms on the render thread (a new buffer's first replay) | ~0.15 | render | fix |
| 14 | `ultimate_renut_replay_inherit_same = false` in Blur | BuildInheritRuns' copy, Replay's compare | it never matches in Blur (0 of 131,072) | 0.05-0.1 | render | A/B |
| 15 | `sasrt_gpu_skip_inert_draws = false` in Blur | DrawStateLocked's prologue (a GuestDevice and the depth control read at every draw) | 0 draws skipped in every 10 s window | 0.05-0.15 | render | A/B |
| 16 | The fast pipeline cache two-way, not direct-mapped | FastPipeline table (4,096 entries) | 7,652 conflicts a 10 s (27 a frame), each MakeKey and GetPipelineLocked | ~0.1 | render | fix |
| 17 | The texture slot memo two-way | TextureMemoSlot | 17,690 misses to another entry a 10 s (61 a frame), each the whole bind path | 0.1-0.15 | render | fix |
| 18 | Provisional pipeline answers dropped by their own shader's variant, not the global prebuild epoch | FastPipeline's provisional check | 4,941 provisional misses a 10 s (17 a frame) | ~0.05 | render | fix |
| 19 | A fast path for draws whose op changed only constants (no fields): straight to the constants, geometry and draw | FastDrawLocked | 3.42 ms own time; how many such draws is not counted (`banjo_renut_diag_draw_kinds` counts fields) | up to ~0.5 | render | census, then fix |
| 20 | Op storage reserved from the same buffer's count last frame (an Op is 256+ bytes; vector growth copies them) | NewOp `buffer.ops.emplace_back` | NewOp 1.97 ms own time on the job threads | 0.1-0.3 | jobs | census, then fix |
| 21 | CaptureStackHeaders only for ops whose chunks hold a surface header word | NewOp (port 76, `blur_renut_stack_surface_headers`) | runs at every op (~920 a frame) | 0.1-0.2 | jobs | census, then fix |
| 22 | Begin*Vertices captures as deltas on the Game Master, as the other immediate draws are | CaptureDeferred | 130 a frame at 2,795 bytes each; CaptureDeferred 0.22 ms on the Game Master | ~0.1 on the critical thread | Game Master | fix |
| 23 | Re-race the replay stream's prefetch stages in Blur (tuned on N&B, where two other stages cost time and were turned off) | Replay's look-ahead (A/B targets 55, 56) | Replay 1.93 ms own time, much of it prefetch issue | +/-0.3 | render | A/B |
| 24 | The texture uploads' and resolves' census lines added to the 10 s report for good (address, size, format, why) | textures.cpp, draw.cpp | #6 and #7 have no per-texture data today | (enables #6, #7) | - | census |
| 25 | A second render thread for the shadow-map command buffers: their own command list, submitted in order (a moonshot) | Replay, render_thread.cpp, the draw path's globals per thread | the render thread is one serial 16 ms chain a frame; shadow passes are separate buffers | the render thread's latency -3 to -4 ms (no CPU saved) | render, core 2 | fix (large) |

## What to do first

- **Toml only (one console race, no build):** #1, #2, #14 and #15 together, then one at a time if it helps.
- **One census build:** #3's bytes, #6's sub-stages, #7's textures, #8's fence ages, #19's draw kinds, #20's growth
  and #21's cost as counters.
- **Then the fixes,** largest first: #3, #4, #5, #9, #10, #11.

Rough total: 4-6 ms a frame of the render thread's 16, 1-2 ms of the job threads', and #8 still unknown. The render
thread is 50 % idle, so its savings free core 1 for the job threads that share it and submit GPU work sooner. They do
not lower the GPU's 28.5 ms.

## Ten more (the user, 09:50: "can you come up with 10 more please")

**New evidence:**
- **Sizes:** the per-draw functions' machine code, from the ELF's symbols.
- **Hot spots:** the sampled addresses inside FastDrawLocked and NewOp, on their disassembly (scratchpad hot_bins.py).
- **Game Master:** its stacks (host_top.py), with the waiting samples apart.
- **Ruled out:**
  - the frame limiter's 24 % of the Game Master is a sleep, not a spin;
  - the texel readbacks are already asynchronous (CompleteResolveReadbacksLocked at the slot's fence);
  - the census switch is inlined;
  - `RefreshBindingLocked` already checks a texture once a frame;
  - plume's pipeline and vertex buffer binds already send only the groups and slots that changed.

| # | Idea | Where | Evidence | Saves (est.) | Thread | Kind |
|---|---|---|---|---|---|---|
| 26 | A hand hot/cold split of the per-draw functions: the verify, diagnostic, census, log and format paths and the rare cases moved into `[[gnu::cold, gnu::noinline]]` helpers; one copy of each hot helper, not two clones | FastDrawLocked (38 KB of code), DrawStateLocked (48 KB), FillTextures (two clones, 15 and 17 KB), WholeGeometryTry (15 KB), PushFloatConstants (two clones), NewOp (13 KB and an 8 KB clone) | the A57's instruction cache is 48 KB; FastDrawLocked's 79 samples are spread over field loads with no hot loop. The compiler's block partitioning broke the LTO link on 11-16 and flag experiments are over, so the split is in the source | 0.3-1.0 | render, jobs | fix |
| 27 | The hidden overlay dialogs detached: the FPS, log and profiler dialogs attached only while one shows | renut_app.h OnCreateDialogs; the SDK's ImGuiDrawer::Draw (it returns at once with no dialogs) | they are attached for good, so every present builds an empty ImGui frame (NewFrame, each dialog's Draw, Render, the immediate drawer's Begin and End): ~0.2 ms on the main thread, which shares cores 0-2 with the game | ~0.2, maybe a GPU pass | main | fix (small) |
| 28 | What the Game Master blocks on inside Renut's queueing, by cause (the render ring full, the capture pool, the record lock, the deferred-vertex mutex), then the biggest one fixed | QueueRunCommandBuffer, Resolve -> QueueResolve, CaptureDeferred, QueueCapture, render_thread::QueueUnlocked, KickDevice | waiting samples of the Game Master there: 0.68 + 0.55 + 0.28 + 0.28 + 0.14 + 0.14 % (~2 %, ~0.7 ms a frame of the critical thread) | up to ~0.7 on the critical path | Game Master | census, then fix |
| 29 | Immediate draws batched on the Game Master: captured into a local block and pushed as one ring item per run of them | FlushDeferredVertices, QueueDraw, QueueCapture, QueueUnlocked | ~195 immediate draws a frame, each paying all four (~1.1 % of the Game Master running, ~0.37 ms a frame) | 0.2-0.3 | Game Master | fix |
| 30 | A run's device copy narrowed to what its replay reads: the inherited words (up to ~3.5 KB) and the constant groups the buffer did not set | QueueRunCommandBuffer `run->running.assign` (the whole 12.9 KB device) | ~15 runs a frame (65,536 replays in 145 s), ~200 KB copied a frame on the Game Master | 0.03-0.06 | Game Master | fix |
| 31 | NewOp's whole-device check every 256 ops, not every 32 (`banjo_renut_tags_check = 256`) | DifferingChunks in NewOp | 9,408 whole checks a 10 s (32 a frame, each over the 12.9 KB device) and 0 chunks changed without a mark. Not strictly exact: an unmarked write (none seen in Blur) would be caught 256 ops later, not 32 | 0.05-0.08 | jobs | A/B |
| 32 | ComputeViewport's answer kept by its input words (a small table: the frame goes back and forth between a few viewports) | FastDrawLocked -> ComputeViewport | 0.22 ms own time on the render thread | ~0.1 | render | fix |
| 33 | Open-addressing tables for the render thread's lookups instead of std::unordered_map | render/surfaces.cpp g_targets, render/pipeline.cpp g_pipelines (a map of vectors), the shader records | GetHostSurfaceLocked 0.13, FindShaderLocked 0.17, GetPipelineLocked 0.09, GetStreamSource 0.09 ms | 0.1-0.2 | render | fix |
| 34 | One constants step a draw: the vertex, pixel and shared pushes in one call with one setup, and adjacent ranges in one push command | PushFloatConstants (twice a draw), PushSharedConstants | ~3.3 calls a draw at ~0.35 us each (1.1 ms a frame); the two PushFloatConstants clones 0.66 and 0.44 ms own time. Different from #9, which changes only the held test | 0.1-0.2 | render | fix |
| 35 | Renut's native D3D setters: count their calls a frame and the time a call; if it is the call from the generated code, emit their few stores in the callers; if it is the device's lines moving between cores, leave them | d3d_native_state.cpp (RsZEnable, SetViewport, SetSurfaceInfo, ...) | 11 setters, 0.54 ms a frame together on the job threads and the Game Master; each is a few loads and stores (RsZEnable: 2 loads, 2 stores, a mask) | 0.1-0.3 | jobs, Game Master | census, then fix |

**First steps for these ten:**
- **Toml only:** #31 joins the other toml A/B (#1, #2, #14, #15).
- **The census build:** #28 and #35.
- **Then the fixes:** #26 (the largest, one build of its own), #27, #29.
