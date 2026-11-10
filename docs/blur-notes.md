# Blur port: notes

This file records the port: what each step found, with its evidence. The project is a copy of renut_nx_ultimate_base
(`D:\renut-ultimate\base`, github-main `1fd35fd8`, ports 1-62, its files and not its history), with the base's later
ports 63 and 64 applied (the Viva Piñata session's message of 2026-11-10: `D:\renut-ultimate\tools\ports\p63_*.patch`,
`p64_*.patch`; after them the four files equal the base's `0a4a5ec7` but for line endings). The base, banjo-nx's method
docs, rr6-nx, pacman-nx, guwange-nx and worldkeflings-nx are read-only references. Comments that say "katamari-nx",
"rr6-nx", "pacman-nx", "sasrt-nx" and so on record findings from those ports; this port's own say "blur-nx".

The request (2026-11-10, from 13:39 AEST): port Blur (Bizarre Creations, 2010) to the PC and the Switch with ReXGlue,
the Switch the main goal; work uninterrupted until 06:00 AEST without asking; a package for the console by then.

## Decisions (made without asking, as the request said)

| What | Decision | Why |
|---|---|---|
| Game files | `D:\blur\game` (the disc, 124 files, 5.4 GB, `tools/extract_iso.py`); title update 2's `default.xexp` copied beside `default.xex`; the update's other files in `D:\blur\tu` | the request; kameo-nx's layout |
| `app/assets` | a junction to `D:\blur\game` | no second copy; git-ignored |
| Title update's data files | mounted as `UPDATE:` from an `update` folder next to the executable (pacman-nx's form, `app/src/renut_app.h`), or `--update_data_root` | the console keeps them in a package of their own |
| Internal project name | `renut`, as the base | its CMake, post-codegen passes and glue name `renut_*`; the NRO, its title and the SD folder are blur-nx |
| Generator for the first codegen | the base's built `sdk/out/win-amd64/rexglue.exe` | the request |
| Base ports after 62 | ports 63, 64 applied | the request says "ports 1-62 and later"; the Viva session relayed them for the user |
| Hand-written natives | none; `hand.txt` lists nothing | the request: only from a console profile |
| Register diet (non_volatile_as_local and the rest) | off for the first codegens | it needs this game's own audit on code generated with it (katamari-nx's rc3 lesson) |
| `.ps1` tools | run through the session's PowerShell tool | the machine's default policy refuses `powershell -File` from a plain shell; no policy change |

## The game (2026-11-10)

- ISO: XGD2, partition at `0x0FD90000`, 124 files in 27 folders, 5.71 GB.
- `default.xex` (`tools/extract_iso.py`, `tools/xex_info.py`): title **41560845**, media **01C784D3**, version **0.0.0.1**,
  disc 1 of 1; base `0x82000000`, entry `0x82238148`, image `0xF00000` (15 MB); AES + basic compression; original PE
  name `Amax.exe`; file time 2010-06-17.
- **XDK 2.0.11164** for every static library (XAPILIB, D3D9, D3DX9, XBOXKRNL, LIBCPMT, XPARTY, XMASS, XMP, XONLINE,
  XAPOBA, XAUDIO2, XGRAPHC, XAUD, XMCORE, XHV2; approval 2), LINK 10.0.10224, C1/C2 16.0.10224; imports `xam.xex`
  (244 records) and `xboxkrnl.exe` (340), 2.0.11164, minimum 2.0.8955. **The same XDK as Guwange, A World of Keflings
  and PAC-MAN CE DX's disc** in "XDK versions of our Xbox 360 ports"; much newer than Katamari's 5632 (the base) and
  Ridge Racer 6's 2135.
