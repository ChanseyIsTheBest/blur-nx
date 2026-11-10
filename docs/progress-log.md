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
