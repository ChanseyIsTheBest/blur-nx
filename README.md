<p align=center><img src="tools/switch/icon/blur_icon.jpg" alt="Blur" width="256" height="256"></p>

<h1 align=center>Blur: Nintendo Switch port</h1>

A port of the Xbox 360 game **Blur** (Bizarre Creations, 2010) to the Nintendo Switch, with a PC build as the step on
the way.

It's not an emulator. The PowerPC code of the game's `default.xex` (with title update 2) is statically recompiled to C++
with [ReXGlue](https://github.com/rexglue/rexglue-sdk), and the game's Direct3D calls are drawn by
[reNut](https://github.com/masterspike52/reNut)'s Direct3D-level renderer, on the Switch GPU's own API, deko3d. The port
is built on [renut_nx_ultimate_base](https://github.com/ChanseyIsTheBest/renut_nx_ultimate_base), which gathers the
fixes and optimisations of the other ports.

> [!NOTE]
> You need your own copy of the Xbox 360 disc and its title update 2. Game data is never part of this repository or its
> packages.

## How to install

1. Open the installer page: **https://chanseyisthebest.github.io/blur-nx-installer/**
2. Choose your game (the disc image or an extracted folder), then title update 2 (its Xbox 360 package, or the folder
   it was extracted to), then any DLC packages, and press **Create blur-nx.zip**. Everything runs in your browser and
   your game files stay on your computer. The page makes the game's shaders for the Switch from your own files, and
   packs the level files with LZ4 (the rest of Blur's data is compressed already).
3. If you have an older blur-nx, delete its `switch/blur-nx/game_root` folder. Then extract `blur-nx.zip` to the root of
   your SD card. It makes `switch/blur-nx/`.
4. In **Sphaira**, find Blur, press **X**, choose **Install Forwarder**, set **Address Space** to **39-bit**, choose
   **Create Forwarder**, and start the game from the Home Screen. Start it from the forwarder: that is what gives it
   four CPU cores.

Your options are in `switch/blur-nx/blur.toml`, which the game writes at its first start: the language (by default the
Switch's), the frame rate cap, the handheld and docked resolutions, SMAA, MSAA, four-core mode, the button layout and
the game menu's look. Every other setting is built into the NRO (`app/blur_switch.toml`). Hold **A+B+X+Y+L+R** during
the game to open the game menu: the settings, the game's achievements and each player's controls.
`dist/switch/README.txt` has the details.

## State

- **Plays on the console:** boot, menus, career, races with every effect of the game's picture (motion blur, bloom,
  glow, reflections), at 30 fps most of the time at stock clocks. In a race: 29.3 fps on average with the cap at 30,
  88 % of frames on time, 1 % lows about 18 fps (2026-11-18, handheld, four cores).
- **Faster:** the release NRO is built with link-time optimisation and a profile (PGO) from a race on the console, the
  hottest game functions as checked machine copies and hand-written natives, and the base's renderer and CPU work.
- **Not tested:** split-screen multiplayer, the DLC. Online play needs Xbox LIVE and is not supported.
- **Known:** at very low frame rates (about 15 fps, seen only in instrumented test builds) the picture can ghost; at the
  release's frame rate it does not.

Bug reports: add `log_level = "info"` at the top of `switch/blur-nx/blur.toml`, play until the problem happens, and
include the `switch/blur-nx/logs/` folder. The release writes no log in normal play; a crash writes
`switch/blur-nx/logs/rex/rex_crash.log`.

## Documentation

| File | What it holds |
|---|---|
| `docs/blur-notes.md` | This game: decisions, code generation, game fixes, the renderer at XDK 2.0.11164 |
| `docs/progress-log.md` | The work in order |
| `docs/building.md` | How to build (the base's guide) |
| `docs/hand-and-machine-optimising-guide.md`, `docs/optimising-functions.md` | The hot-function method |
| `docs/off-by-default.md` | The base's fixes that are off, and why |
| `docs/base-README.md`, `docs/ultimate-fixes.md` | The base this port is built on |
| `dist/switch/README.txt` | The player's guide: the SD card's layout, the settings, the game menu |

## Credits

- **reNut** by masterspike52: the renderer. Its author permits reuse in these ports.
- **renut_nx_ultimate_base** and the ports its fixes come from (katamari-nx, banjo-nx, sa2-nx, rr6-nx, sasrt-nx,
  kameo-nx and the others); **banjo-nx** for the renderer's adaptation and the method docs.
- **ReXGlue** for the recompiler and SDK; **NetherSX2_nx** by NaGaa95 (the four-core layout, MIT), **battd_nx** (the GPU
  arena), **bejeweled3-nx** and the **Zuma** ports (the hot-function method).

`THIRD_PARTY_NOTICES.md` has the full list and each project's license. GPL-3.0 where the sources are (`LICENSE`).

Blur is a trademark of Activision Publishing, Inc.; the icon artwork is © Activision. This project is not affiliated with
or endorsed by Activision, Bizarre Creations, Microsoft or Nintendo.
