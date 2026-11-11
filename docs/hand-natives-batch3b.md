# Hand natives, batch 3b (draft)

Console race profile of 2026-11-11 15:18 (rebased build 46b3f6d6), 14 candidate functions read with that build's ELF
(`tools/switch_hot_pcs.py`, thread "JQ Worker"). Four natives are written (`app/src/cpu/hot/blur_hand_natives_batch3b.draft.h`,
lines in `hand_batch3b.draft.txt`), ten functions are declined with a reason each. Nothing was built or run: every doubt
below is something the first PC guard run (`banjo_cpu_verify`, every call) has to settle.

Keys to add: `kN8230A4D8 kN822D92D8 kN822D9040 kN822D79A8`.

## Written

### sub_8230A4D8 (1.42 % of a core) - whole function

Walks `n` (a halfword at obj+22) 1 KB blocks. Per block, with the flag byte obj+31 set, it calls sub_822EA3C0 (copy 1 KB
src -> dst) and then zeroes the source block with eight `dcbzl`; with the flag clear it zeroes the destination block with
eight `dcbzl`. At the end obj+31 = 0 and obj+8 = r4[0]; obj+4 == -1 skips everything but obj+8. Samples: 5 of 25 on the
first field load (cache miss, not removable), the rest spread over the zero stores, each followed by a card mark, and the
inlined callee. The native sees that the blocks are consecutive: one copy of 1024 n bytes, the 16-byte "lead" pieces the
callee's `dcbz` leaves when bit 4 of dst is set (the last 16 bytes of every block but the last, and the 16 below the first),
one zero of the source (or of the destination), the cards once per range. Registers written: r3-r10 (values by path in the
header comment), the stack word `stwu` leaves at r1-144.
Doubts to check first: (1) the back-chain word at r1-144 is written because the build has `REX_CHAIN_STORE` on
(`BANJO_GEN_BACK_CHAIN_ELIDE` is OFF by default); the savegprlr stubs emit nothing in the generated function, so no other
stack word is written. (2) The claim that the callee's zeroing leaves only the lead piece is taken from n_822EA3C0's comment
and the generated code (pieces at `(dst + 128 k) & ~31`); the guard will show it. (3) Everything overlapping is declined, so
the common case in the profile (a buffer moved to a different buffer) must not overlap; if the "compared" count is low,
look at the overlap tests first (the object fields obj..obj+40 are included).

### sub_822D92D8 (1.42 % of a core) - whole function

int16 -> float x 2^-15 for n <= 1024 (`out[k] = float(in[k]) * 2^-15`), as five stretches (head until out is 16-byte
aligned, blocks of 32, blocks of 8, one group of 4, single elements). The scalar stretches go through the stack (`std` then
`lfd`, failed store forwarding), the vector ones through `lvlx128 + lvrx128` pairs. Samples are flat (no bucket above 9 %).
Every result is exact (int16 times a power of two), so the native converts everything in one pass (8 elements a pass from
one unaligned 16-byte load, a 16-bit swap, `cvtepi16_epi32`, `cvtepi32_ps`, one multiply, a 32-bit swap) with one card range,
and *computes* what the stretches leave: r3-r10, f1-f13, the doublewords at (r1-192)+80/88/96/104 and the flush mode. That
part is where it can be wrong: it follows the generated statements stretch by stretch, so the first guard failure will name
the register.
Doubts to check first: (1) `r9` and `r10` at the end (pointer registers updated by `lhzu`/`stfsu` keep their upper 32 bits;
`SetLo`); (2) the counts of the stretches (the native refuses, `covered != n`, if they do not add up; check that this is
not firing); (3) f10 after the group of 4 is the raw doubleword (`lfd` of the stack), not a converted value; (4) the
flush mode after n == 0 is left alone; (5) outputs that are not 4-byte aligned are declined (the original rounds `stvx`
down).

### sub_822D9040 (0.80 % of a core) - whole function (f1 != 1.0)

