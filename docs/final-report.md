# blur-nx: report (2026-11-11)

Blur (Bizarre Creations, 2010, Xbox 360) on the PC and the Nintendo Switch with ReXGlue and Renut's renderer, from
renut_nx_ultimate_base. Details in `docs/blur-notes.md`, the hour-by-hour log in `docs/progress-log.md`.

## The package

`D:\blur\builds\2026-11-11_0608_blur-nx_test` (commit `14c556d9`, NRO md5 `fa9fcf64c98796e2ebd94793cc3569c0`):
`COPY_TO_SD_switch_blur-nx/` (the NRO, `renut.toml` = the shipped settings with logs, `configs/` with `renut_play.toml`
(diagnostics off), `renut_ab.toml` (30 s halves, target 1 the do-nothing control), the picture-changing options as
their own configs, `renut_profile.toml` (the console profile), `renut_verify.toml`; the prebuilt DKSH shader cache,
3076 variants), `blur-nx.elf`, `source_14c556d9176a.tgz`, `README.txt`, `PACKAGE_NOTES.txt` (what to test, the logs
to send, the console profile, the A/B). No game data anywhere; the game folder is the player's own disc.

## What works

- **PC** (D3D12 and Vulkan): boot, logos, attract video, menus, career, a race (Barcelona Oval, 20 cars, HUD,
  power-ups, motion blur, bloom, glow, the tail lights' glow, the car's reflections and rear window), at 60 fps.
- **Switch build in Ryujinx** (deko3d, the same code as the NRO): the same, the race at 10-14 fps in the emulator;
  0 verify differences, 0 memory emergencies over 9 minutes (emu11).

## Measured

| What | Result |
|---|---|
| Verify keys (store cards, snapshots, fetch data; Ryujinx) | 0 differences (about 72,000 card-proven copies checked) |
| Ryujinx race | 10-14 fps; the game at 0.67-0.93 of real time (below) |
| The game's speed against its frame rate (PC, `blur_diag_frame_timer`) | real time from 15 to 60 fps; fps / 15 below 15 (0.80 at 12 fps); twice real time at 120 fps (now held to 60 on the PC) |
| PC picture against the SDK's emulated renderer at the same moment and lighting phase | within about 10% of its brightness; the car paint about 25% darker (where the car stops decides its light, below) |
| Console | not measured here: the console was another session's (`renut_profile.toml` and `renut_ab.toml` are for that) |

## Fixed in this stretch (each with its own key and A/B number, on)

- 215-218: the motion blur's source (stack surface headers per queued operation, the sample-grid resolve, the
  overwritten EDRAM wipe, a surface size guard): the Switch race was drawn over garbage before.
- 219 `blur_renut_resolve_exp_bias`: resolve exponent bias (the glow 2048 times too dim).
- 220 `blur_renut_edram_inherit`: views of one EDRAM take each other's writes (the environment map's alpha, the glow
  chain).
- 221 `blur_renut_ms_slice_resolve`: multisampled resolves into cube faces (five black faces: the black rear window).
- 213, corrected: the occlusion answers now land where the game reads them (no query was ever answered: the light
  glows, the tail lights' red glow).
- `blur_pc_speed_cap` (PC): the game ran at double speed on a 120 Hz display.

## Visual issues still open

1. The PC frame is still about 10% darker than the reference in the same lighting phase, the car paint about 25%.
   The car's light changes with where it stops (in the trees' shade the game lights it less, in both renderers), so
   a comparison at one exact car position is needed before calling it a renderer fault.
2. A resolve into a destination of another format keeps the source's precision (the console converts and clamps into
   the destination's: 8-bit quantisation, 16-bit fixed point); not reproduced.
3. `blur_renut_view_reinterpret` (210) is off (the 7e3 / 8888 views of the HDR scene): not proven either way.
   `blur_renut_resolve_keep_dest` (212) no longer fires; `blur_renut_tile_resolve_staging` (214) is not proven
   against its absence on the console.

## Next steps

1. On the console: the package's `renut_ab.toml` (each fix's cost; 220 and 221 add copies) and `renut_profile.toml`
   (the hot functions for the machine copies), the frame rate in a race (it decides the game's speed below 15 fps).
2. Machine copies and PGO + LTO: on hold until you say (the SASRT session's atomics patch is applied to
   `tools/localize_native.py`, not run).
3. The same-position comparison for item 1 above (a save at the grid, or a fixed camera).
4. The base: the tested renderer fixes were sent to the RR6 session for renut_nx_ultimate_base (the "Proposed base
   ports" table in `docs/blur-notes.md`).

## Decisions taken (recorded in the notes)

- The game's gamma ramp on (it is the reference's and the 360's picture); SMAA off (not the game's picture); both as
  configs the other way. `renut_post_process_precision = "Game"`.
- Every correctness fix on by default with its own key and in the in-run A/B list.
- PC comparisons use the shipped settings (`make_blur_tomls.py` writes a PC test copy beside `renut.exe`): the earlier
  PC runs had the build's defaults.
- The PC holds the game to 60 fps (`blur_pc_speed_cap`); the Switch is unchanged (60 Hz display). Ryujinx's timing
  was only measured, not changed.
- The DKSH cache is compiled on the PC from a Ryujinx run's pipeline list (3076 variants, 0 failures).
- The console was not used (another session's); nothing was pushed.

## Credits

reNut's renderer (Renut), renut_nx_ultimate_base and the ports it grew from (katamari-nx, banjo-nx, sa2-nx, rr6-nx,
sasrt-nx, kameo-nx, vivapinata-nx, halo3-nx, pacman-nx, futari-nx and others), NetherSX2_nx, battd_nx, the
bejeweled3-nx and zuma ports; ReXGlue; the SDK's emulated renderer (Xenia's) as the reference. GPL-3.0.
