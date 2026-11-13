# The 30 codegen ideas rechecked for exactness, and where we are more precise than the 360 (2026-11-14)

The user (01:58): "Double check to see if they are actually exact and you are being too conservative and give me that
new table + 20 more where we being too precise vs 360". The 30 are #130-#159 (docs/cpu-codegen-inexact-30-1114.md).

## Part 1: the 30 rechecked

Most of the 30 were called inexact because they assume something about the code. But many of those assumptions are
**provable for Blur's binary**: either the PowerPC calling convention guarantees them for compiled code, or a scan of
Blur's code can show that no function breaks them. A proof makes the idea exact for Blur. Recheck result: **16 exact**,
**1 timing only**, **2 closer to the real 360 than today's build**, **11 really inexact**.

| # | Idea (short) | Verdict | Why |
|---|---|---|---|
| 130 | lwsync as release / acquire forms | **Not exact** (corrected) | stlr / ldar order only that one access; lwsync also orders the later ones (`stw A; lwsync; stw B; stw C` keeps A before C). The earlier doc's "exact half" was wrong. The exact cheaper mapping is #165 |
| 131 | XER[CA] dead at calls and returns | **Exact** (scan) | the ABI makes XER volatile: compiled code never reads a carry across a call. A scan for a carry read before any carry write at entry or after a call proves it for Blur |
| 132 | single ops in single without CG-14's proof | Not exact | a true double input (`float f = d1 + d2` compiles to fadds on doubles) rounds twice |
| 133 | double chains ending in frsp in single | Not exact | one op on single-representable inputs is exact (double rounding is harmless for +, -, *, /, sqrt: 53 >= 2x24+2), but that case is CG-14's; a double constant like 0.1 or a chain of 2+ ops differs |
| 134 | stack, globals, heap as separate restrict arrays | **Exact** (escape analysis) | a function's own frame slots whose address is never formed (no `addi rX, r1, N`, no `mr rX, r1`) cannot be reached through another register; the outgoing-argument area counts as escaped |
| 135 | fast-math subset | Not exact | by definition |
| 136 | -freciprocal-math | Not exact | by definition (exact only for powers of two) |
| 137 | loop lifting + auto-vectorization | **Exact** (element-wise loops) | with a runtime overlap check (loop versioning) element-wise loops vectorize exactly; reductions (sums) need #135 |
| 138 | no 32-bit wrap on base + displacement | **Exact** (d-form) | rA + d crosses 0 or 4 GB only for addresses the 360 itself would fault on. Not for x-form (rA + rB): a "negative" rB must wrap |
| 139 | 32-bit registers where no 64-bit op runs | **Exact** (interprocedural register dataflow) | provable, but most 32-bit ops cost the same on the A57: small gain |
| 140 | typed memory (float vs integer aliasing) | Not exact | float bits moved through memory (stfs + lwz: PowerPC has no FPR-to-GPR move) are common |
| 141 | read-mostly globals as constants | Not exact | assumes the future |
| 142 | fast approximate CRT math | Not exact | by definition |
| 143 | if-conversion including loads | **Exact** (results) | a load the original would skip only changes results if it faults; allowing only bases already dereferenced in a dominating block (and no MMIO) means it cannot fault newly |
| 144 | fres / frsqrte as estimate + one Newton step | **Closer to the 360 than today** | the 360 returns a low-precision estimate; today's build computes the exact value with a double divide (and sqrt). Neither is bit-exact to the 360; the estimate is cheaper and nearer its precision |
| 145 | back-chain stores dropped | **Exact** (scan) | only code reading 0(r1) or taking r1 as a value can see them; a scan finds those functions |
| 146 | only ABI argument registers at calls | **Exact** (ABI + millicode scan) | compiled code never reads a caller's non-argument volatile registers; the millicode helpers (r11 / r12) are the scan's exceptions |
| 147 | spin loops with a host back-off | **Timing only** | the guest computes the same |
| 148 | non-wrapping induction variables | **Exact** (counted loops) | an IV bounded by the loop's own exit test (`i < n`) cannot wrap; pointer IVs cannot for valid addresses |
| 149 | profile-pruned copies with bail-out | **Exact** (by construction) | the bail-out hands the state to the full translation; the risk is in the implementation, not the idea |
| 150 | card marks once per page per function | **Exact** (race-free code) | if pending marks flush at every barrier, atomic, call and kernel call, a consumer that synchronizes (as correct 360 code must, on a weakly ordered CPU) always sees them |
| 151 | lvx alignment masks dropped | Not exact | the lvsl + two lvx unaligned-load idiom relies on the masking |
| 152 | dot products as fused chains | Not exact (vs today) | different last bits; the 360's own rounding of vmsum is not known |
| 153 | mfcr / mtcrf narrowed to cr2-cr4 | **Exact** (ABI + scan) | the volatile fields of a saved CR word are never inspected by compiled code |
| 154 | lwarx / stwcx. as LDXR / STXR | **Closer to the 360 than today** | the 360's reservation is address-based like LDXR / STXR; today's compare-and-swap emulation is value-based (ABA passes). Risk: forward progress if a load between them clears the monitor |
| 155 | vtable loads hoisted across calls | Not exact | constructors and destructors change the vptr |
| 156 | indirect calls without range / null checks | **Exact** (valid runs) | only a run that is already crashing changes (a host crash instead of a report) |
| 157 | leaf red-zone slots as host locals | **Exact** (ABI + scan) | a leaf calls nothing; only an address formed from r1 inside the leaf could reach the slots |
| 158 | getter call CSE | **Exact** (the data-race-free rule) | the rule GCC already applies to plain loads: never merged across loops, barriers, atomics or kernel calls |
| 159 | non-escaped stack slots survive calls | **Exact** (escape analysis) | callees write only their own frames below the caller's r1; with the outgoing-argument area counted as escaped, no callee can reach the slots |