32 passes of 8 positions: `p(m) = m * s`, entry = table + ((p >> 6) & 0x3FFFFFC), `lo` and `hi` (the next float),
`out = lo + (hi - lo) * frac`, two `stvx128` a pass. Per pass 16 `lvlx128` table loads merged by `vmrghw128`; the generated
merge keeps only lane 0 of each load (derived from the unpack lane indices: lo vectors are [lo3 lo2 lo1 lo0] and [lo7..lo4]
in host lane order, hi likewise). The setup stores `s` and `s << 3` to the stack and loads them straight back with
`lvlx128` (no forwarding). The native loads the 16 words directly into four vectors, keeps the setup in registers (the two
stack words and the back-chain are still stored), keeps the same `vrfiz`/`vsubfp`/`REX_VMADDFP`, marks the cards once.
Registers: r3-r10, v0, v11, v12, v13 (the last pass), flush mode on.
Doubts: (1) the lane mapping of the gathers (the one thing in it that is derived rather than copied); (2) `LvlxW` returns
a word cut short when the address offset is above 12 (matches `VectorMaskL`, not verified for that case); (3) the constant
K at the guest address 0x82B9D114 is read from guest memory each call, as the original does; (4) the table may overlap the
output: every pass loads before it stores, as the original, so it should be exact, but it is the first thing a guard
difference would be about.

### sub_822D79A8 (0.74 % of a core) - whole function (f1 != 1.0)

The same interpolation on two tables with a chained offset: per pass four lookups at the byte offsets S0..S3 (the phase
`r9` grows by the step, the low byte carries over, each step adds `(phase >> 6) & 0x3FFFFFC`), A and B words at S and S+4,
`vrlimi128` blends keeping lane 0 of each (vectors [w(S3) w(S2) w(S1) w(S0)]), out1 from table A and out2 from table B.
Same treatment as sub_822D9040. Registers: r3, r5, r7, r8, r9, r10 (the last pass), v0, v10, v11, v12, v13.
Doubts: (1) the order of the chain (P1..P4 and S0..S4) was traced statement by statement from the generated code
(steps 1-54 of the loop); a slip there changes every table address, so the guard will show it at once; (2) r5 = S1 + 4 and
r3 = idx(P2), r7 = idx(P3), r8 = idx(P4) as the last pass leaves them; (3) the stack word is the step at (r1-112)+140.

## Not written

- **sub_8230AAD0 (1.79 %)**: recursive and a switch on the entry type calling four other functions (real callees on every
  path), with a `stwux` frame of variable size. Samples are on the dispatch and the entry-field loads.
- **sub_82236E78 (1.61 %)**: the XDK's memcpy-like routine (head, 128-byte block loop, vperm loop through
  sub_82239350, 16-byte loop, tail, small copies through sub_82954218). 11 of 25 samples are the source loads of the block
  loop (cache misses). Each path leaves its own r4-r10, v0 and 96(r1) residue; a hot-path native would only save card marks.
- **sub_822E4FE8 (1.42 %)**: scalar prologue with a call (sub_82954CE0) on one path, then a vector kernel with vsldoi windows
  and sixteen vmsum4fp128 dot products already in registers in the machine copy; the operation order has to stay.
- **sub_822BEDD0 (1.42 %)**: a jump-table dispatcher over 26 entries calling 14 functions.
- **sub_8230EDE0 (0.99 %)**, **sub_822D2378 (0.99 %)**: 16 and 12 direct callees.
- **sub_822E1468 (0.74 %)**: a callee on one path; on the other 7 of 13 samples are the loads after the first store
  (object cache misses).
- **sub_829552D8 (0.68 %)**: a libm routine with 7 callees.
- **sub_82302700 (0.68 %)**: sampled path is field loads; the other paths call virtual routines through `bctrl`.
- **sub_826ADB30 (0.62 %)**: closest point on a triangle; 80 vector statements in registers, nothing to remove.

Only four of the requested eight natives were written: the other ten either have real callees on the sampled paths or no cost
beyond cache misses that a rewrite removes.
