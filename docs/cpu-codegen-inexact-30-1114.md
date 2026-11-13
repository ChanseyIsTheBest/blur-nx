# 30 more codegen ideas, none exact (#130-#159, 2026-11-14)

The user (01:45): "What about 30 more codegen not exact ideas give me another table". All are in the PPC -> C++
translation (sdk/src/codegen/builders, the machine copies' tools, the copies' compile flags), and each one assumes
something the translator cannot prove about Blur's code, in exchange for cheaper host code. None repeats CG-01..40,
VX-01..40, the near-exact 50 and refills (#1-#69), #70-#99 or #100-#129 (docs/cpu-big-ideas-30-1114.md); already in
the build and so left out: CG-13 (all guest float code in flush-to-zero: scalar denormals already flushed), the
near-exact switches (#1, #5, #17, #19, #41), LR elision, r1 preserved.

What the translator does today, read for this list: `fres` is `double(float(1.0 / x))` (a double divide) and
`frsqrte` is `1.0 / sqrt(x)` in double (a square root and a divide, both unpipelined on the A57); `mfcr` builds all 32
CR bits with 32 conditional ORs; `sync` and `eieio` are seq_cst fences and `lwsync` an acq_rel fence (all a full
`dmb ish` on AArch64); the Xenon priority hints (cctpl / cctpm / cctph) are already no-ops.

Savings: % of the machine copies' time (84 ms of the ~117 ms of CPU a frame, all threads) or ms a frame, all threads;
unmeasured. "Check" is how to find out how often the assumption is wrong before building it.

| # | Idea | What it saves | Est. | When it is wrong | Check | Difficulty |
|---|---|---|---|---|---|---|
| 130 | `lwsync` before a store becomes a release store (`stlr`), after a load an acquire load (`ldar`); in code touching no shared data, dropped | a full `dmb ish` per lwsync today | 0.5-2 ms | a lock-free queue whose ordering came from a dropped fence: rare wrong values or a hang | lwsync count per hot copy; the guard race | Medium |
| 131 | XER[CA] assumed dead at calls and returns: the carry of srawi / addic / subfic computed only where read in the same function | carry computations | 0.3-1 ms | hand assembly or millicode passing a carry across a call | scan for a CA read before any CA write after a call or at entry | Easy |
| 132 | Single-precision ops (fadds, fmuls, fmadds, fdivs...) in host single precision without CG-14's proof that the inputs are singles | fcvt pairs and double rounding sequences | 1-3 % | an input that is not a single (a double from fadd or lfd) rounds twice: last-bit differences | PC census of non-single inputs | Medium |
| 133 | Double-precision chains that end in frsp / stfs computed in single (C promotes float math to double: `x * 0.5`) | double ops, conversions, double fdiv / fsqrt | 0.5-2 % | one op gives the same result; chains of 2+ differ in the last bit | PC census of chain lengths | Medium |
| 134 | Guest memory split for the compiler into regions: the stack, .data / .bss and the heap as separate restrict-qualified arrays | GCC keeps stack slots and globals in registers, moves loads past other regions' stores | 2-5 % | a heap pointer into the stack or a global, written in the same function | escape scan: functions that turn `addi rX, r1, N` or a global's address into a pointer stay as today | Hard |
| 135 | Fast-math subset on the float-heavy shards: -ffinite-math-only, -fno-signed-zeros, -fassociative-math | reassociation (vectorized sums), NaN / inf paths folded | 1-3 % | NaN or inf in physics; sums in another order | PC race: positions and lap times against today's | Easy (flags) |
| 136 | -freciprocal-math: repeated divisions by one denominator become one reciprocal and multiplies | unpipelined fdiv | 0.3-1 ms | last-bit differences | none needed | Easy |
| 137 | Loop lifting: the guest's counted loops emitted as structured `for` loops with restrict pointers, so GCC auto-vectorizes them (NEON) | the 360 compiler's scalar loops: copies, mixing, transforms | 1-4 ms | overlapping arrays (in-place processing); a runtime overlap check makes it exact | loop versioning | Very hard |
| 138 | No 32-bit wraparound on base + displacement: one host pointer per guest base register, then immediate offsets and ldp / stp pairs | address arithmetic on every access (if the copies redo the 32-bit add per access) | 2-5 % | rA + d crossing 4 GB (never for valid 360 addresses) | read the copies' addressing first | Medium |
| 139 | 32-bit general registers in functions that use no 64-bit instruction (upper halves not modelled) | rlwinm wrap masks, sign extensions, 64-bit multiplies and compares | 1-2 % | a 64-bit op (ld / std / rldic* / cmpd) elsewhere on a value from 32-bit code | per-function scan for 64-bit instructions | Medium |
| 140 | Typed guest memory: float, integer and vector accesses through different C types (strict aliasing between them) | GCC keeps values in registers and reorders across accesses of the other kind | 1-3 % | type punning: float bits handled as integers (abs by mask, fast inverse sqrt, lfd / stfd copies) | scan for both kinds at one address in a function | Hard |
| 141 | Read-mostly globals folded as constants (a PC census proves no write after boot) | tuning values and singleton pointers read every frame | 0.5-1.5 ms | a value changed later (options, another track) | write census on those pages; a guard that reverts | Medium-hard |
| 142 | Fast approximate float math in place of the guest CRT's (sinf, cosf, atan2f, powf, expf, logf: NEON polynomials, 2-4 ulp) | the CRT's PPC implementations | 0.5-2 ms | last-bit differences in physics and camera | calls per function first | Medium |
| 143 | Speculative if-conversion including loads: short branches become csel, their loads run unconditionally | mispredicted branches in hot copies | 0.5-1.5 % | a load from an unmapped or watched page (a handler trip, or a crash on null) | only bases proven non-null in the block | Hard |
| 144 | `fres` / `frsqrte` as host estimates (frecpe / frsqrte) plus one Newton step, in single | a double divide, or a double sqrt plus a divide, per instruction | 0.3-1.5 ms | last bits differ from today's (today's are more precise than the 360's own estimates) | none needed | Easy |
| 145 | Back-chain stores dropped (stwu's store of the old r1), except in functions that read 0(r1) | a store and a byte swap per call | 0.3-1 ms | a stack walker or an alloca-style epilogue the scan missed | static scan for 0(r1) reads | Easy-medium |
| 146 | At call sites only the ABI argument registers materialized (r3-r10, f1-f13, v2-v13); millicode helpers excepted | context writes before every call | 1-2 % | non-ABI code reading a caller's r0 / r11 / r12 or another volatile register | scan of millicode (__savegprlr...) and hand assembly | Medium |
| 147 | Guest spin-wait loops (a load, a compare, a branch back, often a priority hint) given a host back-off (yield after N turns) | cores burnt by spinning threads (no SMT on the Switch) | 0-2 ms | timing only: a loop that is not a wait, slowed | count loop iterations per site on the PC | Medium |
| 148 | Loop counters and pointers as non-wrapping induction variables (signed or 64-bit) | GCC's unrolling, vectorization and strength reduction, blocked by uint32 wraparound | 0.5-2 % | a loop whose counter really wraps | none practical | Medium |
| 149 | Profile-pruned copies: paths never seen in the training profiles cut out, with a bail-out into the generic translation at each cut | code size, register pressure, branches in the hot copies | 2-5 % | a bail-out that does not hand the state over exactly | the guard; bail-out counts | Very hard |
| 150 | Card marks once per page per function call (#38's per-block batching widened to the whole function) | the copies' mark instructions (10 G run marks in a PC race) | 1-3 ms | a snapshot taken mid-function misses a later store's mark until the next one | Renut's snapshot timing census | Medium |
| 151 | lvx / stvx alignment masks (`& ~15`) dropped: addresses assumed aligned | one instruction per vector access | 0.1-0.5 % | an unaligned address relying on lvx's masking (the lvsl / lvx unaligned idiom) | scan for lvsl / lvsr; PC census of unaligned lvx | Easy |
| 152 | Dot products (vmsum3fp128 / vmsum4fp128) as fused multiply-add chains | separate multiplies and adds, each rounded | 0.5-1.5 % of vector copies | last-bit differences | none needed | Easy |
| 153 | mfcr / mtcrf in prologues and epilogues narrowed to the nonvolatile fields (cr2-cr4), or dropped when the function never writes them | today 32 conditional ORs per mfcr | 0.3-1 ms | code inspecting a volatile field through the saved word | scan: mfcr results only stored and reloaded by mtcrf | Easy |
| 154 | lwarx / stwcx. as host load-exclusive / store-exclusive (LDXR / STXR) where both sit in one function with no call between | the compare-and-swap emulation of reservations | 0.3-1 ms | livelock if a load between them clears the monitor every time; ABA behaviour changes | stress test | Medium |
| 155 | vtable pointer loads hoisted across calls (an object's vptr assumed constant while its method runs) | reloading the vptr before each virtual call | 0.3-1 ms | constructors and destructors changing the vptr mid-call | none practical | Medium |
| 156 | Indirect-call lookups without the range and null checks | every bctrl through the function table | 0.3-1 ms | a bad pointer crashes the host instead of being reported | none needed | Easy |
| 157 | Leaf functions' red-zone slots (below r1, no stwu) as host locals | stack loads and stores in leaf copies | 0.5-1 % | a red-zone address used as a pointer inside the leaf | escape scan | Easy-medium |
| 158 | Repeated calls to store-free getters with the same arguments merged within a function | small getters called again and again in hot functions | 0.3-1 ms | another thread changes what the getter reads between the two calls | per-callee store-free proof | Medium |
| 159 | Non-escaped stack slots survive calls (no reload after a call) | reloads after every call | 1-3 % | a callee writing the caller's frame through a pointer the escape scan missed | escape scan + guard | Hard |

## Where to start

- **Cheap and likely worth it:** #144 (estimates instead of double divide / sqrt), #153 (mfcr), #130 (lwsync as
  release / acquire forms: the exact half first), #156, #131. Each is a builder change plus one codegen.
- **Biggest:** #134 + #159 (stack and globals the compiler can keep in registers), #138 (if the copies redo the 32-bit
  add per access), #149 (profile-pruned copies), #137 (auto-vectorized loops).
- **Flags only (one build, an A/B):** #135, #136.
