# Hand natives, batch 3a (draft, 2026-11-11)

Asked: eight natives from sub_82500AC0, sub_822E6E98, sub_825217B8 (callers of the sine or of sub_82520D78) and the leaves
sub_822E29A8, sub_822D7DF0, sub_822E8418, sub_82522568, sub_826A7858, one at a time. **One is written; seven are not.** I read
each in turn and stopped where exactness could not be proven in the time; each reason is a `#` line in
`hand_batch3a.draft.txt` and repeated below. Files: `app/src/cpu/hot/blur_hand_natives_batch3a.draft.h` (key to add:
`kN82500AC0`; helpers in `blur_b3a`), the `.txt`, this file. Nothing was built or run.

## Written

### sub_82500AC0 (whole function; caller of sub_82954478; exact=r1)

A rotation matrix from an axis and an angle (Rodrigues): `r3` = three row vectors (read for the lane that is kept, written
back), `r4` = the axis, `v1` = the angle in element 0. It saves v126/v127 (at `r1-64/-48`), builds a frame of 160, takes
sin(angle) and sin(constant - angle) (the constant is the vector at `0x82027BF0`, so the pair is sin and cos) with two
calls of sub_82954478, rounds each to single, and in three rounds (one column per round) forms the entries with
`vmulfp128`/`vmaddfp128` chains and `vrlimi128` lane insertions into the three rows, storing the rows after each round;
then restores v126/v127.

What the native removes against the machine copy: the two sine calls and their hand-overs (the sine's arithmetic is inline,
constants loaded once; the same code as the jq012 natives' `SinEval`), the `stvewx128` + `lfs` round trips (the word is
used from the register), the intermediate stores of the three rows (nothing reads them between rounds; the axis block is
read three times by the original, and it is the same value because the overlap checks exclude any write to it), the
context round trips of every vector temporary, and three of the four flush-mode switches. As in the native of
sub_82521E68, the sines run in whatever mode the host is in: both inputs are widened singles (`SingleToDoubleAnyMode` of
the word in lane 3 of v1, `SingleToDoublePure` of lane 3 of `constant - v1`), so no operation of the sine meets or makes a
denormal double and flush on and off agree. The mode is switched on once before `vsubfp128` and is on at the end, as in
the original. The vector inputs of the first float operation are `Pin`ned after the switch, and the axis and row loads
come after the `Fence`.

Stores made (all `Note`d first; one `Card` for the 192 bytes under `r1`, one for the rows): the second sine's red zone at
`r1-192` (|x| bits, the reduction, and unless `REX_NV_SAVES_ELIDE` two zero words), the back chain at `r1-160` (unless
`REX_BACK_CHAIN_ELIDE`), the words at `r1-80` (lane 3 of the subtraction) and `r1-76` (the second sine rounded), v126 and
v127 as they came in at `r1-64`/`-48`, the dead saves at `r1-24`, `-16` (zero: the copy's locals r30, r31) and `r1-8` (the
low word of LR; unless `REX_LR_MOVES_ELIDE`), and the three rows at the end (lane 0 keeps its memory value: the three
rounds replace lanes 3, 2 and 1 only). `ctx.lr` is truncated to its low word as the original leaves it, unless elided.
v126/v127 are not written back to the context: the original restores them from the slots it wrote, so they end as they began.

Declines (nothing written): `r1` not 16-aligned; frame, rows, axis block or constant block wrapping or crossing the
physical-view boundary; the rows overlapping the axis block, the frame or the constant block; the axis or constant block
overlapping the frame.

Doubts to check first:
1. The flush-mode argument (same as sub_82521E68's; restore the switches around each `SinEval` if it is rejected).
2. The lane choice of `vspltw128 ...,1` and `,2` is host shuffle `0xAA` and `0x55`, taken from the generated statements.
3. The order of the multiplies in the three rounds was copied statement by statement; each `vmaddcfp128` is the generated
   `REX_VMADDFP(a, c, b)`.
4. exact=r1: r5-r12, f0, f1, f13, v0, v7-v13 are scratch the original leaves and the native does not.
5. The profile evidence is indirect: the function is not among the 80 hottest by name; it is a caller of the sine
   (sub_82954478, 2.3 % of a core).

## Not written

- **sub_822E6E98**: also calls sub_82954558 (the cosine, a leaf I could inline) and sub_829552D8 (an arctangent-like
  routine with five callees of its own). The setup path therefore has real callees. After the setup comes a loop of 64
  blocks of four floats per record (a clamped matrix update with loop-carried scalars f5, f7, f8, f9), reachable without
  the callees when three field pairs of the object are equal. A hot-path native on that loop is possible; I did not write it.
- **sub_825217B8**: caller of sub_82520D78, but both loops index a shared array through index tables stored in the 28-byte
  records themselves, so the loads and stores alias in data-dependent ways; the call of sub_82520D78 sits inside the second
  loop's conditional. No provable hoist.
- **sub_822E29A8**: leaf filter loop with a 16-way `vsel` tail keyed on `r6 & 15`; the copy already keeps it in registers.
- **sub_822D7DF0**: int16 to float scale; the vector body's `vupkhsb128`/`vcsxwfp128`/`vperm128` lane semantics unverified.
- **sub_822E8418**: 176-instruction vector body with reciprocal estimates; nothing to remove.
- **sub_82522568**: straight-line, but it writes object fields and reads them back (offsets 148-156, 700, 708) and uses a
  record-form compare; not provable in the time.
- **sub_826A7858**: pointer chasing over an index table with data-dependent exits (cache misses).
