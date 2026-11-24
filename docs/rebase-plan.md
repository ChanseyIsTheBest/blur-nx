# Rebasing blur-nx onto a new renut_nx_ultimate_base

The user's instruction (2026-11-11): when the RR6 session says the new ultimate base is ready, rebase blur-nx onto it and
graft the parts of this port's renderer the base does not have.

## How

This repo started as a copy of the base's files (commit `37d724df`: base github-main `1fd35fd8`, ports 1-62; ports 63/64
applied after), not its history. So the rebase is a three-way merge with `37d724df` as the common ancestor:

1. A branch `base-update` from `37d724df`, in its own worktree (`git worktree add`, main's checkout untouched until
   the merge); there `git rm -rq .`, the new base's committed tree extracted (`git -C <base> archive <commit> | tar -x`:
   its commit, not its working tree, which may hold work in progress), `git add -A`: the branch's tree is then exactly
   the base (files it deleted since port 62 deleted too). Committed as "the ultimate base at <its commit>".
   Checked 2026-11-11: `37d724df` has the same 23,534 files as the base at port 62 (`14afc76d`); their blob hashes
   differ only by line endings (the base keeps some files with CRLF; this repo's `core.autocrlf = true` stores LF), so
   committing the new base's files here the same way normalises them alike, and unchanged files match the ancestor.
2. `main` merged with `base-update`: git applies the base's changes since `1fd35fd8` onto this port's, and stops only
   where both changed the same lines. In a conflict: the base's version of a fix it now carries (below, "sent"), this
   port's version of what is Blur's own.
3. Codegen again with the new base's codegen (the user, 2026-11-11: "do codegen again after the rebase", the codegen
   has improved): delete `app/generated/codegen.stamp` and `codegen.build.stamp`, codegen, post-codegen, the PC and
   Switch builds.
5. Then machine copies for every function in the profile, with the new base's machine-copy tools (the user,
   2026-11-11: "make machine copies for everything in the profiler"; the hold is lifted): verify keys 0 differences
   in Ryujinx, the A/B list, a new package. PGO + LTO still only when the user says.
