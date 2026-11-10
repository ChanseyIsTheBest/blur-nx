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
| Register diet (non_volatile_as_local and the rest) | off for the first codegens (on later: below) | it needs this game's own audit on code generated with it (katamari-nx's rc3 lesson) |
| `.ps1` tools | run through the session's PowerShell tool | the machine's default policy refuses `powershell -File` from a plain shell; no policy change |
| Register diet | on since 15:55 (`non_volatile_as_local`, `non_argument_as_local` with 131 kept, `elide_lr_stores`), after the audits (`out/nonarg1.log`, read-before-write) | the codegen notes below |
| The Switch test NRO | built with `BLUR_BUILTIN_CONFIG=OFF`: it reads a full `renut.toml` next to it (the package's `renut.toml` / `renut_play.toml` / `renut_ab.toml`, copied over it) | the request's package is three full tomls; the built-in pair was Katamari's (log off, MSAA 4) |
| SMAA (`banjo_renut_smaa`) | off in Blur's tomls; `configs/renut_smaa.toml` turns it on | banjo-nx ships it on (its user's call); it changes the picture, so here it is the user's decision |
| The game's gamma ramp (`banjo_renut_game_gamma`) | on in Blur's tomls; `configs/renut_gamma_off.toml` | the title matches the reference with it (below); its console cost is the user's to weigh |
| The reference | `--banjo_renderer=emulated --gpu_plugin=xenos --d3d12_readback_resolve=true` | the user's tip (18:40) |
| Builds | a PC and a Switch build may run at once | the user, 19:50 |

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
fixed above. `banjo_renut_game_gamma` is off in the base; Blur ships it on (the title against the reference, below).

## The reference and the picture (2026-11-10)

