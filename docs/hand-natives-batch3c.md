# Hand natives, batch 3c

Console race profile 2026-11-11 15:18 (rebased build 46b3f6d6), report 16. Code: `app/src/cpu/hot/blur_hand_natives_batch3c.draft.h`,
lines for `hand.txt`: `app/src/cpu/hot/hand_batch3c.draft.txt`. Every function below was read from the generated code and its
samples (`tools/switch_hot_pcs.py`) one at a time. The profile of every function in this batch is flat (the largest
single bucket is 5 to 30 samples of a few dozen), so the gain to expect from each is small: the doubts say what to
check first.

## sub_82432918 (hot path)

Checks whether the object's slot `h` (h = halfword at key + 2) already holds the key (three words) and whether the
next entry's first word equals the key's word at +12; if so returns 1, otherwise stores into the slot and calls
`sub_8232CC40` / `sub_82432300`. 8 samples over the whole function (0.56 % of a core). Decision: native for the two
paths that return 1 without a call (h == -1 and "slot equal, next equal"); the rest declines before writing. It
removes the r8-r10 write-backs and the flag chain only, so the gain is small. Only memory written: the back chain at
r1 - 112 (unless `REX_BACK_CHAIN_ELIDE`). Doubts: the native stores that word last and so declines if a read overlaps it;
check the hit rate of the declined (call) path, which may be most of the calls. If the equal path is rare it does not pay.

## sub_8251E438 (whole, a leaf)

Pushes a 64-byte record and two list entries into an object (see the comment in the code). 16 samples, spread over the
card marks after each of the 16 stores and the loads behind them. Decision: whole-function native that keeps every
access in the original's order (the lists' lengths are read, used, reread and incremented; the stores can alias them),
copies the vector as 16 bytes without the two byte swaps, loads no register. Doubts: it saves little beyond the
swaps, the register write-backs and the card calls batch; verify with the guard that the vector copy (lvx/stvx at
different alignments) and the `blelr` path (negative word) are both reached, and that the result of
`REX_RO_CFG_U32` is what the build's mode returns (the native uses the same macro at the same addresses).

## sub_82526568 (whole, four callees inlined)

Builds two output vectors (r4 always, r5 a vector or zero) from a view/camera object: two reciprocals and a reciprocal
square root by Newton steps, `sub_82521DC0` when the first scale is not positive, a smoothstep `sub_82521228`, and
(when the word at obj + 880 is below 2) a second one `sub_82521148`. 27 samples at the race (0.8 % of a core), spread
over the whole function (largest buckets 11 %). The machine copy inlines the four callees but, as its header says,
stores and reloads 8 to 20 registers through the context around each one, and the function goes through the stack ten
times with a 4-byte store followed by a 16-byte `lvlx128` of the same line (failed forwarding on the A57). Decision:
a whole-function native: the callees are written inline (they are leaves apart from `sub_82520E70`, a leaf itself), every
register is a local, every guest store is made where and in the order the original makes it (cards as the original
marks them), every read of the object and tables is made from guest memory where the original makes it, and only the
16-byte reads of just-stored stack words come from the registers. Every flush-mode switch of the original is kept in
place with the arithmetic around it pinned (Fence / Pin), because some switches are `Unconditional` and the scalar sections
run with the mode off, the vector ones with it on.
Doubts to check first, in this order: (1) the flush-mode handling is the likeliest source of a guard difference: `REX_FLUSH_KNOWN_ON/OFF` are
no-ops on the Switch and checks on the PC, the native calls them where the original does; (2) `ctx.lr`: on the path
through `sub_82521DC0` the original reloads the link register from its save slot (low word, unless
`REX_LR_MOVES_ELIDE`); the native stores that word at r1 - 248 and writes `ctx.lr` the same way; the guard must not
skip lr; (3) the stack words written below the frame (r1 - 240 - 8 / - 16 / - 32 / - 48 / - 128) exist only on the path
through the two callees that use them; (4) `stvewx128` stores lane 3 of an all-equal vector: the native takes lane 0, so
check that no `NaN` payload differs between lanes (all lanes are the same operation on the same value); (5) `s.Note` is
called before each store, so a logged run that ends in the middle leaves the stores made so far: the others do the same.
Declines: r1 not 16-aligned, frame or either output vector wrapping or crossing the physical view, an output vector
overlapping the frame.

## Not written (one paragraph each; the reasons are in `hand_batch3c.draft.txt`)

- `sub_82505EF8` (566 instructions): its one direct callee is `sub_8252E168` (336 instructions, nine callees) called from
  inside the loop. A native cannot call it; no cost visible in the 32 samples (largest bucket 9 %).
- `sub_826803B0` (704): five callees and a save/restore of five vector bundles; flat (35 samples, largest 8.6 %).
- `sub_826EFE18` and `sub_8266D118`: two copies of the same bounding-volume traversal. The samples are on the vector box
  test and the object-field loads of the inner loop; the leaf case calls `sub_826EFA20` / `sub_82669E58` after stores to
  the stack and the lists. A hot-path native for "no leaf is reached" would have to compute the whole traversal without
  writing and is declined most of the time. Nothing written.
- `sub_8252E168`, `sub_826EFA20`, `sub_824EB1C8`, `sub_8250C1A8`, `sub_8248DBB0`: 8 to 26 callees each, flat or atomics.
- `sub_82432300` (recursive interpreter), `sub_82432A28` (six calls; its call-free exits are a handful of loads),
  `sub_82507988`, `sub_82521FA8` (both end in calls after a short vector set-up), `sub_8227E098` (lwarx/stwcx and ldarx
  loops): calls or interlocked operations.
- `sub_82668520` is the one worth a next look: its first exit (a distance test passes) calls only the leaf
  `sub_826ADB30` (85 instructions) and returns after 4 stores, with `__savevmx_119` / `__restvmx_119` (9 vector registers
  stored to the red zone and reloaded) around it. A native for that exit only (inline the leaf, write the 9 save slots,
  decline the other two exits that call `sub_826AE058`) is feasible; 16 samples, flat, so it was left.