4. Checks before calling it done: the PC grid comparison against the reference (glow, rear window, motion blur, the
   far scenery's brightness), a Ryujinx race (0 verify differences, 0 memory emergencies), the in-run A/B keys still
   switch, the package.

## The RR6 session's word (2026-11-11 ~12:30)

The base at `ed33a5f4` has ports 71-75 (SASRT's machine-copy round and formats batch: rows 71-75 of its
`docs/ultimate-fixes.md`, `D:\renut-ultimate\MERGES.md`, `D:\renut-ultimate\tools\ports\p71-p75`), but **not yet**:
this port's renderer fixes (211-213, 215-221, the census mutex) go into the base next as port 76, and RR6 says when the
base is ready. Do not rebase before that. Then:
- codegen and post-codegen again (port 74's generator ideas: `SASRT_HOT_*` CMake options, `SASRT_LOCALIZE_*` env at
  post-codegen); **fresh build folders**: an existing one keeps the new CMake options cached OFF;
- port 72 (`SASRT_VMX_FAST_ESTIMATES`, `SASRT_VMX_FUSED_MADD`: vrsqrtefp / vrefp / vlogefp estimates and fused
  vmaddfp / vnmsubfp on AArch64) is ON in the base and **not bit-exact** (the user's call for the base). It does not
  touch the verify keys (RR6: `vmx_float_macros.py` runs before `localize_native.py`, so copies and originals use the
  same forms; AArch64 only, so the PC's verify is exact on both sides; port 74's PC verify: 243 M calls, 0
  differences). What it changes is the game's float results against the console's exact forms: 1-3 ulp on the
  estimates, fused multiply-add in 2.4 % of lanes. **The user (2026-11-11): "Keep it on like the base"**: Blur builds
  with the base's defaults (no `-DSASRT_VMX_*=OFF`);
- port 75 (renderer formats: 2_10_10_10 as 10:10:10:2, EDRAM formats 10/12 not RGBA8, 32-bit float textures point
  sampled) changes what Blur draws where it uses them: the PC comparison against the reference covers it;
- PGO: not used for Blur (only when the user says), so "retrain PGO" does not apply.

## What is this port's own (the delta from `37d724df`, 2026-11-11)

**Renderer fixes sent to the base** (the RR6 session has them; if the user approves them for the base, the new base
may carry them: keep the base's version then, ours where it does not):

| Key (A/B) | Where |
|---|---|
| set_words rounding (replay inheritance) | `guest/command_buffers.cpp` |
| `blur_renut_expand_alias` (211) | `render/textures.cpp` ResolvedAlias |
| `blur_renut_resolve_keep_dest` (212) | `render/textures.cpp` ResolveTarget, `render/draw.cpp` |
| `blur_renut_occlusion_queries` (213) + `QueryBlockPhysical` | `d3d/hooks/state.cpp` |
| `blur_renut_stack_surface_headers` (215) | `render/surfaces.*`, `backend/queued.cpp`, `guest/command_buffers.cpp`, `d3d/hooks/state.cpp` |
| `blur_renut_sample_grid_resolve` (216) | `render/draw.cpp`, `shaders/host/sample_grid_resolve_ps.hlsl`, `backend/video.*`, `gpu.cmake` |
| `blur_renut_surface_size_guard` (217) | `render/surfaces.cpp` |
| `blur_renut_wipe_overwritten` (218) | `render/draw.cpp`, `render/surfaces.cpp` |
| `blur_renut_resolve_exp_bias` (219) | `render/draw.cpp`, `shaders/host/resolve_copy_ps.hlsl` |
| `blur_renut_edram_inherit` (220) | `render/draw.cpp`, `render/surfaces.*`, `shaders/host/edram_inherit*.hlsl*`, `backend/video.*`, `gpu.cmake` |
| `blur_renut_ms_slice_resolve` (221) | `render/draw.cpp` |
| FastCensus mutex | `render/draw.cpp` |
| XenosRecomp two-binding vertex shaders | `app/thirdparty/reblue-XenosRecomp/XenosRecomp/main.cpp` |
| PC path wizard skipped when the game folder is given | `app/src/renut_app.h` |
| `localize_native.py` makes `generated/hot`; `switch_shaders.ps1` "Continue"; `renut_state_tables.py --ref-*` | `tools/` |

The keys and their A/B labels: `banjo_renut_tuning.cpp/.h` (210-221).

**Blur's own (always grafted):** the hooks at XDK 2.0.11164 (`d3d/hooks/*.cpp`, `d3d/d3d.h`, `d3d/d3d_native*`, the
device offsets and snapshot size, the command-buffer hooks, `D3DQuery_GetData`'s entry with `blur_renut_diag_query_log`),
`app/config/*` (codegen, hooks, funcs, CRT, hot lists), `app/overrides.toml`, `app/renut_config.toml`,
`app/indirect_promotions.txt`, `app/src/blur_keys.cpp` (`blur_pc_speed_cap`, frame_cap), `app/src/blur_write_watch.*`
(PC diagnostics), `app/src/renut_app.h` (paths, title update, built-in config, the diagnostics' install),
`app/CMakeLists.txt`, Katamari's layer taken out (`tools/blur_strip_katamari.py`), the tomls and package tools
(`tools/make_blur_tomls.py`, `make_blur_builtin.py`, `package_switch.ps1`, `run_*.ps1`), the RenderDoc tools
(`tools/rdc_*.py`), the PC test copy of the shipped settings, docs.

**Not in the base, kept as they are:** `blur_renut_view_reinterpret` (210, `shaders/host/view_reinterpret*`): off, not
proven; `blur_renut_tile_resolve_staging` (214, `render/draw.cpp` TileResolveStagedLocked): **on** (corrected
2026-11-11, RR6's note: this plan first said "kept off"; its default in this tree is on, as deko3d's region resolves of
Blur's tiled 4x scene need it in Ryujinx; not proven against its absence on the console).

## Done (2026-11-11, 14:00-14:30)

`base-update` (`1334e6d9`): base `a30f24f7` over `37d724df` (23,546 files; every file that differs from the ancestor
is one the base changed). Merged into main (`7131f98d`): 26 files in conflict, 34 changed on both sides. The base's
version where it carries this port's fixes; Blur's own kept (the merge commit's message lists both). Duplicates the
automatic part of the merge left (no conflict marker: both sides added the same helper in different places) found by
diffing each file against the base and by a scan for functions and cvars defined twice: `ConvertibleResolveFormat`
(textures.cpp), `ResolveCoverPixels` (draw.cpp), the EDRAM span code (surfaces.cpp: the base's whole file, which holds
everything Blur's had), Blur's own copy of 213's answer (state.cpp: its GetData entry now calls
`native::AnswerOcclusionQuery`). The generator: the base's `sdk/out/win-amd64/rexglue.exe` (built 13:00, after its last
SDK change, port 71), copied as the first one was (the old one in `C:\blur-nx-test\old_tools`). Build folders
configured with `cmake --fresh` (the old caches in `C:\blur-nx-test\old_caches`).

**Not touched by this port:** the CPU side's hot lists are this game's (empty machine-copy lists); the SASRT session's
atomics patch is in `tools/localize_native.py` (not run). The new base's machine-copy round (the SASRT ports) comes in
with the base and is the one used for step 5 (Blur's own hot lists are made fresh from the profile then).

## Sync 2026-11-22: base cebb46af (ports 182-254)

The user: "sync up with ultimate base please new render thread improvements". The same method as above: base-update
`56af7712` (the base's committed tree at cebb46af), merged into cycle1 as `4a37ffc2` with `git checkout --conflict=diff3`
and per-hunk picks (the merge commit lists each file). 22 files in conflict, 34 changed on both sides. Points to know
for the next sync:

- **git pairs Katamari's dist folder with Blur's** (rename detection): the base's edits to dist/switch/katamari-nx
  (port 252's forwarder 0100080021954000, port 254's uam line) arrived as conflicts in dist/switch/blur-nx. Resolve them
  by making the tomls again: `python tools/make_blur_tomls.py --out dist/switch/blur-nx` (from the base's
  tools/renut_base_banjo.toml, Blur's SHIPPED on top: Blur's forwarder wins), then `python tools/make_blur_builtin.py`.
- **Device offsets in new base code**: port 213's field map in render/draw.h has Katamari's own-declaration offset
  (0x2F98); Blur's is 0x3050. draw.cpp's static_assert caught it, but only in the Switch syntax check
  (scratch_scripts_1120/ninja_syntax_wt.py with switch-release-final's commands). Scan the base's diff for hex offsets in
  0x2000-0x6000 after a sync.
- **Port 254 / 251 and Blur's core 3**: the uam compiles moved to core 2 (banjo_system_core_uam false) because port
  251's command stage (0x2E) and warm helper (0x2F) sit on core 3. Blur's audio threads stay there above them (0x2B,
  the mixer 0x2D); Blur's game threads also use core 3 (banjo_system_core 2). The stage's gain in Blur is measured on
  the console (A/B 182, 185, 179).
- **Codegen**: the generator's source changed (ports 187, 194) and post_codegen.py (224): the PC build with both
  stamps deleted relinked rexglue.exe and ran codegen (346 s) and post_codegen (about 45 min). cc_entry_census.py ends
  with "none".

PC checks (C:/blur-nx-test/pc_runs): sync_verify_1122 (banjo_cpu_verify): 84 M calls compared, one difference,
sub_8230C2D0 (bulk), whose closure has an atomic increment (sub_822F1100, lwarx/stwcx.) of a counter the audio job
threads share: the guard's copy-then-original passes see it moved by another thread (the differing byte fits a counter
2 apart); the copy is kept (it has run unguarded in every Switch build). That run stayed on loading screens (black
captures; the guard's slow loads, as on 11-18). sync_noguard_1122 (the PC's first-64-calls guard): reaches the race and
draws as 11-21's run does at the same second; its 35 guard differences (17 functions) are the classes 11-21's pre-sync
run showed in 13 of the same functions (r12 and stack-address leftovers); new to it only by the route: sub_823262D8
(a NaN's sign through the hand-written sin/cos native 82954478, an input of 2.6e28) and sub_82455348 (a byte of a
global at 0x82CC00D0, a race function the 11-21 menu run never reached).

Profile (hb16, commit `247be126`): the instrumented build of `45b0379e` trained on the console, one race each of
Barcelona Oval and Amboy (Proving Grounds' first and second events), 551 profile files. Training note:
scratch_scripts_1120/one_race_event2.sh's down press had come one confirm early on 11-18 (on the chapter card, where it
does nothing), so hb13's and hb15's "Amboy" sessions were Barcelona Oval again (the 1.0.0 and pre-sync 1.0.1 profiles
saw one track). It now presses down after the fifth confirm, and amboy_check.py stops the script in the menus unless a
shot of the event list shows Amboy selected.

Console (2026-11-22, 22:49-23:41; release builds, `log_level = "info"`, frame cap off, Barcelona Oval, the 240 s quiet
drive; pacing_sum.py over the race windows, `--gpu-min 0 --cpu-min 150`). The console now runs at higher clocks (GPU
1305 MHz): an uncapped race sits at the display's 60 Hz, so the mean cannot move; the lows and the render thread show the
sync. A = the pre-sync 1.0.1 (2026-11-22_release_1_0_1_media, hb15), B = the synced release
(2026-11-22_release_1_0_1_sync, hb16), in the order A B B A:

| race | mean fps | 1 % low | 0.1 % low | render thread ms | GPU ms |
|---|---|---|---|---|---|
| A1 | 59.93 | 39.90 | 31.11 | 10.55 | 14.19 |
| B1 | 60.05 | 42.63 | 34.53 | 8.07 | 13.77 |
| B2 | 60.03 | 41.16 | 31.29 | 9.01 | 13.91 |
| A2 | 59.67 | 35.91 | 15.93 | 10.83 | 14.30 |

B against A: 1 % low 41.9 against 37.9 fps, render thread 8.5 against 10.7 ms a frame (-20 %), the race's CPU about
250-280 % against 270-300 % (rex_profile.log). A2's 0.1 % low is two frames near 200 ms (the render thread's).

A/B race on B (`banjo_renut_ab_targets = "182,185,179,1"`, 15 s halves; ab_sum.py; the render thread's us a draw, the
halves catching different scenes): 182 the command stage on 4.61 / off 5.54 (it pays); 185 the warm helper 4.65 / 4.57
(even, 15 s each); 179 kick submits 4.51 / 4.18 (on about 8 % worse); 1 precise barriers 4.78 / 4.61 (on about 4 %
worse). One race: 179 and 1 are signs for a longer A/B, not verdicts.

A crash in A2 (the pre-sync build): about 15 s after the drive (near the race's end), std::terminate on std::bad_alloc on
an XMA decoder thread: XmaContext::DecodeFrame, the frame cache's AddNode (`new uint8_t[need]`, a few KB: past the
small-alloc arena, so newlib's malloc returned null). The heap report 30 s before: 1496 MB taken of 3074, 830 MB never
handed out; no growth over the race. Not exhaustion, then: newlib's heap state (a corruption) or something taking
~800 MB at once. First bad_alloc in any saved Blur crash log; the pre-sync 1.0.1 was the first build with the Opus
streams and the H.265 movies, which B has too. B passed three race ends (B1, B2, the A/B race). Open.

## Sync 2026-11-24: base b40a6f79 (ports 255-279)

The user: "sync up to the ultimate base again please". Two merges, because the base moved on during the first:
base-update `56951598` (the base at ac35fff5, ports 255-273) merged as `e6dbc0f9`, then base-update `1c7ee86a` (the
base at b40a6f79, ports 274-279) merged as `8536019b`, both with `git checkout --conflict=diff3` and per-hunk picks (each
merge commit lists its files). Points to know for the next sync:

- **pgo-inplace is Blur's own**: the base now carries Katamari's profile (540 files). Keep Blur's set exactly and leave
  the base's out; Blur's profile is trained fresh after each sync anyway.
- **Katamari's own pieces in app/**: port 267's WMV hook (katamari_wmv_ffmpeg.cpp and its libavcodec link) and the
  katamari_switch*.toml files stay out; the NRO's version stays Blur's (the base's 1.0.2 is Katamari's). Port 259's
  nxvideo library is taken, and Blur's movie code moved behind its RENUT_NXVIDEO switch.
- **Port 255's dynamic resolution stays off for Blur**: blur_switch_defaults.toml's
  `banjo_renut_dynamic_resolution = false` sets both modes' keys; the console log says "off now" for both.
- **blur_small_alloc.cpp**: port 270 is Blur's own ae5e607a (newlib's break under malloc's lock); one _sbrk_r is kept,
  with SASRT's plume_region_give_back_for_sbrk added.
- **tools/switch_remote.py**: both sides' commands (Blur's rm, port 277's append).
- **Syntax check of a worktree's files**: scratch_scripts_1120/syntax_check_nopch.py (switch-release-final's command
  without the precompiled header, and every -I into main's tree pointed at the worktree), so the worktree's headers are
  not paired with main's copies (false redefinition errors otherwise).
- **Ports 275 and 279 in Blur**: Blur's races log no predicated tile runs and 0 colour or depth hand-over losses, so the
  tile twins and the second hand-over destination change nothing on screen here. Device offsets: draw.h keeps Blur's
  declaration at 0x3050.

PC checks of `e6dbc0f9` (C:/blur-nx-test/pc_runs): sync_verify_1124 (banjo_cpu_verify): 83 M calls compared, 0
differences. sync_noguard_1124 (the first-64-calls guard): the game thread and the render thread went quiet 157 s in
(~25 s into the race) while the audio and file threads ran on, and no stack was taken; the same race again under the
Switch build's load (sync_noguard_1124b) reached the end. A hang watcher now sits beside the no-guard races
(scratch_scripts_1120/pc_hang_watch.py: when the 10 s frame lines stop for 25 s, every thread's stack twice, named from
renut.pdb).

Profile (hb17, commit `ed6aa325`): the instrumented build of `8536019b` trained on the console 2026-11-24, 21:01-21:23,
one race each of Barcelona Oval and Amboy (each track seen on its 20 s shot), 551 profile files, summary runs = 2. The
release on it: C:/blur-nx-test/builds/2026-11-24_release_1_0_1_sync2 (clean build; the same four Vulkan-header notes as
hb16's, no profile mismatch, no personal strings).

Console ABBA (2026-11-24, 21:50-22:28; release builds, `log_level = "info"`, frame cap off, Barcelona Oval, the 240 s
quiet drive; pacing_sum.py `--gpu-min 0 --cpu-min 150`). The console was in handheld mode (1280x720, the handheld GPU
clock): every race is GPU-bound at ~31 fps (GPU ~28.5 ms a frame), so the mean cannot show CPU-side changes. A = the
11-22 release (2026-11-22_release_1_0_1_sync, hb16, ports 1-254), B = the new release (hb17, ports 1-279, with the 11-23
nv retries, abort report, audio re-appends and the sbrk lock), in the order A B B A:

| race | mean fps | 1 % low | 0.1 % low | render thread ms | GPU ms |
|---|---|---|---|---|---|
| A1 | 30.90 | 16.78 | 13.68 | 13.59 | 28.70 |
| B1 | 30.39 | 17.09 | 13.43 | 13.48 | 28.24 |
| B2 | 30.79 | 17.14 | 13.53 | 13.73 | 28.76 |
| A2 | 30.87 | 15.42 | 9.03 | 13.82 | 28.76 |

Even: compare_runs.py pools A at 30.99 fps, render thread 13.29 ms, CPU 328 % and B at 30.79 fps, 13.53 ms, 327 %
(B's races drew ~3 % fewer draws a frame: the scene, not the build). A2's lows are one render-thread stall of 170 ms at
69 s (frames-ahead waits behind it). No crash in the four (the card's rex_crash.log is the 11-23 one), dynamic
resolution off in both modes, 0 hand-over losses. The card was left with B and the player's own blur.toml.

The PC build of `8536019b` and its races (verify, the hang hunt) were stopped mid-build at the user's word ("don't
bother with the PC build", 2026-11-24 22:35): ports 274-279 change no CPU-side code, and `e6dbc0f9`'s verify race was
clean. The no-guard hang of sync_noguard_1124 stays open (pc_hang_loop_1124.sh hunts it).
