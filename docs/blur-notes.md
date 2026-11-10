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
- **Jump tables** (`tools/switch_check.py --code 82150000:82B05C38`): 961 `bctr` in the code (240 absolute tables, 6
  byte, 9 halfword, 706 other), 255 switches and 419 indirect calls generated; truncated 0, wrong targets 0, missed 0,
  switches at a call through a function-pointer table 0. "No bound in the code": `sub_828ED868`, bounded by `cmplwi
  cr6,r11,9; bgt` right before it, 10 cases generated: right. Its 4 odd shapes are calls through function pointers (an
  object's slot at +0x27C98, and tables of 12- and 8-byte records at 0x820F8F30 / 0x820F5CA8: every one of the 4 + 26
  code entries a registered function): indirect calls are right.
- **The first PC run** stopped 0.6 s in on "Call to invalid or unregistered function at guest address 0x8233BD60"
  (`li r3,1; blr`, a vtable method after a vcall thunk), the class the sweeps find.
- **Pointer sweep** (`tools/pointer_fixpoint.py`): 26,100 gaps, 64 candidates, **46 accepted** (0x8233BD60 among them,
  stored at 0x82032218, 0x8203CAA4, 0x8203CADC, 0x8203E928); rejected 15 "no code", 2 tail branches into a function
  body, 1 fragment; fixpoint in pass 2.
- **Gap sweep** (`tools/gap_sweep.py`): 233 accepted in pass 1 (617 candidates), then one in each of passes 2-4;
  fixpoint in pass 5.
- **Labels taken back out** (`tools/override_labels_check.py`, rr6-nx's copy of pacman-nx's tool: 6 suspicious of
  282): `0x8223CF94` (an SEH scope label, `mr r8,r8`, branched to by its parent), `0x824B4638` and `0x82572B50` (right
  after a `bl`: its return point), `0x829629E0` (`li r3,8` runs into it), `0x8296B274` and `0x82993B50` (epilogue tails).
  And `0x8270FEAC`, a pointer-sweep pick "built at 0x8270FE9C": the base of a jump table (`lis r12,0x8271; addi
  r12,r12,-340; lwzx; mtctr; bctr`, its words case addresses), katamari-nx's known false pick, found by
  `tools/read_before_write.py` reading r16 at its "entry"; no other "built at" entry has that shape. **275 entries
  remain** in `app/overrides.toml`.
- **The register diet** (`non_volatile_as_local` with v64-v127 kept, `non_argument_as_local`, `elide_lr_stores`):
  - `tools/non_argument_audit.py app/generated --noreturn=8295BC10` on code generated without it: ENTRY 318 lines (MSVC
    exception funclets reading their parent's frame in r12, nearly all), RET 30, HANDOVER 0; `tools/diet_keep_list.py`:
    100 ENTRY and 45 RET functions keep those registers in the context (`non_argument_as_local_keep`).
  - `tools/read_before_write.py app/generated --noreturn=8295BC10` on code generated with it: 13 functions, none needing
    `share_registers`: 6 are setjmp callers' `env` copies (the generator's), 1 is setjmp itself, 2 `r30 ^ r30` and 2
    `~x + x` carry forms (results independent of the register), `sub_825F8A98` rebuilds v27-v29 lane by lane with
    `vrlimi128` masks 8, 4, 2, 1 (a transpose: the old value is never used), and `sub_8296FC44` is a data word after a
    `blr` (`0x8296FC70`) the generator itself registered, never run.

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

**Hooks Katamari's XDK never linked** (the base's 39 `BANJO_RENUT_UNHOOKED_*` bodies, which the first remap did not look
at: found when the first Renut run crashed):

- **D3DVertexShader_Bind is `sub_82251090`**, XDK 11164's form: (pShader r3, Flags r4, pDecl r5, pStreamStrides r6,
  pPixelShader r7). It is the closest function of Blur's Direct3D range to N&B's `sub_8263FBD0` (0.56; the next 0.31);
  its callers (game code `sub_8232ECE8`, `sub_8232FF60`) pass a vertex shader, 0, a declaration made from a
  0xFF-ended element list (`sub_823310E0`), a stride array and a pixel shader. Unhooked, it ran on Renut's declaration
  record, read a garbage element count at +28 and walked off the guest stack (`renut4`: "read of guest 0x703A0000").
  Hooked through `D3DVertexShader_Bind11164_hook` (Flags and the pixel shader dropped: Renut keeps the declaration and
  strides, and runs no microcode to patch).
