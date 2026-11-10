blur-nx: Blur (Bizarre Creations, 2010; Xbox 360, title 41560845) on the Nintendo Switch
=========================================================================================

A ReXGlue static recompilation, built from renut_nx_ultimate_base (banjo-nx's SDK fork, its post-codegen CPU passes
and the ports' fixes) with reNut's Direct3D-level renderer on plume's deko3d back end (no NVK, no PM4 ring, no EDRAM
model). Your own game files are needed; nothing of the game is in this package except the recompiled code.

Credits: reNut's renderer by masterspike52 (its author permits its reuse in these ports), renut_nx_ultimate_base and
the ports its fixes come from (katamari-nx, banjo-nx, sa2-nx, rr6-nx, sasrt-nx, kameo-nx and the others),
NetherSX2_nx by NaGaa95 (the four-core layout, MIT), battd_nx (the GPU arena), bejeweled3-nx and the zuma ports (the
hot-function method). GPL-3.0 where the sources are.

SD card layout
--------------
sdmc:/switch/blur-nx/blur-nx.nro        the program
sdmc:/switch/blur-nx/renut.toml         settings, the only toml this test build reads (see "The tomls")
sdmc:/switch/blur-nx/game_root/         the disc's files: default.xex and the rest of the disc (extract your ISO; the
                                        ISO itself is not read). Title update 2's default.xexp goes next to
                                        default.xex.
sdmc:/switch/blur-nx/update/            title update 2's other files (mounted as UPDATE:, as the console does)
sdmc:/switch/blur-nx/dlc/               optional DLC packages (Xbox 360 LIVE files), installed at the first start;
                                        not tested with any

The tomls
---------
This test build (BLUR_BUILTIN_CONFIG=OFF) has no settings compiled in: it reads renut.toml next to it, in full. To use
another toml, copy it over renut.toml. A toml under any other name does nothing.

renut.toml        the shipped settings, with logs: Renut on deko3d, 720 lines handheld and docked (the Xbox 360's own:
                  pixel-identical), the game's own 4x MSAA, the game's display gamma ramp, every exact enhancement on,
                  the items measured SLOWER ON THE CONSOLE in banjo-nx off, SMAA off.
renut_play.toml   the same with the render-thread diagnostics off and the log at "error" (for playing).
renut_ab.toml     the same with an in-run A/B list, 30 s halves, target 1 as the do-nothing control (a Vulkan-only
                  key). Stay in one place (a menu, or a race standing still) for 10+ minutes; the log's
                  "[renut A/B]" lines give each target's time.
configs/          one key away from renut.toml each, for you to decide on: renut_1080_docked.toml (1080 lines docked;
                  not pixel-identical), renut_dynamic.toml (dynamic resolution), renut_smaa.toml (SMAA on the 3D),
                  renut_gamma_off.toml (without the display's gamma ramp: brighter darks, saves its present cost); and
                  test settings: renut_verify.toml (every verify mode: slow), renut_pgo.toml, renut_profile.toml (the
                  console profile, below).

A console profile
-----------------
Copy configs/renut_profile.toml over renut.toml (switch_profile_stacks = true): the sampler writes
sdmc:/switch/blur-nx/logs/rex/rex_profile.log, one report every 10 s. Play a few minutes of a race, then send that
file and logs/renut_001.log. With blur-nx.elf (this NRO's own ELF, in the package) the samples map to functions:
the machine copies (tools/switch_hot_bulk.py --merge) and any hand-written native are made from it.

Cores
-----
The port runs on the three cores a homebrew launch gets. Four cores are optional and off: set
banjo_forwarder_program_id in renut.toml to your forwarder's id, and the NRO writes that forwarder's four-core NPDM
(NetherSX2_nx's layout, CPU 0-3) to sdmc:/atmosphere/contents/<id>/exefs/main.npdm at the next start. Delete that
main.npdm to go back to three cores.

Profiling files
---------------
blur-nx.elf and the source snapshot (source_<commit>.tgz) are the ones this NRO was built from.