So the "exact by proof" ones join the safe list. The cheapest to try: **#131, #145, #146, #153, #156, #157** (each a
builder change and a scan); the biggest: **#134 + #159, #138, #149, #137, #150**.

## Part 2: where we are more precise (or more careful) than the 360 itself

What the 360 hardware or kernel did cheaply, which today's translation or SDK does in full. Matching the 360 is both
cheaper and no less faithful. Found by reading the builders and the SDK, and the PC census of kernel calls
(census_cg13: ~1.4 M kernel calls a second, 891 k KeTlsGetValue, 12 k each of NtAllocate / NtFreeVirtualMemory in its
window).

| # | Where | Today | The 360 | Instead | Est. | Exact? |
|---|---|---|---|---|---|---|
| 160 | vrefp (vector reciprocal estimate) | an exact vector divide | a low-precision estimate | frecpe + one Newton step (still more precise than the 360) | 0.3-1 ms | as close to the 360 as today |
| 161 | vrsqrtefp | a vector sqrt and a divide | an estimate | frsqrte + one Newton step | 0.3-1 ms | as close as today |
| 162 | vlogefp | log2f called 4 times (libm) | an estimate | a NEON polynomial like today's vexptefp | small (count first) | as close as today |
| 163 | lwarx / stwcx. | a sequentially consistent compare-and-swap (acquire + release); isync emitted as nothing | no ordering of their own: the game's lwsync / isync / sync order them | relaxed exclusives, with isync (after the stwcx. branch) as an acquire fence | 0.5-2 ms (~320 k CAS a second in a PC race) | exact vs the 360 |
| 164 | eieio | a full fence (`dmb ish`) | orders stores (cacheable memory); MMIO is already handled by the translator's MMIO path | `dmb ishst` | small | exact vs the 360 |
| 165 | lwsync | a full fence (`dmb ish`: also orders store-then-load, which lwsync does not) | load-load, load-store, store-store | `dmb ishld` + `dmb ishst` (measure first: two half barriers may not beat one full on the A57) | 0-1 ms | exact vs the 360 |
| 166 | SIMDE (the vector translation is written in x86 SSE intrinsics) | x86's own NaN, conversion-range and rounding rules reproduced on ARM (no SIMDE_FAST_* flags set) | VMX's rules, which on most ops equal ARM's native ones (e.g. saturating float-to-int = `fcvtzs`) | SIMDE_FAST_NANS / _CONVERSION_RANGE / _ROUND_TIES per op where native ARM = VMX | 0.5-2 % of vector code | exact vs the 360 where ARM = VMX (op by op) |
| 167 | dot products (SIMDE's `_mm_dp_ps`: 848 sites in 10 of the generated files) | x86's dpps masking and summation order emulated | VMX128's own order (not x86's) | a plain NEON multiply and pairwise adds | 0.5-1.5 % of vector code | as close to the 360 as today |
| 168 | callee-saved register save areas (the millicode's GPR / FPR / VMX saves) | byte-swapped into guest memory and back | the 360's own byte order, but nothing except the matching restore reads them | kept in host byte order (no swaps on save or restore) | 0.5-1.5 % | exact (ABI + scan) |
| 169 | vpkd3d128 / vupkd3d128 (D3D packing of vertex data for the GPU) | the 360's exact packing emulated | hardware pack | native conversions (+-1 ulp on data only the GPU reads) | count uses first | not bit-exact (GPU data only) |
| 170 | XMA decoding | FFmpeg's float decoder | a fixed-point hardware decoder; the game gets 16-bit PCM | a cheaper transform within 16-bit output (FFmpeg is not bit-exact to the 360 either) | small now (the frame cache answers most race frames) | as close as today |
| 171 | APC delivery (every alertable wait, twice) | the APC spinlock taken even when no APC is queued | a flag test | an atomic "APC pending" test first | 0.1-0.5 ms | exact |
| 172 | KeTlsGetValue (~100 k a second) and KeQueryPerformanceFrequency | an import call, a thread lookup, a bounds check | a few instructions reading the PCR (a constant for the frequency) | inline reads in the copies (the frequency a constant) | 0.1-0.4 ms | exact |
| 173 | NtAllocateVirtualMemory / NtFreeVirtualMemory (~1,300 a second each in the census window) | host mapping changes each time (each guest chunk is mapped several times: the views) | page-table edits | decommitted pages stay mapped and are zeroed on the next commit (only code reading freed memory could tell) | 0.5-3 ms if the rate holds in races (census first) | exact for code that does not read freed memory |

### Checked and dropped (already done, or too rare to matter)

- Rumble: XInputSetState already sends vibration only when it changes (one IPC per change, not per call).
- Audio output: already two frames per wake-up (M-43, audio_switch_pump_pairs).
- Sleeps: KeDelayExecutionThread sleeps without spinning for accuracy.
- Cache instructions: dcbf / dcbst are already no-ops; dcbz is a 32-byte memset.
- vexptefp: already a ~12-bit polynomial (the 360's precision).
- vmaxfp / vminfp: the exact helper is needed (#42's census: 4.1 % of lanes differ with Arm's fmax / fmin).
- The CAS's trailing `dmb ish`: already off in Blur (banjo_cpu_guest_cas_no_barrier).
- Input polling (two locks per XamInputGetState) and thread priority changes: under ~260 calls a second.
- MmQueryAddressProtect (~2 k a second): already a cheap heap query.

### A fidelity note (not a speed idea)

`stfs` of a double that is not a single: today `float(x)` (rounded). The PowerPC store-single conversion takes the
bits as they are (no rounding) when the value is not a single, so a store of a true double can differ from the 360 in
the last bit. Matching the 360 would cost a few instructions, not save any; it only matters for doubles stored as
floats without an frsp, which the PC census could count.
