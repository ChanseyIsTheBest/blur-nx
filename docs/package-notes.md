# blur-nx test package: what to test, and how

Draft of 2026-11-10 (finished at packaging time). The package holds the NRO, three settings files, the configs, the
NRO's ELF and the source snapshot; no game data.

## Setting up

1. Copy `COPY_TO_SD_switch_blur-nx/` to `sdmc:/switch/blur-nx/` on the SD card.
2. Extract your own Blur disc into `sdmc:/switch/blur-nx/game_root/` (default.xex and the rest). Title update 2:
   its `default.xexp` next to `default.xex`, its other files in `sdmc:/switch/blur-nx/update/`.
3. Start `blur-nx.nro` from the homebrew menu (title override / full RAM: the game needs the memory of a game launch).

This build reads `renut.toml` next to the NRO, the shipped settings with logs. For playing, copy
`configs/renut_play.toml` over it.

## What to test first

- It boots through the logos and the attract video to "Press START", and the career menus draw.
- A race (Barcelona Oval, the first Proving Grounds event): 20 cars, the HUD, power-ups. In Ryujinx the lower part of
  the race frame was garbage before the tile-resolve fix (`blur_renut_tile_resolve_staging`); please look at the whole
  screen.
- The tail lights and bright lights: the PC build still lacks the glow and light trails the emulated renderer shows
  (under investigation); compare with your memory of the Xbox 360.
- Frame rate: the log's 10 s lines (`logs/renut_001.log`) give the frame times.

## Logs to send

`sdmc:/switch/blur-nx/logs/renut_001.log` (and `logs/rex/rex_crash.log` if it crashed).

## A console profile (for the hot functions)

Copy `configs/renut_profile.toml` over `renut.toml` and race for 3-5 minutes: the sampler writes `rex_profile.log`
(next to the NRO under `logs/rex/`, or at `sdmc:/switch/` where the homebrew menu starts the NRO with that folder as
its working folder). Send it with `renut_001.log`. With this package's `blur-nx.elf` the samples map to functions: the
machine copies (`tools/switch_hot_bulk.py --merge`) and any hand-written native are made from it.

## A/B timing

Copy `renut_ab.toml` over `renut.toml`, start a race and stand still (or stay in one menu) for 10+ minutes; send
`renut_001.log`. Its "[renut A/B]" lines give each target's frame time with the item off and on; target 1 is the
do-nothing control (a Vulkan-only key).