- **XGRegisterPixelShader is `sub_82244630`** (guwange-nx's and worldkeflings-nx's exact shape): Blur makes its pixel
  shaders through it (CreatePixelShader is not linked); its 14 instructions write exactly what Renut's hook writes.
- Linked but left unhooked: GetGpuTextureDimensions `822408F8` (only Resolve calls it, which Renut replaces);
  BeginExport `82245CA8` / EndExport `82245DB0` (only `sub_822595F0` calls them, reached only from D3DDevice_Swap
  `82254790`, which Renut replaces: pacman-nx's finding at this XDK); the public BlockOnFence `82248B38`,
  AllocateInlineData `82248938` and N&B's tiling forms (the hooked paths cover them, as in Katamari).

**The device's own declaration** is at +0x3050 here (`kDeviceDeclarationOffset`): Blur's InitializeApiState stores it
with `stw r11,12368(r31)` at `0x822534B8` (Katamari's 0x2F98 moved by the render-state block's +184). At 0x2F98 Renut
dropped every draw on it ("vertex declaration BBC93050 is not one the renderer knows").

**The path wizard**: on the PC, Renut's builds open a path dialog when no saved path config exists, and wait for a
click: the first runs looked like a hang right after "Swapchain: 3 images". A game folder given on the command line
(or in the toml) that holds `default.xex` now skips it (`app/src/renut_app.h`, OnFinalizePaths).

**Argument forms that differ at XDK 11164** (each read in Blur's code; the hooks' parameter lists compared with
guwange-nx's and worldkeflings-nx's, which were adapted to this XDK):

| Function | Katamari's (the base's hook) | Blur's | Evidence |
|---|---|---|---|
| D3D_Lock2DSurface `82241D20` | (texture, slice, level, rect out, rect, flags) | an unused 0 second argument first | its three thunks (`82242630`, `82242940`, `82242550`) all pass `li r4,0`; the game cleared rows at pBits 0x00100000 before |
| D3D_LockSurface `82241C48` | (texture, slice, level, flags, 4 out-pointers) | the same extra argument | Lock2DSurface passes its out-pointers one register later, the last on the stack |
| D3D_LockResource `82246230` | (resource, type, ...) | the same extra argument | LockSurface passes the blocker type 14 in r5, not r4 |
| D3DDevice_SetGammaRamp / SetPWLGamma | (device, ramp) | (device, flags, ramp) | `mr r4,r5` at entry; guwange-nx's and worldkeflings-nx's hooks |
| D3DVertexShader_Bind `82251090` | (shader, declaration, strides) | (shader, flags, declaration, strides, r7) | above; a fifth argument guwange-nx calls an index |
| D3D_Lock3DSurface `82241DB0` | (texture, level, box out, box, flags) | the same | Volume_LockBox's thunk `822429B0` |

pacman-nx found the first three at XDK 21250 (its TU3) and wrote them as that update's change; Blur shows they are
already in 11164.

**Objects:** vertex shaders keep D3D's own layout (Blur's XGRegisterVertexShader `82245020` writes exactly what Renut's
hook writes: an 872-byte header, the physical part at +32, the container at +872, slot tables at +896, 416-byte
binds), so the game's GetFunction calls (`82244EF8` pixel, `82244F50` vertex) work on them. **Vertex declarations are
Renut's own records**, so every D3D function that reads a declaration's insides must be hooked: D3DVertexShader_Bind
and D3DVertexDeclaration_GetDeclaration (`82244C50`, count at +24, elements at +52 in D3D's; the game asked it for the
elements of a declaration and walked a garbage count off its stack, a host crash 20 s in).

**Unhooked Direct3D functions game code calls** (`app/generated`, calls from outside 0x8223D000-0x82256400 into it, the
hooked set taken out): the float constant setters `82244170` / `82244248` (they write the device's own constant
block at +0x780 / +0x1780, which Renut reads, as in Katamari), and getters of device fields Renut keeps at their 11164
places (viewport +12824, streams +12636, index buffer +12612, textures +12720, shaders +12868 / +12872, declaration
+11992, constants). SetVertex/PixelShaderConstantI (`822444A0` / `822444F8`) are hooked, as pacman-nx did at this
XDK.

**Command buffers.** Blur records Direct3D command buffers (Katamari's XDK linked none, so the base carries N&B's
bodies unhooked). Matched with N&B's own functions (banjo-nx's title update 3 image) over Blur's Direct3D range:
BeginCommandBuffer `8224A918` (0.82, the next 0.23), EndCommandBuffer `8224AE70` (0.99), RunCommandBuffer `8224B248`
(0.97), SetCommandBufferPredication `8224A310` (1.00). Over the four pairs 322 displacements are the same and every one
that differs is a device word, so the buffer object's layout is N&B's. End, Run and the predication keep N&B's
arguments; Begin has the first release's unused pPersistTags before pTilingRects (r3-r9, where N&B's uses r3-r8),
which the base's own comment says TU3 dropped. Unhooked, D3D's RunCommandBuffer walked a buffer recorded on Renut's
device and read guest 0x00100000 (`renut10`).

