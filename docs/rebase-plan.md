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
