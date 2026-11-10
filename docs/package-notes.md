# blur-nx test package: what to test, and how

Written 2026-11-10, finished 2026-11-11 for this package. The package holds the NRO, three settings files, the configs,
the DKSH shader cache, the NRO's ELF and the source snapshot; no game data.

## Setting up

1. Copy `COPY_TO_SD_switch_blur-nx/` to `sdmc:/switch/blur-nx/` on the SD card.
2. Extract your own Blur disc into `sdmc:/switch/blur-nx/game_root/` (default.xex and the rest). Title update 2:
   its `default.xexp` next to `default.xex`, its other files in `sdmc:/switch/blur-nx/update/`.
3. Start `blur-nx.nro` from the homebrew menu (title override / full RAM: the game needs the memory of a game launch).

This build reads `renut.toml` next to the NRO, the shipped settings with logs. For playing, copy
`configs/renut_play.toml` over it.

## What to test first

- It boots through the logos and the attract video to "Press START", and the career menus draw.
- A race (Barcelona Oval, the first Proving Grounds event): 20 cars, the HUD, power-ups, the motion blur at the screen
  edges. Please look at the whole screen (earlier builds drew garbage under the race in Ryujinx).
- Your car from behind: the rear window shows the car's interior through the glass, the paint reflects the
  surroundings, and the tail lights throw a red glow that spreads sideways. These were fixed on 2026-11-11 (the
  environment cube's faces, `blur_renut_ms_slice_resolve`; the occlusion answers the light glows wait for); compare with
  your memory of the Xbox 360.
- Frame rate: the log's 10 s lines (`logs/renut_001.log`, "game frames (N fps)") give it. **The game's speed follows
  its frame rate below 15 fps**: Blur gives each frame at most 1/15 s of game time, so at 10 fps the race runs at 2/3 of
  real time (from 15 to 60 fps it keeps real time). In Ryujinx the race ran at 10-14 fps (0.67-0.93 of real time); the
  console's own rate is what decides it.

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
do-nothing control (a Vulkan-only key). The list includes this port's correctness fixes (211-221: the resolve and
EDRAM fixes, the occlusion answers, the cube-face resolves): their A/B gives each one's cost on the console and, with
the item off, how the picture looked before it.

## What was tested here

- PC (D3D12, the shipped settings): the race against the SDK's emulated renderer at the same grid spot: the motion
  blur, the glow, the bloom, the tail lights' glow and the rear window now match; the frame overall is within about 10%
  of the reference's brightness (the car's light depends on where it stops: in the trees' shade the game lights it
  less, in both renderers).
- Ryujinx (this NRO's code, 2026-11-11, emu11): menus, a 9-minute race, 0 verify differences, 0 memory emergencies.
- Not tested on the console by me (the console was another session's); the configs above are for that.