**The SDK's emulated renderer (`--banjo_renderer=emulated --gpu_plugin=xenos`, Direct3D 12) cannot serve as the
reference for Blur's 3D scenes.** It boots the game (the CPU side is the same) but its own resolves fail: "Resolve
region 0 <= x < 1280 is outside the surface pitch 320", 10,902 times in an 8-minute run (`ref_route1`); the title
screen's car is a pink-white blob and the race is washed out nearly to white (the exposure chain reads the failed
downsamples). Its ROV render-target path (`--render_target_path_d3d12=rov`, Xenia's exact EDRAM model) is the same
(`ref_rov1`: the white blob at 122 s, a white frame at 45 s). So the reference is used for what it draws right: the
2D layers (menus, HUD, text), the Bink videos and the loading screens; the 3D frames are checked for consistency on
our own renderer (and against the console when a log comes back). Fixing the emulated renderer's resolve is outside
this port's renderer; noted for the base.

The user's suggestion (18:10): switch the enhancements and speed-ups off and bisect. Done for every run-time one:
`--banjo_cpu_enhancements=false --banjo_noncpu_enhancements=false` (the log: "cpu master OFF, non-cpu master OFF",
none active), `ref_off1`: the same white blob on the title and the same resolve error (2,096 times in 200 s). So no
run-time enhancement does it; nothing to bisect there. What the master keys cannot switch is build-time: the codegen
options and post-codegen passes, which rewrite the generated code itself. A CPU-side mistranslation inside D3D's own
Resolve would show only in the reference (Renut replaces Resolve) and would look like this; checking it needs a PC
build of code generated without the passes (a separate tree and about 40 minutes): not done yet, listed under "Next".

**The fix for the reference (the user's tip, 18:40): `--d3d12_readback_resolve=true`.** Xenia Canary's own
compatibility report for Blur shows the same white 3D with today's emulated renderer and an older Xenia's report
calls it playable with readback resolve on; so it is here (`ref_rb1`): the title's car, the menus and the race draw
right. Every reference run from then on: `--banjo_renderer=emulated --gpu_plugin=xenos --d3d12_readback_resolve=true`.
The resolve fault above is the emulated renderer's (its non-readback resolve path), not this port's; noted for the base.

**Comparisons with the reference (`tools/blur_pair.py`: side by side, a difference image, mean |difference|):**

| Scene | Before | Fix | After |
|---|---|---|---|
| Title ("Press START") | mean diff 2.9 / 7.6 / 9.0 (ours brighter in the darks) | the display's gamma ramp the game loads (`banjo_renut_game_gamma`, a base key that was off): on in Blur's shipped toml, `configs/renut_gamma_off.toml` without it | 0.3 / 1.3 / 1.5 (`gamma1`) |
| Race (Barcelona Oval, first seconds) | ours darker (means 56 / 61 / 52 against 67 / 71 / 59), the tail lights' glow and the bloom missing, the car's rear window black | the 640x360 HDR downsample: resolved as 16_16_16_16_EXPAND, sampled through another header as 16_16_16_16_FLOAT; Renut made that texture from guest memory, which the resolve never wrote (the log's "[flash] ... made from guest memory over resolve destination ... 640x360 format 29"). `blur_renut_expand_alias` (A/B 211, on): the two formats are the same half floats in the reference (the SDK's d3d12 `texture_cache.cpp`), so the texture reads the resolve's own image | the texture reads the resolve (route7's log), but the picture did not change: see the next row |
| Race, the post pass that reads last frame's 640x360 image | the same 640x360 image empty every frame | each frame, before the image's own resolve, Blur resolves a 1x1 2_10_10_10 target into its corner (the console converts into the destination's format and leaves the rest); Renut made a resolve destination again whenever the source's format differed, so the whole image was made again, empty, for that pixel (`trace2`: "resolve colour 7010F6B0 (1x1 fmt 11) -> texture 44ED87A8", then the image read from guest memory). `blur_renut_resolve_keep_dest` (A/B 212, on): a single-sampled resolve of part of an existing destination of another format keeps it and writes its part with a draw that converts (`DrawResolveCopy`'s pipeline now by the destination's format) | the image is kept and read (`route8`: "keeps it and converts", then "reads resolve destination ... through its texture"); frames clean |

**What the race comparison can and cannot say (20:00).** The reference's race frame and ours are not the same moment:
the reference's car shows 3 mph with its brake lights on (the red glow round the tail lights, the lit rear window),
ours 0 mph; the eye adaptation (the 1x1 luminance chain) depends on how long the race has run. So the remaining
difference in brightness (ours about 10 lower in the means) is not yet a renderer finding: it needs the same moment
in both (the same scripted presses from a save at the grid, or a capture at a fixed frame after the race loads).
`banjo_renut_diag_resolve_trace` now also lists, in its frame, resolves sampled through another header and textures
made from guest memory over a resolve destination ("sample GUEST"): the traced race frames show none left but
ordinary textures in memory a resolve once used.

**The request's list, item by item (20:05).** Depth bias per draw: dynamic state, set when the game's words change
(rr6-nx's `rr6_depth_bias`, on; never in the pipeline key). Sampler LOD bias: the fetch constant's (MipMapLodBias / 32)
in each sampler. Foliage alpha: alpha test and alpha to coverage are applied (the depth-only census: 0 depth-only draws
with a pixel shader drawn without them). Resolves sampled as textures: the two fixes above, and the trace's "sample
GUEST" check in the race. User clip plane: Blur uses plane 0 on 16 vertex shaders (the PC log's "user clip plane"
lines: applied); the rear-view mirror draws.

**The EDRAM views of one surface (`blur_renut_view_reinterpret`, A/B 210, off).** Blur draws a 1280x720 4x MSAA
target as 2_10_10_10_FLOAT (7e3) and resolves it through an 8_8_8_8 view (and once at 320x180 the other way: drawn as
16_16_16_16_FLOAT, resolved as 8_8_8_8): the console reads the same EDRAM words, Renut's views are separate host
targets ("Resolve through a view of format 3F of EDRAM last drawn as 06: its pixels are not carried over"). A pass
that carries the words over (`shaders/host/view_reinterpret*.hlsl`, 7e3 bits <-> bytes, per sample) was written and
tried: the bytes-to-floats direction broke the tiled frames (rows 256-719 black: a tiled frame's later tiles read the
words of the first), the floats-to-bytes direction brought the race's means to 63 / 68 / 57 but made the HUD's panels
pale. Not proven, so off; the key and the A/B stay for the console's log and a later look.

**Renut on Vulkan (`renut13`, `route2`)**: the logos, the title with the car under the logo, the Bink attract video,
the menus (single player, career, Proving Grounds), the tutorial videos, car select and a Barcelona Oval race with
its HUD, power-ups, smoke and track: 120 fps on the RTX 3060 (the PC's vsync), render thread 0.3 ms and GPU 2.3 ms a
frame at the title, no errors in the log.

## The Switch build in Ryujinx (2026-11-10)

**emu2 (20:01, host-mapped memory):** the logos, the profile screen at 94 s, the showroom loading; Ryujinx itself died
2 minutes in ("Fatal error. 0xC0000005" in its translator: in host-mapped mode a bad access in the NRO kills the
emulator, no crash log). **emu3 (20:10, the software page table in `ryu-data\blur\Config.json`, Blur's own folder;
the host-mapped file kept as `Config.json.hostmapped`):** further, to 328 s of game: the Bizarre Creations logo, a
loading screen, the Bink video, the career menus, Proving Grounds, the showroom car: all drawn right, 0 verify
differences. Then "Invalid memory access at 0x0000736472616328" (the bytes of "(cards": a pointer made of text) at
`renut+0x20792a0`, `FastCensusDraw` (addr2line on `app/out/switch/renut`), called from `DrawStateLocked`.

**The cause: a race in a diagnostic.** `banjo_renut_diag_fast_census` (on in the test toml for the guide's census)
counts field combinations in an `unordered_map` from `FastCensusDraw`; `FastCensusReport` walks it and replaces it.
On the PC the draws run on the game's thread, where the report runs too (`StatsGameFrame`); with deko3d's queued
backend the draws run on the render thread, so the report replaced the map under an insert. Both now hold one mutex
(`render/draw.cpp`, `g_fast_census_mutex`; diagnostics only). A base port (below).

**emu4 (20:40, host-mapped memory, the census fixed):** 10 minutes, no crash, 0 verify differences; title, career
menu, the Barcelona Oval intro and the race. In the race most frames are garbage below the top ~256 rows (colourful
noise): Blur resolves its 1280x720 4x scene in three tiles (rows 0-256, 256-512, 512-720), and plume's deko3d region
resolve is a 2D-engine blit of the multisampled image, of which only the top tile arrived in Ryujinx (no warning in
either log; whether the console's 2D engine does the same is not known). `blur_renut_tile_resolve_staging` (A/B 214,
deko3d, on): the target resolved whole into a single-sampled staging image (plume's own resolve), each tile copied
from it; the staging is made again only when the target was drawn or cleared since (a tile's own clear-after does
not count), so once a frame (`render/draw.cpp` `TileResolveStagedLocked`).

**Occlusion queries (20:30).** `D3DQuery_GetData` (`sub_8224CD70`) is called about 130 times a second in a race, every
one an occlusion query (type 9), and under Renut its sample blocks never hold D3D's 0xFFFFFEED sentinel or any GPU
count (zeros, then leftover memory): the game read 0 or garbage samples. `blur_renut_occlusion_queries` (A/B 213, on)
answers every issued occlusion query as the emulated GPU does (1000 samples passed). Post-codegen had made GetData's
two callers direct `__imp__` calls, so they were pointed at the hooked entry in the generated code
(`renut_recomp.104.cpp`, `renut_recomp.26.cpp`; post-codegen writes the same once it runs again). The glow did not
come back with it (`query2`).

**The glow and the dark rear window, with RenderDoc (20:40, D3D12 through renderdoccmd; the Vulkan layer's capture
crashed the app).** The scene pass is right (the rear window shows the interior). The 640x360 half-float image the
composite adds is a motion-blur pass (`ps_bbd6c08b4ae59fb0`: g_tDepth, g_tColour, g_tNoise, a reprojection matrix,
steps along the motion) whose colour input is the 640x360 texture itself (last frame's result) plus a 1x1 resolve
into its corner from a temporary surface on the stack (0x7010F6B0, cleared, 1x1 in its header); it is all zero, so the
blurred highlights (the tail lights' spill) add nothing. Where the console puts the scene into that image is not yet
found: the next thing to look at.

**Found (21:10): the motion-blur source is a temporary surface on the stack.** The resolve trace with the source
header's words (`diag3`): the surface at 0x7010F6B0 reads size 0, info 0, format 0 at replay, and its "format" even
changed between runs (2_10_10_10 in one, 8_8_8_8 in the next): Renut's render thread reads a surface's header when it
replays the frame, by which time the game's function has returned and its stack is reused. When the game bound it, the
header described the real source of the 640x360 image. `blur_renut_stack_surface_headers` (A/B 215, on): a surface
bound by SetRenderTarget / SetDepthStencilSurface whose header is on the stack (0x70000000-0x7FFFFFFF) keeps its header
words as they were then (`render/surfaces.cpp` `NoteSurfaceHeader`; the render thread's `GetHostSurfaceLocked` reads
the copy). The composite (`ps_b20cdc973266527b`) pulls the scene towards the motion-blur image by a radial edge term
and the image's alpha, adds the lens image and the bloom squared, then tone-maps through a 3D lookup: with the image
all zero the frame was darkened towards the edges and had no motion blur. The bloom chain reads the scene copy and a
256x1 curve the CPU uploads (values up to 15 of 255); the CPU locks only Blur's six 1x1 luminance resolves, which the
base's readback covers.

**The whole chain, with the reference's own capture (22:00-22:50).** A RenderDoc capture of the reference (renderdoccmd
on the emulated renderer, `rdc_trigger.py` at the grid) shows the motion blur's colour input is the half-size scene,
and how the console makes it: the game draws the frame into its 1280x720 single-sampled half-float target (base 0,
pitch 1280), binds a temporary 640x360 4x half-float surface over the same EDRAM (base 0, pitch 640: the same sample
grid, each 640x360 pixel's four samples are a 2x2 block of the 1280x720 one) and resolves it: the 4x resolve averages
the samples, a free 2x downsample. Renut had three things in the way, each now fixed under its own key:

| Key (A/B) | What | Before |
|---|---|---|
| `blur_renut_stack_surface_headers` (215, on) | the temporary surface's header is on the stack; the headers of stack surfaces bound as render targets or depth are copied when a clear or resolve is queued and used by the render thread for that operation (`backend/queued.cpp`, `CaptureStackHeaders`); draws use the header as last bound | read at replay, after the stack was reused: an empty 1x1 surface (or, from leftover words, 12120x26733 and the like: in Ryujinx each failed allocation set off a memory emergency, and the races were drawn over garbage) |
| `blur_renut_sample_grid_resolve` (216, on) | a 4x surface over a single-sampled one of the same format at twice its pitch resolves as the 2x2 box of the big one when that was drawn after the view (`DrawBoxDownsampleLocked`, the mip shader) | the 4x view's own host target, empty |
| `blur_renut_wipe_overwritten` (218, on) | vivapinata-nx's EDRAM wipe (a clear through another layout, applied at the wiped target's next resolve when it was not written since) is left out when another target over the same EDRAM base was written after the wipe | Blur's own clear of the 640x360 view earlier in the frame was applied at the resolve, over the scene drawn through the 1280x720 view: a black frame |
| `blur_renut_surface_size_guard` (217, on) | a header over 8192 wide or tall makes no target | the failed allocations above |

With the four: the race draws with its motion blur (the soft screen edges of the reference), the brightness close to
the reference's (`glow5`: means 55-65 against 67). The tail lights' wide red spill is still to compare at the same
moment (the reference's car was braking).

**emu9 (22:50, the Switch build with the four keys):** the race in Ryujinx draws clean (no garbage), with its HUD and
power-up effects; 0 memory emergencies, 0 verify differences; the 2x2 resolve and the stale-wipe rule act on deko3d too.

**Done properly (the user, 22:55: "no shortcuts, as long as you need"; 23:00-).**
- The stack headers travel with every operation that replays a device snapshot: queued draws (whole and delta
  captures), queued clears and resolves (in the pooled capture), and recorded command buffers (a list on the buffer by
  op index, filled in `NewOp`, set at the four replay sites); the last-bound table is only a counted fallback ("stack
  surface ... read with its last bound header": none expected).
- EDRAM tiles exactly (`render/surfaces.cpp` `EdramSpanOf`): 80x16 samples a tile (40x16 for the 64-bit formats), 4x
  samples 2x2 a pixel, 2x 1x2, a surface's tiles row by row from its base modulo 2048. The wipe-overwritten check is a
  tile-range intersection (was: the same base tile), and vivapinata-nx's wipe marks a target only when the clear fills
  every one of its tiles whole (was: the same base and a tile count no greater than the clear's).
- The sample-grid resolve has its own shader (`sample_grid_resolve_ps.hlsl`): any source rect and destination point,
  4x (2x2) and 2x (1x2) views; the mip shader's box (origin only, 4x only) is gone.
- GetData's callers: the code regenerated and post-codegen run again (no hand edit of generated code). Deleting
  `generated/codegen.build.stamp` alone reruns rexglue, but rexglue keeps its own input fingerprint in
  `generated/codegen.stamp` and writes nothing when the inputs are unchanged: only post-codegen ran again (23:02, the
  hand edit still there). A true regeneration deletes both stamps (build 40).

**The tail lights' glow (23:40-00:00), from the two captures side by side.** In the reference the red spill is not in
the scene: it is a layer the final pass adds (an additive full-screen draw of a 320x180 texture, after the HUD, then
~20 single-point draws with no pixel shader: the lights' visibility tests for the next frame's queries). The game
draws its light glows (30-40 sprites) into a 320x180 4x 8-bit target, resolves it, and blurs it down a chain
(320x180 -> 160x90 -> 80x45 -> 40x22 and back) whose half-float steps are resolved with flags 0x2C000000 into
k_16_16_16_16 destinations. The flags' top six bits are D3DRESOLVE_EXPONENTBIAS, +11: the game's D3D (the generated
`sub_82251A80`: `flags >> 26` packed with the destination format and moved into RB_COPY_DEST_INFO, copy_dest_exp_bias
at bits 16-21 by the layout in the SDK's `registers.h`) scales the resolved colour by 2^11 so the faint glow survives
16-bit fixed point; the fetch that reads it back has an exponent adjust of -11. Renut applied the fetch's adjust
(`sasrt_renut_texture_exp_adjust`, on: the "does not apply" in its log line is stale) but not the resolve's bias: the
glow came out 2048 times too dim. `blur_renut_resolve_exp_bias` (A/B 219): the bias applied in every colour resolve
path (a scaled copy draw; a multisampled source through a staging image; the sample-grid draw; the deko3d hand-over,
which cannot scale, left out when there is a bias; the skip signature includes it).

**Still open from the same comparison:** Renut gives a resolve destination the source surface's host format; the
console converts into the destination texture's own format (Blur's glow chain resolves 2_10_10_10 into k_8_8_8_8 and
half floats into k_16_16_16_16, fixed point with its clamp). The bias makes the values right in range; the 8-bit
quantisation and the fixed-point clamp are not reproduced yet.

**The same spot, both renderers (2026-11-11, 00:30-03:00).** Earlier race captures were at different places on the
track, so their inputs could not be compared. Both renderers now run the reference's own input timeline (START at 140 s,
A at 150-224 s; ours waits at the title) and RenderDoc captures both at 240-290 s (`grid_capture.ps1`, `rdc_trigger.py`):
the car is on the same grid spot in both. The reference draws its frame in three predicated tiles of a 1280x512 host
target; its draws are paired with ours by aligning the vertex counts of its middle tile with ours (537 of 850 matched).

**A correction to the test runs: the PC ran the build's defaults.** The PC app reads `renut.toml` beside `renut.exe`,
and there was none: every PC run so far had the build's defaults, enhancements included (`renut_post_process_precision`
"High", 8-bit post targets at 16 bits). `tools/make_blur_tomls.py` now also writes `app/out/pc/renut.toml`: the shipped
settings with the PC back end (D3D12; the shipped deko3d means Vulkan off the Switch) and no Switch shader-miss folder.
Not shipped. All comparisons from here on are of the picture players get.

**`blur_renut_edram_inherit` (A/B 220, on): one EDRAM, many views.** Blur draws its 256x128 environment panorama
(which the ground, car glass and car paint read) into a 512x256 4x target cleared to alpha 1, then draws the panorama's
RGB through a 320x256 single-sampled view of the same EDRAM (write mask RGB) and resolves that. On the console the view's
alpha is the clear's (and the panorama's, over its first 64 tiles: the reference's two black squares); here the view is
its own host target, whose alpha held old HUD pixels (mean 11 of 255 against the reference's 251). Before a draw that
keeps part of its target (a write mask without every channel of the format, or blending), the target now takes what other
views wrote over its EDRAM tiles since its own last write, oldest first, through the console's layout: tiles of 80x16
32-bit samples (40x16 for 64-bit formats), rows `columns` tiles across from the base, 4x pixels as 2x2 samples and 2x as
1x2 (`edram_inherit.hlsli`, four forms: single-sampled or multisampled on either side). Only views of the same EDRAM
format at the guest's scale; a source whose tiles a newer one covers is skipped; predicated-tiling targets (more than
2048 tiles) never. O(1) when nothing else was written. The environment texture's alpha is now 251.0, the reference's
exactly; the glow blur chain (320x180 -> 160x90 -> 80x45, blended) takes what the previous step left as on the console.
About 1.4 copies a frame on the PC.

**The occlusion queries were never answered (my bug in A/B 213).** A log of what `D3DQuery_GetData` returns to the game in
both renderers (`blur_renut_diag_query_log`, a custom entry for `sub_8224CD70` that runs under either) showed every
call under Renut "not ready", all race long, and every call in the reference ready with 1000 samples. The game's GetData
forms a block's address as `(p & 0x1FFFFFFF) + (((p >> 20) + 0x200) & 0x1000)` through the 0xC0000000 mapping: a pointer
in the 0xE0000000 range (Blur's blocks are at E6D8xxxx) is one 4 KB page further on, the console's offset of that range.
The hook wrote its answer at the pointer's own page; GetData read the sentinel 0xFFFFFEED at the real block and said not
ready. Fixed (`QueryBlockPhysical`): every query now answered as the reference's, 1000 samples. **The tail lights' red
glow now spreads sideways as in the reference**: the light glows were hidden as "not visible" while no query answered.

**Still open: the car's interior and paint.** The rear window shows the interior in the reference (pixel 82,87,75) and
almost black here (2,5,4); the paint is darker too. The interior's draw is the same shader in both (its constants agree:
Renut folds the read-only ones the reference reads from its buffer, 0.75, 10, 0.25, 30, 15), the same textures except one:
the 8x8 ambient cube the CPU fills every frame (two cubes, six faces each, locked and copied in through D3D's
LockSurface by the game's texture updater `sub_82332B98`, from a staging buffer). Ours has the same shape with other
scales: the light lobe about 0.46 of the reference's (its brightest texels 0x98 where the reference clamps at 0xFF), the
sky term in alpha about 3.5 times. The same CPU code runs in both renderers, so one of its inputs differs. Ruled out:
resolves read by the CPU (the reference without resolve readback gives the same cube), the luminance readback (0.027571
in both, a floor), occlusion answers (the cube is the same with them fixed or off), frame rate (30 fps capped: the same),
memexport (no readback in the race). Next: the writer of the staging buffer, found with a write watch on the guest page
(host page protection, the PDB naming the recompiled function).

**The black rear window, found (03:00-05:30).** Tracing the 8x8 ambient cube's difference led through the game's own code
with two new PC diagnostics (`app/src/blur_write_watch.cpp`): a write watch (a guest page write-protected, a vectored
handler naming the writing recompiled function and its generated line from the PDB, re-armed by single-stepping) and
wrappers put in the indirect-call table (`Memory::SetFunction`) around the cube filler `sub_8216AE98` and the light
writer `sub_8216CA28`, which log their inputs under either renderer. The cube is a sum of the scene's lights; light 0's
colour is base x min(2 x scale, 1), the scale the fourth float of the lighting object's +2672 vector, which the game
adapts every frame (`sub_8216FA88`): about 0.137 in the race intro, 0.43-0.44 on the grid, 0.12-0.3 once racing, in both
renderers alike. The earlier same-spot captures had caught the reference in its intro phase and ours on the grid: the
cube's difference was the phase, not a renderer fault.

The window itself: at the same pixel the car interior draws alike in both (0.05), but the glass over it outputs RGB 0.03
here against 0.27 in the reference (alpha the same). The glass and the paint reflect the 256x256 environment cube the
game renders every frame, 4x, and resolves face by face into slices 0-5. Here five faces were black: plume's region
resolve has no slice argument, so every multisampled resolve landed in slice 0. `blur_renut_ms_slice_resolve` (A/B 221,
on): a multisampled resolve into a slice or depth other than 0 goes through the single-sampled staging image and a copy
into its slice (the path Vulkan already used for destinations with levels). The six faces are now the reference's within
a few levels; the rear window shows the interior (67/70/58 against 87/89/77; the rest is the lighting phase and the
car's place), the paint is brighter.

**Machine copies: on hold (the user, 21:12: "I'll tell you when", waiting on the SASRT session).** The SASRT
session's generator patch (copies of functions with guest atomics) is applied to `tools/localize_native.py`; nothing
runs it until post-codegen runs again.

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
| `FastCensusDraw` / `FastCensusReport` share a mutex | with a queued backend (deko3d) the census map was replaced by the report thread under the render thread's insert: Ryujinx crashed with a pointer made of text | emu2, emu3 here; `render/draw.cpp` `g_fast_census_mutex` |
| Occlusion queries answered as the emulated GPU answers them (`D3DQuery_GetData` hook), each block at the address GetData forms (`QueryBlockPhysical`: the 0xE0000000 range's 4 KB offset) | Renut runs no GPU packets, so a query's ZPASS counts never come and GetData says "not ready" for ever: whatever a game hides by a query is never drawn (Blur's light glows); an answer at the pointer's own page is never read | `d3d/hooks/state.cpp` here; the SDK's `ExecutePacketType3_EVENT_WRITE_ZPD`; `blur_renut_diag_query_log` |
| Multisampled resolves into a slice other than 0 through a staging image (`blur_renut_ms_slice_resolve`) | plume's region resolve has no slice: every cube face / array layer resolved from a multisampled target landed in slice 0 | Blur's environment cube (five black faces, the black rear window); `render/draw.cpp` |
| Views of one EDRAM take each other's writes (`blur_renut_edram_inherit`) | each view is its own host target; a draw that keeps part of its target (a write mask, blending) showed what that host target last held, not the EDRAM | Blur's environment panorama alpha (11 -> 251 of 255, the reference's), its glow chain; `edram_inherit.hlsli` |
| A partial resolve keeps a destination of another format (`blur_renut_resolve_keep_dest`) | the console converts into the destination's format; Renut remade the destination (empty) | Blur's 640x360 image here |
| Multisampled region resolves on deko3d through a whole-target staging image (`blur_renut_tile_resolve_staging`) | plume's deko3d region resolve blits the multisampled image; in Ryujinx only the first tile of a tiled frame arrives | emu4 here |
| The base's notes: a regeneration deletes `codegen.stamp` as well as `codegen.build.stamp` | with only the build stamp gone, rexglue sees its input fingerprint unchanged and writes nothing; post-codegen runs again over the files as they are | build 39 here |