**A correction to pacman-nx's zones at this XDK:** N&B's tiling Flags word 13196 (0x338C) is at **13364** here (+168),
not 13372 (+176): Blur's BeginTiling stores it with `stw r25,13364(r31)` at `0x822552C4`, right after the tile rects
13356 and 13360 (+168). The +176 starts at the clear values (13376 / 13384 the colour, 13392 ClearZ, 13396
ClearStencil, which match pacman-nx's). The two lines that write it are fixed (`device.cpp`, `draw.cpp`). The command
buffer pairs also show N&B 13348 -> 13544 and 13356 / 13360 -> 13556 / 13560 (+196 / +200, where pacman-nx's zone
says +180); no line of Renut's uses those words.

**The run-time vertex shaders.** About 1,000 of Blur's vertex shaders are made at run time and captured by Renut
(`shader_capture`), but the PC build's XenosRecomp skipped every one without a message, so they stayed "not in the
shader cache" (1,547 cache entries for 2,544 containers). Their container is `0x102A1121`: the 0x20 bit marks a vertex
shader with two bindings (the bit Renut's XGRegisterVertexShader hook already reads as two slots), whose second
binding's tables sit at header words 0x1C and 0x20, which XenosRecomp's dump reader required to be 0. One microcode
serves both bindings and Renut takes the input layout from the declaration, so the reader now accepts them when the
0x20 bit is set (`app/thirdparty/reblue-XenosRecomp/XenosRecomp/main.cpp`).

**XDK forms checked so far:** SetGammaRamp's entry is Katamari's (`mr r4,r5`: the ramp in r5); the base's hook read r4:
fixed above. `banjo_renut_game_gamma` is off in the base, so it does not act; to look at in the visual pass.

## What belonged to Katamari, taken out (`tools/blur_strip_katamari.py`)

rr6-nx's list (pacman-nx's before it): the N&B / NFS:MW / Mass Effect sources no CMake file builds, Katamari's stage
diagnostic, its tools and console breakdowns, `pgo/` (another game's profile), the icon, the forwarder NPDM. The hot
path's common files come from rr6-nx's ultimate branch with rr6-nx's own three natives also taken out: no hand-written
native, empty machine-copy lists. `katamari_*` names in CMake and the app became `blur_*`.

## Proposed base ports (for the base, not changed here)

| What | Why | Evidence |
|---|---|---|
| `tools/renut_state_tables.py --ref-render / --ref-sampler` | the tool only knew N&B's tables; the base's own reference is Katamari's | used here to pair Katamari -> Blur, 121 of 121 records |
| The build's shader dump rule (`gpu.cmake`) calls `app/tools/dump_shaders.py`, which is not in the tree | a new port's first PC build stops there unless `out/renut_shader_dump` exists | build 1 here; `tools/renut_shader_dump.py` (the documented step) made it |
| `tools/localize_native.py` creates `generated/hot` before writing | a fresh tree has none; the first build of a new port stopped there | build 2 here (katamari-nx's log mentions the same restart) |
| The renut_recomp PCH depends on the post-codegen step | the PCH compiles while `post_codegen.py` still edits `renut_pch.h`, and every renut_recomp object then fails ("has been modified since the precompiled header"); a second build goes through | builds 6, 9, 11 here, every time post-codegen re-ran |
| On the PC, a game folder given on the command line skips the path wizard (`renut_app.h`) | with no saved path config, Renut's PC build waits for a click on a dialog: a new port's first runs look like a hang after "Swapchain: 3 images" | runs pc1-renut3 here |
| XenosRecomp's dump reader accepts two-binding vertex shaders (container flags 0x20, words 0x1C / 0x20 set) | they were skipped without a message: ~1,000 of Blur's run-time vertex shaders never reached the cache | `app/thirdparty/reblue-XenosRecomp/XenosRecomp/main.cpp`; the installer's wasm translator would need it too |
| The base's XDK notes for XDK 11164 and later: Lock2DSurface / LockSurface / LockResource / BeginCommandBuffer have the first release's unused argument, SetGammaRamp / SetPWLGamma a flags argument | a port at 11164 or later from the base meets them all (pacman-nx met three at 21250) | the table above |