- **Title update 2** (`TU_10LC225_0000004000000.00000000000G1`, LIVE, content type 000B0000, "Blur Title Update #2",
  884 blocks, 3.4 MiB): `default.xexp` (a delta patch, 1.2 MB: target version **0.0.2.1** from source 0.0.0.1; SHA-256
  `a7f2ef5c...71aeec9`), `loc.pak` (639 KB, the localised strings), `gamedata/TitleUpdate.bin` (51 KB),
  `gamedata/Multiplayer.bin` (104 KB), `gamedata/levels/usa_amboy/actorpack.xt` (360 KB, one level's actor pack) and
  `AvatarAwards` (1.25 MB). So it changes code and the strings, multiplayer data and one level's actors. The generator
  applies the patch itself: "XEX patch applied successfully: base version: 0.0.0.1, new version: 0.0.2.1".
- Sections of the patched image (PE header in the loaded image):

  | Section | Range | |
  |---|---|---|
  | `.rdata` | `82000600-82104814` | |
  | `.pdata` | `82104A00-8213F8E8` | |
  | `BINKCONS` | `8213FA00-82142320` | Bink's constants |
  | `.text` | `82150000-82AF65AC` | code |
  | `BINK` | `82AF6600-82B05C38` | code: the Bink video decoder |
  | `BINKBSS`, `.data`, `.XBMOVIE`, `.tls`, `BINKDATA` | `82B10000-82E0DB48` | |
  | `.XEXID`, `.idata`, `.XBLD`, `.reloc` | `82E0DC00-82ECB570` | |

- Data: `cache/*.pak` (root.pak 512 MB, root_streaming.pak 263 MB, boot.pak 70 MB, gamedata.pak 20 MB,
  **shaders-speed.pak** 4.3 MB, ...), `levels/<track>/Level.level*` (7 tracks on the disc layer, more under `layer1`),
  Bink videos in `Ui/movies` (`.bik`).

## Code generation (2026-11-10)

- Config: `app/renut_config.toml` (the base's audit-free keys; the register diet later), `app/config/renut_funcs.toml`
  (hand entries with their evidence), `app/overrides.toml` (the tools' entries).
- First codegen (222 s of analysis): two "target not in any function", each a tail-call thunk after a terminator:
  `0x823FB490` (`addi r3,r3,64; b 0x823DE0C8`, from `0x823D261C`) and `0x82AF8AA8` in **BINK** (from `0x82A303F0`).
- setjmp `0x8295B940` (hook word check, then `stfd f14,0(r3)`, the image's only one), longjmp `0x8295BC10`
  (`mr r7,r3` ... `lfd f14,0(r7)`, the only one): the base's shapes.
- Second codegen (262 s, 379 files, 196 MB): clean but one "Unresolved conditional branch to 0x82479C58 from
  0x82479C74": the gap fill split a leaf with no `.pdata` entry (a three-word compare, two `blr` exits) at its middle
  `blr`; declared as one function `0x82479C48`-`0x82479C90`.
- **BINK is covered**: the generator's code range runs to the end of BINK (its functions are generated, the thunk
  above among them).

## Renut at XDK 2.0.11164 (2026-11-10)

The base's Renut stands at Beautiful Katamari's XDK 2.0.5632. Blur links XDK 2.0.11164's Direct3D, which three ports
of this family already read: guwange-nx (111 named functions, `guwange_d3d_functions.txt`), worldkeflings-nx (81
names) and pacman-nx at its disc (`git 7f0e95737`: the hook map, device layout and guest data at 11164). Their findings
are references; every address below was measured on Blur's own image.

**The same library build.** `tools/blur_d3d_match.py` pairs functions by shape (instructions with branch targets and
`lis`-built globals left out; function bounds from the generated trees): **77 of guwange-nx's 84 named functions are
the same code in Blur**, as are worldkeflings-nx's. The rest are functions those games do not link.

**The hooks** (`tools/blur_hook_sites.py`, `tools/blur_hook_map.py`, `tools/blur_hook_map_hand.txt`,
`tools/renut_hook_remap_blur.py`, `out/renut_map/hook_map_blur.txt`): 334 hook sites in the base.

- 222 state getters and setters from the state tables: Katamari's `0x82AD8E48` (101 render) and `0x82AD9308` (20
  sampler) against Blur's **`0x82B9A888`** and **`0x82B9AD48`**, every record paired by its default
  (`tools/renut_state_tables.py --ref-render/--ref-sampler`, new options);
- 77 by name through guwange-nx's exact shapes (worldkeflings-nx's agree), 10 by Katamari's exact shape, a few by
  loose shape at >= 0.90 with a 0.10 margin;
- 24 by hand, each by its call chain (`tools/blur_call_index.py`) and Katamari's: the device init `sub_82253B10`
  (Katamari's `sub_824E18D0`, the same calls in the same order) gives XenonGpu_Initialize `82255A10`,
  InitializePresentationParameters `82253608`, SetGammaRamp `8223FBB0` / SetPWLGamma `8223FC80`;
  Uninitialize `82253DE0`, BlockUntilIdle `82249CD8`; DrawVerticesUP `82243058`, BeginVertices `82242BC0`; BeginTiling
  `822551E8` (Katamari's full public form: the count stored at TileCount +12924, ClearZ in f1) and EndTiling
  `82255558`; the lock and surface functions; the bool-constant setters `82244360` / `822443C0`;
- 5 not linked in Blur, left unhooked: GetDeviceCaps (no 304-byte caps copy in Direct3D's range), BeginRingManual,
  DrawIndexedTessellatedVertices, CreatePixelShader (nothing loads the pixel-shader counter beside CreateVertexShader's
  `0x82C82B60`), N&B's SetTexture_Inline;
- 11 are N&B's game hooks in `guest/widescreen.cpp`, already compiled out (`KATAMARI_BANJO_ADDRESS_HOOKS 0`).

One conflict, decided for the name route: LineTexture_LockRect is `82242630` (guwange-nx and worldkeflings-nx agree);
Katamari's 7-instruction shape also fits `82237600`.

**The device layout** (`tools/blur_device_layout.py`, `out/renut_map/device_layout.txt`; `tools/renut_device_offsets_blur.py`,
pacman-nx's tool for the same XDK): Blur's own pairs with Katamari give, where they observe a field, the deltas that
pacman-nx's N&B -> 11164 table gives through each line's `katamari-nx: X->Y` note: VertexDeclaration 11812 -> 11992,
the render-state block +184 (Katamari 11840-12740), IndexBuffer 12428 -> 12612 (0x3144, guwange-nx's d3d.h), InKickOff
13216 -> 13408, CommandBuffer 13492 -> 13728 and the block after it +236, gamma +1920, BehaviorFlags 22264 -> 24192.
The zone N&B 10944-11775 (Katamari -12 / -16) is delta 0 at 11164, as pacman-nx's table says. 152 lines rewritten,
each with `(blur-nx: Y->new)`; guwange-nx's `d3d.h` constants (the same XDK) all agree with the result.

**Guest data** (`tools/blur_data_refs.py`, `tools/renut_port_blur.py`, which lists the evidence of each): format table
`0x82020D38`, full viewport `0x82020CC0` (the same 24 bytes), XDK build qword `0x82B9AE90` (2.0.11164.0), VdGlobalDevice's
record `0x820008A4` (11164's LockResource reads it for a title, `0x820008A0` for the system), XPhysicalAlloc's
no-large-pages flag `0x82C82B4C`, HiZ owner `0x82C82B58`, GetCurrentThreadId `sub_829BA5D8`, XMemFree's thunk
`sub_8217CAA0` (what the device init calls).

**XDK forms checked so far:** SetGammaRamp's entry is Katamari's (`mr r4,r5`: the ramp in r5); the base's hook reads r4,
as pacman-nx noted for this XDK. `banjo_renut_game_gamma` is off in the base, so it does not act; to look at in the
visual pass.

## What belonged to Katamari, taken out (`tools/blur_strip_katamari.py`)

rr6-nx's list (pacman-nx's before it): the N&B / NFS:MW / Mass Effect sources no CMake file builds, Katamari's stage
diagnostic, its tools and console breakdowns, `pgo/` (another game's profile), the icon, the forwarder NPDM. The hot
path's common files come from rr6-nx's ultimate branch with rr6-nx's own three natives also taken out: no hand-written
native, empty machine-copy lists. `katamari_*` names in CMake and the app became `blur_*`.

## Proposed base ports (for the base, not changed here)

| What | Why | Evidence |
|---|---|---|
| `tools/renut_state_tables.py --ref-render / --ref-sampler` | the tool only knew N&B's tables; the base's own reference is Katamari's | used here to pair Katamari -> Blur, 121 of 121 records |
