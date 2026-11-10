# blur-nx progress log (2026-11-10, AEST)

| Time | What |
|---|---|
| 13:39 | Start. D: 75.8 GB free, C: 53.2 GB. Read the base's README, building.md, ultimate-fixes.md, codegen-crash-fixes.md, optimising-functions.md, katamari-notes.md (layout sections), banjo-nx's porting-start-here and renderer guide, rr6-nx's notes. |
| 13:41 | Disc extracted to `D:\blur\game` (124 files, 5.4 GB): title 41560845, media 01C784D3, v0.0.0.1. Title update 2 unpacked to `D:\blur\tu` (v0.0.2.1 from 0.0.0.1); its `default.xexp` beside `default.xex`. |
| 13:48 | blur-nx made from the base's github-main (git archive), the untracked pieces copied (FFmpeg config, dxc-bin, rexglue.exe, glslang, deko3d tools); first commit. |
| 13:53 | Codegen 1: two tail-call targets (one in BINK). XDK read from the XEX header: 2.0.11164 everywhere. |
| 14:00 | Codegen 2 with setjmp / longjmp and the two thunks: 379 files, one split leaf (0x82479C48) to declare. |
| 14:05-14:25 | Renut moved to XDK 11164: guwange-nx's names matched in Blur (77 of 84 exact), the state tables (121 of 121), 24 hooks by hand from call chains; 318 hooks placed, 5 not linked. Device zones measured on Blur's pairs, 152 offset lines rewritten (pacman-nx's tool for this XDK); 11 guest data lines. Katamari's game layer stripped (rr6-nx's list). |
| 14:25 | The Viva Piñata session relayed the base's ports 63 and 64: applied. |
| 14:30 | First PC build (Renut on) started. |
| 14:35-15:09 | Three build stops, each fixed: the shader dump rule's script is not in the tree (made `out/renut_shader_dump` first with `tools/renut_shader_dump.py`: 93 vertex, 426 pixel containers in default.xex); XDK 11164's snapshot (42 read chunks, a 24-byte last chunk: worldkeflings-nx's 16 + 8 form); `localize_native.py` wrote into a `generated/hot` that did not exist yet. Jump tables: `tools/switch_check.py` 0 truncated, 0 wrong, 0 missed; its 4 odd shapes are calls through function pointers in data (all 26 targets of the one table registered). |
| 15:10 | First runs. Renut (`pc1`): D3D12 chosen by default, the log stops after the swapchain (a hang). Emulated GPU (`emu_ref1`): needs `--gpu_plugin=xenos`; the game stops 0.6 s in on "Call to invalid or unregistered function at guest address 0x8233BD60" (`li r3,1; blr`, a vtable method: the pointer sweep's first pass accepts it). |
| 15:15 | Pointer sweep to its fixpoint started. Katamari's DLC pack list taken out of the installer. |
| 15:25 | Pointer sweep: 46 functions, fixpoint in pass 2. |
| 15:37 | Gap sweep: 236 functions, fixpoint in pass 5. Six labels taken back out (`override_labels_check.py`). |
| 15:40-15:55 | Register diet: non-argument audit (100 + 45 functions kept), diet codegen, read-before-write (13 reports, all accounted for; one pointer-sweep pick was a jump table's base: out). Jump tables rechecked: clean. PC build 4 started. |
| 16:07 | Reference (emulated GPU, `--gpu_plugin=xenos`): boots through the logos and intro to the "Press START" title by 135 s. The CPU side, kernel, files and the title update work. |
| 16:08-16:13 | Renut: the "hang" after the swapchain was the PC path dialog waiting for a click (no saved paths): skipped when the game folder given holds default.xex. |
| 16:14-17:00 | Renut at 11164, run by run: draws dropped on the device's own declaration (+0x3050 here); D3DVertexShader_Bind (11164 form, `sub_82251090`) ran on Renut's declaration record: hooked; Lock2DSurface / LockSurface / LockResource have 11164's extra unused argument (pBits 0x00100000): fixed; SetGammaRamp / SetPWLGamma take a flags argument; GetDeclaration (`sub_82244C50`) read Renut's record as D3D's (a host crash at 20 s): hooked; XGRegisterPixelShader and SetVertex/PixelShaderConstantI hooked (pacman-nx's precedent). A PC stack dumper written (`tools/pc_thread_stacks.py`). |
