# Codegen and machine-copy ideas for the 4-core frame-time chain: CG-01 .. CG-40

Written 2026-11-12. Read-only study: nothing was built, run or edited besides this file; the console, the emulators and the
game were not touched. Companion of `docs/vector-store-ideas.md` (VS-01..42) and `docs/vector-ideas-2.md` (VX-01..40), whose
ideas are not repeated (a few build on them and say so). This round starts from the user's direction of 2026-11-12: the game
runs 4-core only, CPU fell 206 -> 184 ms a frame but the frame rate stayed ~18.3 fps because the savings landed in JQ Worker
3/4 and the XMA Decoder, which are off the frame's critical path. So every idea is ranked by what it could do for the
**Game Master (GM), JQ Worker 0/1/2 (the render/scene jobs) and the Renut render thread ("?")**.

## 0. Method, and what the numbers are not

- **Profile.** `C:/blur-nx-test/console_runs/2026-11-12_fma_a/rex/rex_profile.log` with its ELF `blur-nx_fma.elf`, race
  reports 10..28 (19 windows, 18.4 fps). Per-thread CPU per frame (ms; the scripts' own scale): JQ4 34.8, JQ3 31.4, **GM 27.2**,
  XMA 19.0, **JQ0 18.3, "?" 18.1, JQ1 14.1, JQ2 10.3**, XThread 8.4, Audio 0.9, TimerQueue 0.9, main 0.5; total 183.9. The
  chain's CPU (GM + JQ0-2 + "?") is **88.0 ms**; 5,663 of the 7,517 samples of GM + JQ0-2 (75 %) are in machine copies.
  One sample is 10.6 us on GM, 8.6 us on JQ0-2.
- **My own read-only scripts** (in the scratchpad: `cg_mix.py`, `cg_static.py`, `cg_ctx.py`, `cg_kinds.py`, `cg_census.py`,
  `cg_sync.py`, `cg_ftoi.py`, built on `thread_ms.py` / `hot_pcs.py` / `insn_mix.py`): (a) the instruction class every sample
  sits on, per thread; (b) the load address form of the sampled loads; (c) a **weighted statement census**: for the 320
  machine copies that carry the chain threads' samples, every `f_` body in `app/generated/hot/n_*.h` was scanned and each
  pattern's count divided by the body's statement count, weighted by the function's chain-thread samples. "x % of statements" below
  is that figure. One percent of statements at average cost is about **0.5 ms** of chain CPU a frame (75 % of 70 ms in copies);
  a pattern that costs more than an average statement is scaled in the entry and says so.
- **What the numbers are not.** Samples sit on the instruction that retires next, so a stalled load shows as a sample on the
  load or the one after it; the profile is a share of CPU, not a measurement of a change. The "ms" in every entry is **CPU of
  the named threads, summed**; what reaches the frame is less (the three JQ threads share their work; a handler on GM or the
  render thread counts one for one only if that thread is on the longest dependency). All of them are order-of-magnitude
  estimates from samples, not measurements. Static site counts are guest-instruction sites in `app/generated/renut_recomp.*.cpp`
  (186 files, 2,571,083 statement groups) or machine-copy sites in `app/generated/hot/n_*.h` (1,099 headers).
- **Exactness.** **E** = bit-identical to the behaviour of the current generated code; **E\*** = exact except for bytes of a dead
  guest stack frame (and says so); **D** = a decision for the user (a behaviour or policy changes); **M** = measurement or
  process, no behaviour change. The PC guard (`banjo_hot_guest.cpp`) compares a copy with the original statements *as they stand
  after post_codegen*, so it cannot see a change made to a header, a macro or a post_codegen pass (both sides share it); only the
  forms self-check (`banjo_forms_selfcheck.cpp`), `tools/arm_forms_check.py`, unit tests under `sdk/tests` and the console race can.

## 1. What the profile says (the ground the ideas stand on)

**F1. The 4-core game cores are saturated on paper, and the frame still does not move.** `banjo_system_core=1`
(`sdk/src/core/threading_switch.cpp`) pins audio, the XAudio mixer and shader compiles to core 3 and leaves "every other thread"
on cores 0-2; `banjo_system_core_xma=false` keeps the XMA Decoder on 0-2 too. The threads on cores 0-2 add up to
27.2 + 108.9 (JQ0-4) + 18.1 + 8.4 + 19.0 = **181.6 ms of CPU in a 54.6 ms frame: 3.33 core-equivalents on three cores
(111 %)**. If the three cores were the limit, the 22 ms saved in JQ3/4/XMA would have shown as frame time. It did not, so either
the layout differs from what the cvars say, or the frame is held by a dependency chain (GM -> JQ jobs -> render thread -> GPU
with hand-offs) rather than by CPU supply. Nothing in the data separates the two: this is the first thing to find out (CG-03).
GM is 50 % busy (27 of 54 ms), the render thread 33 %, JQ0 34 %; none is the saturated stage.

**F2. GM is memory-latency bound.** 44.6 % of GM's samples sit on integer loads and 14.0 % on vector loads (stores 15.5 %);
JQ0-2: 29.1 % + 15.2 % loads. Of GM's 1,143 sampled integer loads, 397 use the guest form `[x, w, uxtw]`, about 510 are
`[xN+imm]` off a host pointer built from a guest address, **90 (8 %) are `[sp]` spills and 43 (4 %) are ctx fields** (`[x19+imm]`):
the stalls are guest-data misses, not register traffic. GM is also flat: 400 functions make up 23.7 of its 27.2 ms, the top
one (`memcpy`) 0.84 ms, `sub_8252D4B0` 0.66, `sub_82523550` 0.45, `sub_826A7858` 0.41; the top 50 are 11.6 ms. No single
function is a GM win.

**F3. The JQ0-2 time is vector math plus the recording thread work.** Top: `sub_8250A7E8` 2.89 ms (an 82 KB copy, a vector
normalise with the `vrsqrtefp` special-case fix-up chain, `fcmgt / fcmeq / bsl / bit` after every `frsqrte`), host `NewOp` 2.26,
`sub_8251E6F0` 2.15, `sub_8252E168` 2.05, `sub_825123B8` 1.77, `sub_825087A0` 1.35, `sub_829D2E80` 1.16 (the Huffman decoder,
not a copy of its own: a generated function with a hand-native draft that was declined), 15 more between 0.5 and 1.3 ms. The
vector-arithmetic ones are the VS/VX lists' ground; the instruction classes there (fcvt 2.9 %, msr 1.4 %, tbl 0.8 %) are what
this list takes.

**F4. The render thread is host code and already tuned.** `FastDrawLocked` 3.2 ms (two instances), `Replay` 1.68, `memcpy`
1.57, `WholeGeometryTry` 0.96, `FillTextures` 0.82, `PagesClean` 0.57, `PushFloatConstants` 0.84 (two). Its hot spots are small
(10 of 225 samples at most); `FastDrawLocked` already has hash + `prfm`, `PushFloatConstants` already has dirty bitmaps and
A/B notes. 39 of the 132 `memcpy` samples sit on the first `ldp x6,x7,[x1]` of the 17..32-byte path: a source line another core
wrote.

**F5. Per-statement plumbing of the machine copies is small, except calls and the float mode.** Weighted over the 320 copies:
call synchronisation `{ ctx.rX = l_rX; ...; call; l_rX = ctx.rX; ...}` is **2.46 % of statements** (11,229 stores and 32,049
reloads in the 320 bodies: 0.082 stores and 0.199 reloads per statement; GCC deletes reloads of dead values, not the stores);
`REX_CARD_FLUSH` 2.72 %; `REX_CARD` 1.31 %; indirect calls (`REX_CALL_INDIRECT_FUNC`) 0.25 % (539 sites); the float-mode
helpers (`*AnyMode`) **1.05 %** (2,386 sites) plus `enableFlushMode` / `disableFlushMode` 0.55 % / 0.20 %; frame traffic
(`REX_CHAIN_STORE` 0.27 %, `REX_DEAD_SAVE` 0.12 %, LR 0.03 %); `s.Stopped()` / `s.Note()` / `s.Hit()` are `constexpr` no-ops in the
Fast form (`banjo_hot_common.h:121,123`) and cost nothing. Static: 15,736 `msr fpcr` sites in the ELF, 11,559 `bctrl` sites in
the game, `indirect_promotions.txt` is **empty** ("none until a PC census"), and the PGO flags carry `-fno-profile-values`.

**F6. The scalar float in VMX regions is expanded, not switched.** `diet_ops.h`'s `*AnyMode` helpers avoid the 41-cycle
`msr fpcr` by testing for denormals instead. In `sub_8251E6F0` (sample 15 at `0x708cf8`) one `fadds` is
`fadd d31 ; fcmp ; b.eq ; fmov x0,d31 ; and ; sub ; cmp ; b.cc ; fcvt s31,d31 ; fmov w0,s31 ; and ; sub ; cmp ...`: the double-tininess
test and the single-denormal test, both of which the operands' origin (a single) makes dead. 109 chain samples sit on the
`fcvt` after such a `b.cc`.

**F7. Things that look hot and are not.** Barriers: 1,791 `dmb ish` sites, 0.4 % of GM samples and 0.1 % of JQ0-2 (one function,
`sub_8227CA08`). Soft TLS (`-mtp=soft`): 1,242 `__aarch64_read_tp` calls, none in a copy on the chain. GOT: 9 KB. No `nop`
padding (no erratum workaround). Instruction fetch: samples sit on the first instruction of a 64-byte line 7.8 % (GM) / 9.0 %
(JQ0-2) of the time against 6.25 % uniform, so sequential fetch misses explain 1.5-2.7 % of the chain at most.

## 2. Ranked table (expected 4-core frame-time gain, then confidence)

Gain = estimated CPU of GM + JQ0-2 + "?" a frame (ms, range); "frame" = what I would expect to reach the frame. Conf = H/M/L.

| rank | ID | title | kind | est. gain (CPU ms; range) | conf | risk |
|---|---|---|---|---|---|---|
| 1 | CG-01 | CHAIN-PRIO: priority tiers, chain above JQ3/4/XMA/XThread | D | frame 0-4 | L | low |
| 2 | CG-13 | FPCR-ONE-MODE: no flush-mode switching and no AnyMode expansion | D | 1.2-2.0 | M | medium |
| 3 | CG-02 | CORE-MAP: fixed cores for GM, render and JQ0-2 | D | frame 0-3 | L | medium |
| 4 | CG-06 | INDIRECT-PROMOTE: fill `indirect_promotions.txt` from a census | E | 0.3-1.0 | M | low |
| 5 | CG-04 | SPIN-BEFORE-WAIT on the chain's hand-offs | D | frame 0-2 | L | medium |
| 6 | CG-23 | PF-USE: prefetch the object behind a freshly loaded pointer (GM) | D/E | 0-1.5 | L | low |
| 7 | CG-03 | HANDOFF-TRACE: wake latency and critical path per frame | M | (decides 01/02/04) | H | none |
| 8 | CG-05 | L2-HYGIENE: off-chain streaming kernels without evicting the chain | D | 0-1.5 | L | low |
| 9 | CG-10 | SYNC-LIVEIN: store only what the callee reads at a call | E | 0.3-0.6 | M | low |
| 10 | CG-14 | SINGLE-KNOWN: operands proven single drop the double-denormal tests | E | 0.2-0.4 | M | low |
| 11 | CG-25 | HOT-COLD-SPLIT: `-freorder-blocks-and-partition` check | E | 0.1-0.8 | L | low |
| 12 | CG-30 | NEWOP-PAGE-CLEAN: skip the 2.7 KB state compare when the page is clean | E | 0.2-0.5 (JQ) | M | low |
| 13 | CG-07 | ICP-VALUES: GCC value profiles for the indirect-call TUs | E | 0.1-0.5 | L | low |
| 14 | CG-08 | SITE-CACHE: per-site inline cache for what stays indirect | E | 0.1-0.3 | M | low |
| 15 | CG-15 | FLOAT-TYPED-HELPERS: `AddSingleAnyMode(float, float)` forms | E | 0.15-0.35 | M | low |
| 16 | CG-09 | HAND-ARGS: hand natives called with registers, not ctx | E | 0.1-0.3 | M | low |
| 17 | CG-31 | RENDER-SMALL-COPY: inline <= 64-byte copies, prefetch the source | E | 0.1-0.4 (render) | L | low |
| 18 | CG-26 | NO-TRAPPING-MATH for the float-heavy shards | E | 0-0.4 | L | low |
| 19 | CG-27 | SCHED-PRESSURE: pre-RA scheduling for the biggest copies | E | 0-0.5 | L | low |
| 20 | CG-29 | JQ-HUFFMAN: native for `sub_829D2E80` (a declined draft, revisited) | E | 0.2-0.5 | L | medium |
| 21 | CG-11 | CARD-FLUSH-GUEST: no card flush at a call whose reach is guest code only | E | 0.1-0.3 | M | low |
| 22 | CG-24 | LOAD-BATCH: loads past stores with proven-distinct addresses | E | 0-0.5 | L | low |
| 23 | CG-12 | CLOSURE-BY-CALLS: closure membership by call counts | E | 0.1-0.3 | L | low |
| 24 | CG-19 | FRAME-ELIDE-ON: the three opt-in frame elisions in the copies | E\*/D | 0.1-0.3 | M | low |
| 25 | CG-34 | REPLAY-STREAM-PF: prefetch the next op in `Replay` | E | 0.05-0.2 (render) | L | low |
| 26 | CG-17 | ITOF-FTOI: `std/lfd/fcfid` and `fctiwz/stfd/lwz` as register conversions | E / E\* | 0.05-0.15 | M | low |
| 27 | CG-20 | STACK-FWD: store -> load of the same `r1` slot in the same block | E / E\* | 0.05-0.15 | M | low |
| 28 | CG-16 | RSP-FUSE: `frsp` as one check, not two | E | 0.05-0.15 | H | low |
| 29 | CG-32 | MUTEX-SPIN: short host critical sections without `mutexLock` | E | 0.05-0.2 | L | low |
| 30 | CG-18 | FSEL-PLAIN: `fsel` as `fcmp` + `fcsel` where the mode is known | E | 0.03-0.1 | M | low |
| 31 | CG-28 | ALIGN-HOT: function and loop alignment of the biggest copies | E | 0-0.2 | L | low |
| 32 | CG-21 | VEC-LOCALS-REG: vector locals as registers, not address-taken arrays | E | 0-0.1 | L | low |
| 33 | CG-22 | CTX-STORE-DEDUP: skip a ctx store the slot already holds | E | 0.01-0.05 | H | none |
| 34 | CG-33 | CONST-UPLOAD-SKIP: record-time compare of constants (the census exists) | E | 0-0.2 (render) | L | low |
| 35 | CG-35 | SHADOW-DIRTY-BITS: one dirty word per 64 chunks from the store cards | E | 0.05-0.2 (JQ) | L | medium |
| 36 | CG-36 | CALL-ABI-REGS: keep `ctx` and `base` out of the callee-save traffic | E | 0-0.3 | L | medium |
| 37 | CG-37 | PGO-FRESH: profile coverage after each post_codegen change | M | (protects every gain) | H | none |
| 38 | CG-38 | FAN-IN-INLINE: fold the 0.1-0.3 ms tail of tiny callees by call count | E | 0.05-0.2 | L | low |
| 39 | CG-39 | GM-TAIL-NATIVES: three more GM functions with a measurable loop | E | 0.1-0.3 | L | medium |
| 40 | CG-40 | STALL-ATTRIBUTION: sample the cache-miss PMU, not the retire PC | M | (decides 06, 23, 24) | M | none |

The ranking says plainly where the weight is: the first six rows are about *when and where threads run and what the float mode
costs*, not about statements, because the statement-level plumbing of the copies is at most a few percent of the chain
(F5) and GM's time is memory latency (F2). The exact ideas in the table add up to about 2-4 ms of chain CPU, the decisions
to perhaps 2-4 more, and the scheduling decisions to an unknown amount between 0 and the whole frame gap.

## 3. The ideas, by theme

### A. Scheduling, hand-offs and the cache the chain shares (the frame-time levers)

### CG-01  CHAIN-PRIO: priority tiers, chain above JQ3/4/XMA/XThread   (D)
- **Pattern.** F1: 181.6 ms of CPU on three game cores, JQ3 + JQ4 + XMA = 85.2 ms (46 % of all CPU) off the chain, all of them
  at the game threads' one priority (`banjo_system_core_mixer_priority` talks of "the game threads' 0x3B"). A chain thread that
  wakes while a JQ3/4 job holds its core waits for the kernel's time slice.
- **Transform.** After thread creation (`threading_switch.cpp`, `RexSwitchSetCurrentThreadPriority`, the guest thread names are
  known there) put GM, the render thread and JQ0-2 one or two steps *above* (numerically below) JQ3, JQ4, XThread and the
  XMA Decoder. cvars `blur_prio_chain` / `blur_prio_offchain`, default off. Respect the NPDM's allowed range.
- **Where.** `sdk/src/core/threading_switch.cpp`, a per-name table in the title's startup.
- **Exactness.** D: nothing in the guest's results changes (guest priorities are the game's own and stay), but the audio
  jobs' timing does: the XMA/JQ4 buffers are decoded ahead, so a small lateness is tolerated; stutter is the risk.
- **Gain.** Frame 0-4 ms. If F1's wake-up wait is real, this is the largest single lever; if the frame is held by a dependency
  with idle cores, it is zero. **Confidence L.** **Risk** low (a cvar). **Verify** CG-03's trace first; then the race A/B
  with audio underrun counters (Audio Worker / XMA ring depth) in the log.

### CG-02  CORE-MAP: fixed cores for the chain   (D)
- **Pattern.** With three game cores and the Horizon scheduler free to migrate: GM 27.2 + render 18.1 + XThread 8.4 =
  53.7 ms fit one core each only if split 27 / 26; JQ0-2 are 42.7 ms on one or two. A thread that migrates drops its L1/L2
  working set (F2: GM's loads are 60 % of its samples).
- **Transform.** GM -> core 0 only, render thread (and XThread) -> core 1 only, JQ0-2 -> core 2 (mask 0b100, or 0b110 as a
  fallback), JQ3/JQ4/XMA -> the leftover of core 0-1 plus core 3 (`banjo_system_core=2` already allows 3). 85 ms of off-chain
  work cannot all fit (free capacity 66 ms on 0-2): some of it must go to core 3, where the system takes about half
  (the 2026-10-27 note in `threading_switch.cpp`).
- **Where.** `threading_switch.cpp` (`RexSwitchSetCurrentThreadAffinity`-style helpers exist at lines 334-440).
- **Exactness.** D. **Gain.** Frame 0-3 ms (migration cost + the stall in CG-01). **Conf L.** **Risk** medium: a pinned thread
  cannot use an idle core when its own is busy (the reason Horizon's default migrates). **Verify** CG-03; race A/B; check core 3 idle
  against audio stutter (the 2026-10-27 audio note).

### CG-03  HANDOFF-TRACE: wake latency and the critical path per frame   (M)
- **Pattern.** Everything above is guessing, because the profile is a PC sampler: it says what a thread does while running, not
  what it waits for. GM is 50 % busy, the pipeline GM -> JQ jobs -> render -> GPU totals more than 54 ms in series
  (27 + 20 + 32), so the stages overlap, and where they wait for each other is unmeasured.
- **Transform.** A ring buffer of `(thread, object, op, tick)` for every guest wait / signal (`XObject::Wait`,
  `WaitMultiple` (the biggest user of `__aarch64_read_tp`: 62 calls), event set, semaphore release, critical section
  contention, the Renut queue's own fences) using `armGetSystemTick()` (19.2 MHz, 52 ns), dumped once a second into
  `rex_profile.log`. A small script then builds, per frame, the longest chain of "A signalled at t, B ran at t + d" edges
  and the wake latency distribution (signal -> running), plus per-thread time blocked.
- **Where.** `sdk/src/system/xobject.cpp`, `sdk/src/core/threading_switch.cpp`, `app/src/renut_engine/gpu/backend/queued.cpp`,
  `tools/` for the script.
- **Exactness.** M (compile-time switch off by default). **Gain.** None itself; it decides CG-01, 02, 04 and says whether any
  CPU saving can reach the frame. **Conf H** that it is informative. **Risk** none off. **Verify** the trace on a PC run first
  (the same hooks), then one console race.

### CG-04  SPIN-BEFORE-WAIT: bounded spin before the chain blocks   (D)
- **Pattern.** The chain's hand-offs are kernel waits (`PosixConditionBase::WaitMultiple`, `svcWaitSynchronization`,
  `PosixEvent::Set` shows on GM at 0.04 ms). A Horizon wake costs a kernel scheduling step and, across cores, an IPI: tens of
  microseconds, with 10-30 hand-offs a frame it is a millisecond or more of *latency*, none of it CPU.
- **Transform.** In the wait path for events and semaphores that another chain thread normally signals within ~20 us, spin up
  to N iterations (`yield` / `wfe`-free polling of the object's state word) before the blocking call. Only for GM, JQ0-2 and
  the render thread, by thread name.
- **Exactness.** D (timing only). **Gain.** Frame 0-2 ms if the wake latency of CG-03 is above ~30 us on chain edges; negative when
  the cores are saturated (F1), because the spin burns what the other threads need. **Conf L.** **Risk** medium.
  **Verify** CG-03.

### CG-05  L2-HYGIENE: the off-chain streaming kernels must not evict the chain   (D)
- **Pattern.** The A57 cluster shares one 2 MB L2. JQ3/4 + XMA take 46 % of all CPU and run streaming 1 KB copy/zero kernels
  (VS-07) and the XMA decoder over multi-MB buffers; GM's data loads are 12 ms a frame of stall (F2).
- **Transform.** In the kernels JQ3/4 run (the natives in `blur_hand_natives_jq34.h`, the `memcpy`/`memset` calls) use `ldnp/stnp`
  pairs or `prfm pldl2strm` / `pstl2strm` and `dc zva` only where the destination is overwritten whole; keep the vector copy
  kernels' lines out of L1 with streaming hints. **Not known to help on the A57** (its manual treats the non-temporal pair like
  a normal pair, as far as I remember): the idea is the micro-benchmark (read a 1.5 MB working set on core 0 while core 1
  streams 8 MB with each variant; `sdk/tests/arm_forms_check.cpp` can host it) before anything else.
- **Exactness.** D (no value changes; E for the data, the policy is the decision). **Gain.** 0-1.5 ms on GM (if 10 % of its
  load stall is eviction by the neighbours). **Conf L.** **Risk** low. **Verify** the micro-benchmark, then the race with JQ3/4
  paused artificially (an upper bound on what the neighbours cost GM).

### B. Calls, dispatch and the ctx hand-over

### CG-06  INDIRECT-PROMOTE: fill the promotion list from a census   (E)
- **Pattern.** `app/indirect_promotions.txt` holds one comment line ("none until a PC census of this game"). The game has
  **11,559 `bctrl` sites** (7 % of 153,869 direct `bl`); the 320 chain copies hold 539 `REX_CALL_INDIRECT_FUNC_AT` sites
  (0.25 % of their statements); e.g. `n_82504618.h:665,672` (`REX_CALL_INDIRECT_FUNC_AT(0x82504618, 1, ctr.u32)`), the virtual calls
  of a vector-math class. Each dispatch is a range test, a load from `base + IMAGE_BASE + IMAGE_SIZE + (target - CODE_BASE) * 2`
  (a 20.4 MB table: 8 bytes per 4 bytes of code, so one cache line and one TLB entry per target function), a null test and `blr`.
- **Transform.** Run the existing PC census (`g_rex_indirect_census`, `tools/indirect_promotions.py --census ...`, defaults
  99.5 % single or 70 % dominant) over the attract loop and a race; write `indirect_promotions.txt`; `tools/indirect_calls.py
  --promote` then emits `if (target == T) f_sub_T(ctx, base, s); else <indirect>`, which a copy's closure can inline and whose
  `ctx` hand-over then shrinks (CG-10).
- **Where.** `tools/indirect_promotions.py`, `tools/indirect_calls.py` (exist), the census build (PC).
- **Exactness.** E: the guard compares the target; a call that goes elsewhere takes the old path. **Gain.** 0.3-1.0 ms: 0.25 % of
  statements x ~15 statement-equivalents saved per promoted dispatch (about 8 instructions plus a probable table-line miss and an
  indirect-branch mispredict), x ~70 % of sites that are monomorphic enough, x 0.5 ms per percent. **Conf M.** **Risk** low
  (code size: a promoted target that is a copy is inlined only up to the closure's limit). **Verify** guard on the promoted copies;
  the PC check build with `REX_PROMOTED_CALLS_OFF` unset; console race.

### CG-07  ICP-VALUES: GCC's own indirect-call promotion for the TUs that have them   (E)
- **Pattern.** The PGO flags in `app/CMakeLists.txt:683,702` include `-fno-profile-values`, which switches off GCC's
  value-profile transformations, the indirect-call promotion among them. `rex_indirect_func_(ctx, base)` is exactly the call
  GCC would speculate on (`if (f == sub_X)`).
- **Transform.** For the shards that hold the 11.5k indirect sites (the generated `renut_recomp.*.cpp` and the bulk shards),
  take the profile-generate / profile-use pair with `-fprofile-values` *only there* (a per-source option), keeping the
  instrumented build's size and speed cost out of the rest. Complements CG-06 (which needs no profile run on the console and also
  helps the PC build).
- **Exactness.** E. **Gain.** 0.1-0.5 ms beyond CG-06 (sites CG-06's thresholds leave: polymorphic ones with two or three hot
  targets). **Conf L:** the reason the flag was put on is not in the notes (instrumented build size / speed is my guess).
  **Verify** one shard's `.gcda` and the disassembly of one virtual call.

### CG-08  SITE-CACHE: a one-entry inline cache for the sites that stay indirect   (E)
- **Pattern.** The lookup table of CG-06 is 20.4 MB and each target occupies a line of it. A virtual call site that sees one or
  two types most of the time still loads a line per call.
- **Transform.** `REX_CALL_INDIRECT_FUNC_AT(function, site, x)` gets a per-site static pair `{target, PPCFunc*}` (16 bytes, in
  the copy's data, written with relaxed stores, read as a single 128-bit load so the pair is torn-free): hit -> call, miss ->
  the current lookup and an update. Per-site data is hot with the site and shares nothing with other sites.
- **Where.** `renut_pch.h` macro (`REX_CALL_INDIRECT_FUNC_AT`), `tools/indirect_calls.py` (site ids exist).
- **Exactness.** E (the cache holds what the table holds; thunk installation / hook changes invalidate by a generation word
  that `ResolveIndirectFunction` bumps). **Gain.** 0.1-0.3 ms after CG-06. **Conf M.** **Risk** low: a stale entry after a
  function is hooked at run time (the generation word is the whole risk). **Verify** unit test with a hook installed between two
  calls; guard; race.

### CG-09  HAND-ARGS: hand natives called with registers, not through ctx   (E)
- **Pattern.** A call to a hand native syncs ctx first: `{ ctx.r3 = l_r3; ctx.r5 = l_r5; ...; REX_CARD_FLUSH(); if
  (!::banjo::hand::n_82520D78::Native(ctx, base, s)) { f_sub_82520D78(ctx, base, s); ... } l_r3 = ctx.r3; ...}`
  (`n_8252D4B0.h:666, 699, 726`: four calls in a row, each storing r3/r5/r9/r10). 64 such sites in the 320 chain copies (0.10 % of
  statements), many more across all copies; each native reads 2-6 registers from ctx and writes none (a hand native "writes no
  register", `localize_native.py` docstring).
- **Transform.** Natives also provide `NativeArgs(base, s, a0, a1, ...)` taking the registers the native reads by value (the
  list is in `hand.txt`'s metadata), so the copy passes locals and does no ctx store/load for the native-only path; the
  fallback `f_sub_X` (rare: the native declines) keeps the full hand-over *inside* the decline branch.
- **Where.** `app/src/cpu/hot/hand.txt` metadata + `localize_native.py` (the call emitter) + the natives' headers.
- **Exactness.** E. **Gain.** 0.1-0.3 ms (0.10 % of statements x 6 stores/loads / 3 per statement x 0.5 ms), more if the
  natives are called in loops. **Conf M.** **Risk** low (mechanical; the guard sees every call). **Verify** guard (the native
  path compares the registers), `arm_forms_check`, race.

### CG-10  SYNC-LIVEIN: store only what the callee reads   (E)
- **Pattern.** The pre-call stores are "registers this function may have changed that the callee (or anything it calls)
  mentions" (`localize_native.py` docstring): *mentions*, not reads-before-writes. Over the 320 copies there are 11,229 such
  stores and 32,049 reloads; the post-call list is "registers the callee can have changed"; GCC drops reloads whose value dies
  but must keep every store (ctx is escaped memory). Example `n_82504618.h:665`: `ctx.r3, r4, r5, r10` stored before a virtual call.
- **Transform.** Use `tools/leaf_dead_stores.py`'s analysis in the other direction (it computes LiveOut per function; a LiveIn
  fixpoint over the same call graph, whole program, no ABI assumption) and store only the registers in the callee's LiveIn
  (indirect callees keep all). The reload list stays as is (cheap, dead ones die).
- **Where.** `tools/leaf_dead_stores.py` (LiveIn), `tools/localize_native.py` (sync emitter).
- **Exactness.** E: the callee cannot observe a register it never reads before writing. **Gain.** 0.3-0.6 ms: 0.082 stores per
  statement, perhaps 30-40 % of them dead-in (a callee that only writes r3 and mentions r4 for an overwrite) -> 0.03 per statement
  of ~3 instructions -> 1 % of the instructions of the copies -> 0.5 ms. **Conf M** (the 30-40 % is a guess; a census of the
  stores against LiveIn is the first step). **Risk** low; `kSkip` registers need care at natives. **Verify** guard (it compares
  registers after the call), forms self-check unchanged, race.

### CG-11  CARD-FLUSH-GUEST: no card flush at a call that cannot reach Renut's reader   (E)
- **Pattern.** `REX_CARD_FLUSH()` before every call: 2.72 % of statements (5,095 sites in the 320 bodies), each a compare and a
  conditional store of the pending run's card (`banjo_hot_prelude.h:57`). Cards are read by Renut at draw time (the views
  `0xA0000000+` and `0x7F000000+`); a callee that is guest code only (direct, known closure, no kernel import, no hook) never
  reads one.
- **Transform.** Skip the flush before calls whose reach is "guest only" (the reach set `localize_native.py` already computes
  for the register sync); keep it before hooks, imports, indirect calls and returns. The run variable lives across the call.
- **Exactness.** E on the card state at every point a reader can run (the run is flushed before any call that can reach one and
  at return). **Gain.** 0.1-0.3 ms (about half of the sites are guest-only; each flush ~4 instructions when the run is open).
  **Conf M.** **Risk** low. **Verify** the card self-check in the forms test, `REX_CARD_CHECK` build, race.

### CG-12  CLOSURE-BY-CALLS: closure membership by call counts, not size   (E)
- **Pattern.** A copy's closure takes "direct callees that are small enough"; the rest are calls with a full hand-over. In
  `n_8252D4B0.h` the callee `f_sub_82520D78` is declared `[[gnu::noinline, gnu::cold]]` though a hot function calls it at 7+
  sites (a native sits in front of it, so it is cold for good reason; the point is that the choice is by size and by the
  native, not by how often the call runs).
- **Transform.** Use the profile's call counts (the sample counts of the callee's own copy per caller, or the PC census's
  edge counts) to admit a larger callee when its call share in this caller is high, and to leave a small cold one out.
- **Where.** `tools/localize_native.py` (closure selection), `tools/switch_hot_bulk.py` (the profile reader).
- **Exactness.** E. **Gain.** 0.1-0.3 ms; **conf L** (no per-edge data in the profile; the PC census would be the source).
  **Risk** low (compile time and size: ~11 KB per copy already). **Verify** guard, NRO size, race.

### C. Scalar float in VMX regions (the mode switch and the AnyMode helpers)

### CG-13  FPCR-ONE-MODE: run the whole program in one flush mode   (D)
- **Pattern.** VMX code needs the flush mode on, scalar FPU code off. The generator switches (`msr fpcr`, ~41 cycles measured:
  15,736 sites, JQ0-2 1.4 % of samples, GM 0.5 %, and the instruction after each) or, between VMX instructions, expands the
  scalar op with denormal tests (`diet_ops.h`: 11,262 `SingleToDoubleAnyMode`, 4,951 `DoubleToSingleAnyMode`, 1,998 `GeZero`,
  1,824 `CompareAnyMode`, 982 `RoundToSingleAnyMode`, 476 `Mul`/245 `Sub`/142 `Add` `SingleAnyMode` in Blur; in the chain
  copies 1.05 % of statements, each ~8 instructions where a plain op is ~3). The disassembly of one `fadds` is 13 instructions
  (F6).
- **Transform.** Set FZ once (at thread start) and delete the whole machinery: `enableFlushMode` / `disableFlushMode` and the
  `*AnyMode` helpers become the plain operations; `lfs`/`stfs`/`fadds` etc. are `ldr+rev+fcvt` / `fcvt+rev+str` / `fadd`.
  Which mode: **FZ=1** (VMX's). The scalar FPU on the Xenon keeps denormals; with FZ=1 a scalar op that meets or produces a
  denormal returns zero instead. That is the whole change in behaviour, and on the A57 FZ=1 also removes the slow denormal path.
- **Where.** `sdk/include/rex/ppc/diet_ops.h`, `fpscr` class, `flush_hoist.py`, `flush_mode_flow.py` (made redundant),
  CMake `BLUR_FPCR_ONE_MODE` (OFF keeps today's code).
- **Exactness.** **D**: not bit-exact for denormal scalar values. A census build can count them first: a counter in the cold
  `*Slow` helpers (they already exist: they are exactly the denormal cases) over a race; if the counts per frame are
  ~0, the decision is easy.
- **Gain.** 1.2-2.0 ms of GM + JQ0-2 CPU: the AnyMode statements go from ~8 to ~3 instructions (1.05 % x 5/3 = 1.75 % of instructions
  = 0.9 ms) plus the `msr` samples (JQ0-2 1.4 % of 42.7 = 0.6 ms, GM 0.5 % of 27.2 = 0.14 ms). **Conf M**, **risk** medium (a
  result that depends on a denormal). **Verify** the census counter; forms self-check for the plain forms; guard (both sides
  change; the PC build comparing against the old forms gives the exact difference set).

### CG-14  SINGLE-KNOWN: operands proven single drop the double-denormal tests   (E)
- **Pattern.** `AddSingleAnyMode(a, b)` tests both operands for a denormal double, computes in double, tests the *double*
  result for tininess, converts, then `RoundToSingleAnyMode` tests the single again (F6: `sub_8251E6F0` `0x708cd4`). When both
  operands are widened singles (from `lfs`, from `frsp`, from a previous `*SingleAnyMode`, from an `fmr` of those) the sum or
  product of two floats is a multiple of 2^-149 and at most 2^128, so the double result is never a denormal double (the
  operand and result tininess tests are dead) and the only remaining question is whether the *single* result is denormal.
- **Transform.** A forward dataflow in `post_codegen` (`single_floats.py` has the type walk for the FPU-mode case) marks
  f-registers that hold exactly-single values (set by `lfs`, `frsp`, the `*SingleAnyMode` helpers, `fmr` of marked); at function
  entry and after calls unknown (cold). Rewrite `AddSingleAnyMode(a, b)` on marked operands to `AddSingleKnown(a, b)` =
  one float op and one test of the result bits.
- **Where.** a new `tools/fp_single_known.py` after `fma_single.py` and before `localize_native.py`, helpers in `diet_ops.h`.
- **Exactness.** E (proved above; the helper's cold path is the old one). **Gain.** 0.2-0.4 ms (the share of the 1.05 % that the
  dataflow can prove, perhaps 40 %, x 4 of 8 instructions saved). **Conf M** (the 40 % is a guess; a census of how many
  operand registers are marked at each helper site is the first step). **Risk** low. **Verify** guard; an exhaustive test of
  the helper against the old one on denormal / boundary floats in `sdk/tests`.
- *Superseded by CG-13 if CG-13 is taken.*

### CG-15  FLOAT-TYPED-HELPERS: `AddSingleAnyMode(float, float)` forms   (E)
- **Pattern.** Every helper takes and returns `double` (the register's type), so `fadds` is `fcvt d,s` on each operand,
  `fadd d`, `fcvt s,d`, `fcvt d,s`. With single-valued operands (CG-14) the double hop is pure overhead: `fcvt` shows on 2.9 % of
  JQ0-2 samples (142) and 109 of them follow a tininess `b.cc`.
- **Transform.** Helpers `AddSingleF(float, float) -> float` (operand test on the float bits: `and; sub; cmp` x 2; result
  test on its bits) and a representation change in the copies' locals for marked f-registers (a `float` local, widened only
  where a `double` use needs it, as `single_floats.py` does for FPU regions).
- **Where.** `diet_ops.h`, extension of `single_floats.py`.
- **Exactness.** E. **Gain.** 0.15-0.35 ms. **Conf M.** **Risk** low. **Verify** as CG-14. *Overlaps CG-14: do the two as one pass.*

### CG-16  RSP-FUSE: `frsp` as one check   (E)
- **Pattern.** `RoundToSingleAnyMode(d) = SingleToDoubleAnyMode(bit_cast<uint32_t>(DoubleToSingleAnyMode(d)))`
  (`diet_ops.h:318`): the first tests `a - 1 < FLT_MIN-ish` on the double, converts; the second tests the single bits for a
  denormal. If the first test passed (|d| >= 2^-126), the rounded single is at least FLT_MIN and cannot be denormal; the second test is
  dead, and so is the `fmov w,s ; and ; sub ; cmp` visible after the `fcvt s31,d31` in F6. 982 sites in Blur, 550 text occurrences in
  the hot headers.
- **Transform.** `RoundToSingleAnyMode(d)`: `if (|d| in slow range) return RoundSlow(d); return double(float(d));` with
  the slow function doing the old two steps.
- **Where.** `diet_ops.h` only. **Exactness.** E. **Gain.** 0.05-0.15 ms. **Conf H** (the proof is two lines); **risk** none.
  **Verify** exhaustive / boundary test of the new against the old on 2^32 float inputs widened plus the denormal boundary
  doubles (`sdk/tests`).

### CG-17  ITOF-FTOI: the stack round trips of int <-> float   (E / E\*)
- **Pattern.** The Xenon has no GPR <-> FPR move: `std r10,-16(r1); lfd f0,-16(r1); fcfid f13,f0` (3,713 sites in Blur) and
  `fctiwz f13,f0; stfd f13,88(r1); lwz r3,92(r1)` (1,479 sites): in machine code `str x10,[x12] ... ldr x9,[x12]; rev x9,x9;
  scvtf d25,x9` (`sub_822D92D8`; GCC did not forward it because other guest stores sit between, which it must treat as aliasing).
  In the 320 chain copies the pattern is rare (0.01 % + 0.01 % of statements) but common elsewhere.
- **Transform.** Rewrite the three statements to `f13.f64 = double(int64_t(r10.u64))` (E, keeping the `std` store: the load is
  gone: no `rev`, no store-forward latency) and, as E\*, drop the store when the slot is not read again in the function
  (see CG-20's liveness) and is below `r1` (dead after return). `fctiwz` direction: `r3 = int32(fctiwz(f0))` and the `stfd`
  kept or dropped the same way.
- **Where.** a new `tools/stack_conv.py` (before the copies), reusing `frame_traffic.py`'s statement regexes.
- **Exactness.** E with the store kept; E\* without. **Gain.** 0.05-0.15 ms on the chain; the idiom is 5,192 sites over the game.
  **Conf M.** **Risk** low. **Verify** guard; forms self-check (the conversion forms are the same expressions).

### CG-18  FSEL-PLAIN: `fsel` as `fcmp` + `fcsel` where the mode is known   (E)
- **Pattern.** `GeZeroAnyMode(f7) ? f8 : f0` (1,998 `fsel` sites): `fmov x,d; cmp; ccmp ...` on the bits so it works in either
  mode. In a region where `flush_mode_flow` knows the mode is *off*, `f7 >= 0.0` is the same truth table (NaN false, -0 true) and
  `fcmpe d,#0; fcsel` is 2 instructions.
- **Transform.** `tools/flush_mode_flow.py` rewrites `GeZeroAnyMode(x)` to `(x >= 0.0)` where the mode is proven off (and the
  denormal-negative case is false in both).
- **Exactness.** E (`GeZeroAnyMode`'s doc states "the same truth table as the comparison with the flush mode off"). **Gain.**
  0.03-0.1 ms. **Conf M.** **Risk** none. **Verify** unit test of the two over all classes of double.

### D. Frame, stack and entry traffic

### CG-19  FRAME-ELIDE-ON: the three existing opt-in frame elisions in the copies   (E\*/D)
- **Pattern.** `REX_BACK_CHAIN_ELIDE`, `REX_NV_SAVES_ELIDE`, `REX_LR_MOVES_ELIDE` exist (`tools/frame_traffic.py`,
  `app/CMakeLists.txt:364-366`, all OFF; port 84 saw 8 guard differences from the LR one when the two forms were in different
  TUs). In the chain copies: back-chain stores 0.27 % of statements (282 sites), dead nonvolatile saves 0.12 % (158), LR 0.03 % (32);
  over all hot copies 2,902 / 2,764 / 1,066 text sites.
- **Transform.** Switch the first two on for the Blur build (the bulk shards' own flag `SASRT_HOT_LR_ELIDE` for the third where
  the two forms are in one TU, which the in-place copies now are).
- **Exactness.** E\*: words of a dead frame keep old content instead of a zero / stale LR; invisible to code that reads a frame
  before writing it only if the game has none (the SASRT frame-exact capture passed for LR; the back chain is read by a guest
  stack walk: Blur has none that I know of, check `KeCaptureStackBackTrace` users). **Gain.** 0.1-0.3 ms (0.4 % of statements,
  one store each). **Conf M.** **Risk** low (a capture proves it). **Verify** a frame-exact capture of the intro and a race;
  guard with both forms in one TU.

### CG-20  STACK-FWD: store -> load of the same `r1` slot inside a block   (E / E\*)
- **Pattern.** 3,553 "other" store -> load pairs on `r1` slots within 8 mnemonics in the game, beyond CG-17's; in the chain copies
  0.08 % of statements. GCC does not forward them across the guest stores in between (it must assume a store through
  `base + u32(rX)` may alias `base + u32(r1) + K`).
- **Transform.** In straight-line code (no label, no call, no hook) forward the stored register to the load when every
  store in between is `r1`-relative at a non-overlapping offset or a *stack-derived-pointer-free* store the generator can
  show (VS-13's `r1`-derived analysis, reversed: a store whose base is a register the function never derived from `r1` can
  still alias the stack, so that is NOT assumed: only `r1`-relative stores). Keep the store (E); drop it when the slot is
  provably dead (E\*).
- **Where.** `tools/frame_traffic.py` style pass, or the copy tool. **Exactness.** E / E\*. **Gain.** 0.05-0.15 ms. **Conf M.**
  **Risk** low. **Verify** guard; PC differential run.

### CG-21  VEC-LOCALS-REG: vector locals as registers   (E)
- **Pattern.** `n_8252D4B0.h` loads the function's ~60 vector locals with `PPCVRegister l_vN = ctx.vN` and writes them through
  `(simde__m128i*)l_v118.u8` casts of a union; in the disassembly `0x42fad8-0x42fb6c` the entry is 18 `ldp x,x / stp x,x [sp]`
  pairs: the union is address-taken, so GCC keeps it in memory and copies it whole. Over the 319 chain copies the entry copies
  are 756 pairs (2.4 per copy; 0.2 % of instructions): large only in a few functions (this one, 40 instructions at entry).
- **Transform.** For functions with more than ~8 live-in vector locals, generate `PPCVRegister` accesses as `float32x4_t` /
  `uint8x16_t` locals (`.u8` as `vreinterpretq`), converted only at the call sync.
- **Exactness.** E. **Gain.** 0-0.1 ms (the average function does not pay). **Conf L.** **Risk** low-medium (the macros that take
  `.u8` pointers). Listed because of how visible it is in a disassembly, not for its size.

### CG-22  CTX-STORE-DEDUP: skip a ctx store the slot already holds   (E)
- **Pattern.** `n_8252D4B0.h:666, 699, 726`: three consecutive native calls each store `ctx.r5, r9, r10` again with unchanged
  values. A straight-line "available value" pass (labels reset it; a post-call reload marks the register clean again) found
  303 of 11,229 sync stores redundant (**2.7 %**) in the 320 chain copies.
- **Transform.** Drop those stores.
- **Exactness.** E. **Gain.** 0.01-0.05 ms: **rejected as a project, listed so nobody redoes the census**; it falls out of CG-10
  for free if that pass tracks clean registers.

### E. Memory behaviour of GM (the 45 % of load samples)

### CG-23  PF-USE: prefetch the object behind a freshly loaded pointer   (D / E)
- **Pattern.** F2: GM's load samples are guest-data misses behind pointers (`lwz r3,off(r4)` then loads off `r3`). There is already
  a `REX_PREFETCH_GUEST` after a pointer-chasing loop's next-pointer load (SASRT idea 52, `SASRT_HOT_PREFETCH`, default OFF).
- **Transform.** In the copies of the top GM functions (they are flat, so the *pass*, not a function): where a `lwz` result
  is used as the base of a later load within the same function and at least ~10 statements of other work lie in between
  (the load would hit the same line later), insert `prfm pldl1keep, [base, w]` directly after the `lwz`. A pass only helps
  when the demand load is far enough from the prefetch; at distance 0 it is a redundant instruction. Start from the
  census (CG-40) of which loads miss.
- **Where.** a new `tools/guest_prefetch.py` before the copies, `banjo_hot_prelude.h` macro.
- **Exactness.** E for the values (a prefetch changes nothing the guest sees); D for the policy (cost: one instruction per site,
  and a wasted line if the pointer is null: a prefetch of address 0 + base is harmless in this window).
- **Gain.** 0-1.5 ms on GM (if 10 % of its 12 ms of stall is covered). **Conf L** (the A57 already overlaps independent
  loads out of order; a prefetch pays only for loads the out-of-order window cannot reach). **Risk** low. **Verify** one A/B
  race per batch; keep the `prfm` where the sample share of the next load falls.

### CG-24  LOAD-BATCH: loads past stores with provably distinct addresses   (E)
- **Pattern.** Guest memory is `base + x` for every access, so GCC cannot prove a store and a following load disjoint unless
  they share a base register and constant offsets; it keeps their order and a missing load waits behind an unrelated store.
  A long dependency of misses (`lwz` ; `lwz` ; `lwz`) is serialised behind stores that cannot touch it.
- **Transform.** GCC already disambiguates same-base, disjoint-offset pairs, and a store through one guest register cannot be
  proven disjoint from a load through another (CG-20's note), so the loads themselves cannot move. What can move exactly is the
  *address computation and a `prfm`* of the later load: hoist `add x, base, w, uxtw` and a prefetch of the later load's line
  above the unrelated store, so the miss starts earlier. The benefit is memory-level parallelism, not instruction count; it
  is the cheap half of CG-23.
- **Exactness.** E when only independent address arithmetic moves. **Gain.** 0-0.5 ms. **Conf L**; flagged because it is the only
  codegen lever aimed at F2. **Risk** low. **Verify** guard; PMU count (CG-40) of outstanding misses.

### F. Compiler flags, layout and alignment (E, all need a console run to confirm)

### CG-25  HOT-COLD-SPLIT: `-freorder-blocks-and-partition`   (E)
- **Pattern.** The ELF has **0 `.cold` symbols** across 83k functions; 761 copies total 6.9 MB (108 over 16 KB, 40 over 32 KB,
  `sub_8250A7E8` is 82 KB), the instruction stream of the chain is spread over them (F7: 1.5-2.7 % of the chain on line
  starts alone). With PGO GCC normally splits unexecuted blocks of trained functions into `.text.unlikely`; the flags carry
  `-fprofile-partial-training` (all code not seen in training is optimised for speed, so no block is cold) and the cold helpers are
  split by `[[gnu::cold]]` only.
- **Transform.** Try `-freorder-blocks-and-partition` (and without `-fprofile-partial-training` for the *copies'* shards, whose
  profile is complete for what the race runs) on one bulk shard; look for `.cold` symbols and the `.text.unlikely` size.
- **Where.** `app/CMakeLists.txt` per-source options. **Exactness.** E. **Gain.** 0.1-0.8 ms (I-cache density of the biggest
  copies; the upper end needs the front-end to be a real part of the 45 % load samples, which it is not by F7). **Conf L.**
  **Risk** low (an untrained path becomes slower). **Verify** `nm` count and a race.

### CG-26  NO-TRAPPING-MATH for the float-heavy shards   (E)
- **Pattern.** `-ffp-contract=off -fno-strict-aliasing` are set; GCC's default `-ftrapping-math` stops it speculating or
  if-converting float operations that could trap, e.g. a divide in a `?:` arm, and keeps FP compares as branches. FP exceptions
  are masked on the target, so nothing reads the flags the guest FPSCR does not already track.
- **Transform.** `-fno-trapping-math` (with `-fno-math-errno` for the one `sqrt` call per 2,000 statements: 14 `bl sqrt`/`sqrtf`
  in the whole ELF, 262 inline `fsqrt`) on the shards holding copies; the values are unchanged (`-ffp-contract=off` stays).
- **Exactness.** E on values; the exception flags GCC may no longer raise are not guest-visible (the guest FPSCR flags are set by
  the generator's own code where used). **Gain.** 0-0.4 ms. **Conf L.** **Risk** low. **Verify** disassembly diff of three copies,
  guard, race.

### CG-27  SCHED-PRESSURE: scheduling for the biggest copies   (E)
- **Pattern.** `spill` is 9.6 % (GM) / 11.3 % (JQ0-2) of the instruction slots of the hot functions (weighted static mix, by samples),
  `mov` 10.8 %; the A57 is out of order but a 128-entry window does not cover an 82 KB function's live ranges.
- **Transform.** `-fsched-pressure` (with `-fschedule-insns`) and `-fira-hoist-pressure` on the shards of the ten largest copies
  (`sub_8250A7E8`, `sub_8251E6F0`, `sub_8252E168`, `sub_825123B8`, `sub_825087A0`, `sub_82504F90`, `sub_824F35D8`, `sub_82668520`,
  `sub_826EFA20`, `sub_826803B0`); count spills in the disassembly first.
- **Exactness.** E. **Gain.** 0-0.5 ms. **Conf L**; the reference is the vector listing already at the A57's limits. **Risk** low
  (compile time). **Verify** `objdump` spill count before/after, race.

### CG-28  ALIGN-HOT: alignment of the hot copies' entries and loop heads   (E)
- **Pattern.** The A57 fetches 16-byte-aligned groups; a loop head or a function entry that straddles a line pays an extra fetch
  cycle every iteration; `-ffunction-sections` plus PGO ordering leaves function alignment at the default (4).
- **Transform.** `-falign-functions=32 -falign-loops=16` for the copies' shards (size +1-2 %).
- **Exactness.** E. **Gain.** 0-0.2 ms. **Conf L.** **Risk** low (NRO size). **Verify** race; skip if inside noise.

### G. Hand natives (for the chain's loop-shaped functions)

### CG-29  JQ-HUFFMAN: a native for `sub_829D2E80`, revisited   (E)
- **Pattern.** 1.16 ms on JQ0-2 (149 samples; **not** a machine copy: a generated function that already has a `blur_hand_829D2E80`
  entry whose draft was declined, `hand_jq012.draft.txt:11`: "dependent table lookups and cache misses"). The loop is
  `lsr w3,w0,#21 ; and w5,w3,#0x7fe ; ldrh w5,[x2,w5,uxtw] ; rev16 ; sxth ; tbnz ; ... ldrb w3,[x2,w3,uxtw]` (13 samples at the
  `ldrh`, 11 at the `ldrb`): two dependent table loads per symbol, each with a byte swap in the chain.
- **Transform.** A native with a 64-bit bit buffer and a *host-order mirror of the table* built once per table (cached by table
  address and a hash of its first 8 bytes, invalidated by the guest write cards): the 16-bit entry loads lose `rev16 ; sxth` from
  the dependency chain (3 cycles per symbol of ~14), and symbols that fit the first-level table decode in one load. The
  draft's reasons (misses) stay: the mirror shrinks the footprint by half only if entries are packed.
- **Exactness.** E (the guard compares output and the buffer state). **Gain.** 0.2-0.5 ms on JQ0-2. **Conf L**: declined before
  for a reason I cannot beat from here. **Risk** medium (the mirror's invalidation). **Verify** hand-native gate (docs
  `hand-and-machine-optimising-guide.md` section 3), guard on real streams.

### CG-39  GM-TAIL-NATIVES: three more GM functions with a measurable loop   (E)
- **Pattern.** GM is flat (F2); the hottest PC share inside a GM function (`sub_8227CA08` `0x442830` 10 of 72 samples: an
  8-byte copy loop `ldr x6,[x19,w1,uxtw]` with `dmb ish` between iterations; `sub_826A7858` 63 samples with 8 at
  `0x5a8230`; `sub_82733868` 46 samples) is at most 14 %. Only `sub_8227CA08` has a loop at its top PC.
- **Transform.** `sub_8227CA08`: the `dmb ish` (a guest `lwsync`/`sync` between copied words) as `dmb ishst` where only
  stores are ordered (the guest's own `lwsync` orders everything but store-load; `ishst` is not enough for load-load, so this
  needs the loop's meaning: read the function first), or batch the copy loop's loads before its barrier. The others have no loop.
- **Exactness.** E when the barrier is demonstrably only between stores. **Gain.** 0.1-0.3 ms (the function is 0.27 ms on GM).
  **Conf L.** **Risk** medium (memory ordering: needs a proof by reading the function, not a pattern). **Verify** the guard cannot
  see ordering; a multi-threaded stress is the only test. Honest remark: I would not do this one without a reason beyond 0.27 ms.

### H. Renut render thread and the recording threads

### CG-30  NEWOP-PAGE-CLEAN: skip the state compare when the page is clean   (E)
- **Pattern.** `NewOp` (JQ0-2: 2.26 ms) spends 85 of its 365 samples in one 128-byte block (`0x272660`..`0x2726e8`): the
  `ChunkDiffers` / `FourChunksDiffer` loop (`guest/command_buffers.cpp:722-747`) comparing 2.7 KB of the device's state
  (offsets `0x2780`..`0x5b60` and `0x5b60`..`0x65e0`) against the shadow, sixteen 16-byte loads a side, for every op.
- **Transform.** The store cards already say whether a guest page was written since the last read (they cover the views Renut
  reads: `0xA0000000+`, `0x7F000000+`; the device state lives in the heap, outside them). Add a card for the device-state page(s)
  (the page of the device object, one 4 KB page holds ~1.5 of the 2.7 KB): if neither page's card is zero since the last
  NewOp of this device, the compare is skipped and the shadow is current. A store marks its page *unless* the compare ran
  since: cost on the guest side is one conditional card store per state write (already in the copies for views).
- **Where.** `command_buffers.cpp` (`DifferingChunks` caller), `banjo_hot_prelude.h` (`rex_card_mark_` accepts the device page),
  Renut's card reader.
- **Exactness.** E when the card is precise for every store, including the unaligned page-spanning ones the existing doc
  treats (mark the page in front too). **Gain.** 0.2-0.5 ms on JQ0-2 (85/365 of 2.26 ms = 0.53 ms, less when the state does change).
  **Conf M**; **risk** low-medium (a missed store is a stale draw state: the existing `banjo_renut_store_cards` self-checks
  apply). **Verify** `banjo_renut_store_cards` check mode, capture comparisons (the repo's `tools/ab_*`), race.

### CG-31  RENDER-SMALL-COPY: inline small copies, prefetch the source   (E)
- **Pattern.** The render thread: `memcpy` 1.57 ms, 132 samples, 39 on the first `ldp x6,x7,[x1]` of the 17-32 byte path and
  the 65-144 byte loop heads (F4); GM `memcpy` 0.84 + `memset` 0.06. The source is a line another core wrote last (JQ record
  threads, GM), so the stall is a cross-core transfer from the neighbour's L1/L2.
- **Transform.** (a) Constant-size copies in `renut::gpu` (the op records, the state blocks) as `vld1q`/`vst1q` inline: no call
  and the loads issue before the call's setup. (b) In `Replay`/`FastDrawLocked` issue `prfm pldl1keep` for the *next* op's
  first line while decoding this one (CG-34).
- **Where.** `app/src/renut_engine/gpu/render/*.cpp`. **Exactness.** E. **Gain.** 0.1-0.4 ms on the render thread. **Conf L.**
  **Risk** low. **Verify** race (render-thread ms).

### CG-32  MUTEX-SPIN: short host critical sections without the kernel mutex   (E)
- **Pattern.** `mutexLock` + `mutexUnlock`: GM 0.17 + 0.16 ms, JQ0-2 0.41, render 0.20 + 0.18; `mutexLock` samples sit on the
  `ldaxr/stlxr` retry (contended line) -- 21 chain samples.
- **Transform.** For the Renut locks that guard a few stores (the pending-vertex lists, the record generation, `g_..._lock`s in
  `command_buffers.cpp`) a test-and-test-and-set with a bounded spin (`yield`) before `svcArbitrateLock`.
- **Exactness.** E (no value changes). **Gain.** 0.05-0.2 ms. **Conf L.** **Risk** low; priority inversion on a saturated core
  (CG-01 interacts). **Verify** race.

### CG-33  CONST-UPLOAD-SKIP: record-time compare of constants (the census exists)   (E)
- **Pattern.** `PushFloatConstants` 0.84 ms on the render thread; `draw.cpp:3175` says a census for "a record-time compare of
  constants" was taken and the loop already skips held groups word by word; the remaining cost is the dirty/need scan itself.
- **Transform.** Move the compare to NewOp's side where the guest's constant bytes are hot (the JQ thread has just written them),
  and send the render thread only the dirty group mask.
- **Exactness.** E. **Gain.** 0-0.2 ms on the render thread (moved to JQ, so a net only if the render thread is the bound).
  **Conf L.** **Risk** medium (two threads, a longer-lived mask). Not started without reading the A/B notes of batches 6-7.

### CG-34  REPLAY-STREAM-PF: prefetch the next op in `Replay`   (E)
- **Pattern.** `Replay` 1.68 ms, flat; its input is the op stream the recording threads wrote (cross-core lines).
- **Transform.** `prfm pldl1keep` of op N+1's header (and N+2's if the stream is dense) while op N is decoded.
- **Exactness.** E. **Gain.** 0.05-0.2 ms. **Conf L.** **Risk** none. **Verify** race (render ms).

### CG-35  SHADOW-DIRTY-BITS: one dirty word per 64 chunks from the store cards   (E)
- **Pattern.** `ChangedChunks` compares `kPlainChunks` chunks of the device state against the shadow every op (CG-30's loop);
  `DifferingChunks` already does four chunks at a time.
- **Transform.** Beyond CG-30's whole-page skip: when a page *is* dirty, a sub-page card (64-byte granularity: one bit per chunk
  in a 64-bit word per 4 KB, set by the copies' stores into the state pages only) names the chunks to compare; the other
  chunks are not loaded at all.
- **Exactness.** E when every store marks its chunk (the stores into these pages are few and known: the D3D device wrappers
  in `device.cpp`, the guest's own `SetRenderState` copies). **Gain.** 0.05-0.2 ms. **Conf L.** **Risk** medium (more
  card machinery, more places to miss a store).

### I. Calls and the ABI of the copies (second order)

### CG-36  CALL-ABI-REGS: `ctx` and `base` out of the callee-save traffic   (E)
- **Pattern.** `n_8252D4B0` entry: `stp x20,x21 ; stp x22,x23 ; stp x24,x25 ; stp x26,x27 ; str x28 ; stp x30,x19 ; stp d14,d15`
  = 7 stores at entry and 7 loads at exit for every call that is not inlined, before any statement runs; a guest call has
  the same ~14 instructions in every function with high register pressure.
- **Transform.** For the *closure-external* calls (the sync sites) mark the callee `[[gnu::preserve_most]]`-like: not
  available on GCC AArch64. The workable form is `-fcall-used-x19 .. -fcall-used-x28` on the shards of small leaf-like copies
  that call nothing (their callee-save traffic vanishes; their callers save around the call instead). Only a measurement
  decides, and only for functions under ~100 instructions.
- **Exactness.** E. **Gain.** 0-0.3 ms. **Conf L.** **Risk** medium (an ABI mix across LTO partitions: GCC rejects it at the
  link). Listed because the entry sequence is the one fixed cost every non-inlined call pays.

### CG-38  FAN-IN-INLINE: the tail of tiny callees by call count   (E)
- **Pattern.** The tail of GM (rows 51-400 are 12 ms): many functions 0.1-0.3 ms; `localize_native.py` keeps functions under 40
  instructions *out* of the list unless they have 50 samples (`optimising-functions.md`), correct for hooks but wrong for an
  in-place copy whose callers inline it.
- **Transform.** For in-place copies there is no hook to pay for (port 81): lift the 40-instruction floor for functions with
  >= 3 callers in the closure list and let LTO inline them into the copies of their callers.
- **Exactness.** E. **Gain.** 0.05-0.2 ms. **Conf L.** **Risk** low (size: ~1 KB each). **Verify** guard, NRO size.

### J. Process and measurement

### CG-37  PGO-FRESH: profile coverage after each post_codegen change   (M)
- **Pattern.** A GCC profile is keyed by each function's CFG checksum; a pass that edits statements (fma_single on 2026-11-12,
  vec_cvt_fixed, flush hoists) changes the CFG of the functions it touches, and `-fprofile-use ... -Wno-missing-profile
  -fprofile-partial-training` then silently treats them as untrained. The hot copies are exactly the functions these passes edit.
- **Transform.** After the last codegen change: a PGO-generate build, a race, a PGO-use build (the recorded order in
  `optimising-functions.md`), and a check that prints how many of the 761 copies have no matching profile (compile with
  `-Wcoverage-mismatch` as a count, not an error).
- **Exactness.** M. **Gain.** Protects every other gain; unknown size when stale. **Conf H.** **Risk** none.

### CG-40  STALL-ATTRIBUTION: attribute the sample to the missing load, not the retiring PC   (M)
- **Pattern.** The profiler is a retire-PC sampler (F2's 44.6 % "on loads" is where stalls retire); it cannot say which loads
  miss, how often, or whether the misses are L1, L2 or DRAM; CG-23, CG-24 and CG-05 are blind without that.
- **Transform.** Add to the sampler a PMU read per sample window on GM and JQ0-2 (`PMCCNTR_EL0`, `L1D_CACHE_REFILL`,
  `L2D_CACHE_REFILL`, `L1D_TLB_REFILL`, `BR_MIS_PRED` via the Horizon PMU access if the title can enable it, `svcReadWriteRegister`
  is not allowed; alternatively `PMUSERENR_EL0` via a sysmodule) and log counts per window; failing that, a *two-build*
  differential: the same function with its loads replaced by `prfm`+`ldr` to see the shift.
- **Where.** the profiler (`rex_profile`), `tools/`. **Exactness.** M. **Gain.** None itself; decides the memory ideas. **Conf
  M** (the PMU access from a title is the unknown). **Risk** none.

## 4. Considered and rejected (so nobody repeats them)

- **Per-statement plumbing of `Stopped()`, `Note()`, `Hit()`.** `constexpr` no-ops in the Fast form (`banjo_hot_common.h`).
- **Barriers.** 1,791 `dmb ish` sites but 0.4 % of GM and 0.1 % of JQ0-2 samples; `sub_8227CA08` is CG-39's only case.
- **Soft TLS (`-mtp=soft`).** 1,242 `__aarch64_read_tp` calls; the callers are Vulkan/Plume pipeline setup, `XObject::WaitMultiple`
  and the audio worker, none a chain hot path.
- **GOT / PIE cost.** `.got` is 9 KB; accesses are `adrp+add`.
- **Erratum nops.** None in the 20,466-instruction `sub_8250A7E8` or anywhere sampled.
- **Entry dead-in loads.** Of 12,783 scalar `l_rN = ctx.rN` entry loads only 592 (4.6 %) are written before any read before any branch,
  and GCC already removes dead loads; of 7,064 vector entry loads 23. Entry ctx->stack copies are 0.2 % of the chain's
  instructions (CG-21).
- **Redundant ctx stores** (CG-22): 2.7 % of 11,229; subsumed by CG-10.
- **`REX_PHYS_HOST_OFFSET`.** 0 on the Switch (a compare on Windows only).
- **Card marks in the copies.** Already check-before-write and page-filtered (`banjo_hot_prelude.h:30-43`: only pages Renut
  reads, a zero card is not rewritten); 1.31 % of statements and the part that remains is CG-11.
- **`GeZeroAnyMode`.** Already integer-only (two compares); only the known-mode form (CG-18) is cheaper.
- **`sqrt` / `errno`.** 14 library calls in the whole ELF, 262 inline `fsqrt`.
- **Byte-swapped loads feeding zero or equality tests.** GCC folds `bswap(x) == C` into `x == bswap(C)` itself.
- **Frame-rate gains from JQ3/4/XMA work.** By the F1 arithmetic they would matter if the cores were the limit; the user's
  measurement says they are not: that is CG-03's question, not another saving there.
- **Sequential instruction-fetch stalls as a big lever.** F7: at most 1.5-2.7 % of the chain (CG-25 and CG-28 are priced
  accordingly).

## 5. The order I would do them in

1. **CG-03 HANDOFF-TRACE and CG-40 STALL-ATTRIBUTION** (measurements, switchable, no behaviour change): together they say whether
   the frame is waiting for cores, for hand-offs or for memory, and so which of 01, 02, 04, 05, 23 is worth anything.
2. **CG-01 CHAIN-PRIO and CG-02 CORE-MAP as cvars** (one A/B each; a race apiece): the only items that can move the frame by
   milliseconds; if CG-03 shows no run-queue wait, drop them and 04.
3. **CG-37 PGO-FRESH** is a process rule for every step after: do not judge a codegen change on a stale profile.
4. **The exact, mechanical ones with a census behind them**, in one batch: CG-06 (needs a PC census run), CG-16 (two lines, an
   exhaustive test), CG-14/15 (one pass), CG-10 (one analysis), CG-11, CG-09, CG-18. Then CG-17/20/19 (the stack ones; CG-19 needs the user's
   OK on dead frame words).
5. **CG-13 FPCR-ONE-MODE** as a user decision once the census counter has run a race: it makes CG-14, 15, 16 and 18
   unnecessary, so the decision comes before (or instead of) them if the denormal count is ~0.
6. **CG-30 NEWOP-PAGE-CLEAN** (JQ side), then the flags in one A/B each (CG-25, 26, 27), then CG-23/24 only with CG-40's data.
7. **The hand natives (CG-29, CG-39) last**: declined once, small, and the hardest to verify.

The ideas that need a console run to price (01, 02, 04, 05, 25-28, 23) are cvars or build flags: none changes a guest
result, so a race A/B decides each. The ones I trust most are CG-16 (proved), CG-06 (the machinery exists), CG-10 (a standard
liveness) and CG-03 (it cannot hurt). The ones I trust least are the two memory ideas (CG-23, CG-24): they are aimed at
the 45 % that is the largest single share of GM, and I could not see from retire-PC samples which loads miss.

## 6. What batch 3 did with them (2026-11-12, console races of 240 s, reports 10-27 of rex_profile.log)

- **Done, exact:** CG-06 (570 promoted sites from a PC census, 75 % of 848 M indirect calls), CG-14 (619 arithmetic
  statements and 926 compares on proven singles), CG-16 (one range test), and the `dcbz` guard fix found on the way
  (vec_batch1.h). The forms self-check covers the new forms: 44,493 of 44,493 on the console, 43,893 of 43,893 on x86.
- **CG-37 confirmed, the hard way:** batch 3 on the morning's profile raced 21.00 fps, on its own fresh profile 23.84
  (the fmadds build before it: 22.51). A codegen change judged on a stale profile is judged wrong.
- **CG-01 rejected (measured):** Game Master and JQ0-2 at 0x3A above JQ3/4 (`switch_thread_priorities`): 23.47 against
  23.84. With the four cores at 377-394 % of 400 the frame follows the CPU a frame, not who runs first.
- **CG-26 rejected (audit):** already rejected in this tree (app/CMakeLists.txt, sa2-nx's audit): GCC may move a float
  operation across the copies' `msr fpcr` mode switches, so it is not exact.
- **CG-11 dropped (census):** of the copies' 31,958 D-form card stores none is in a run of two or more on one base
  between synchronisation points, so a flush at a call has at most one pending card and skipping it only moves the mark.
- **CG-13** stays the user's decision (asked; not built).
- **The user's two extra items:** value profiling (`-fprofile-values`) crashes the training NRO in GCC's value
  profilers on devkitA64 (four fixes in app/src/banjo_pgo.cpp, still corrupting at the first dump): parked,
  BANJO_PGO_VALUES OFF. `-fipa-reorder-for-locality` (BANJO_SWITCH_REORDER_LOCALITY): 24.56 fps against 24.77 with the
  same toml, the NRO 2.3 MB larger; no gain, and GCC 16.1's LTO dropped the shader cache's top-level asm with it
  (those two generated files now build without LTO under that option).
- **New, found on the way:** the XMA frame cache's room (`banjo_xma_frame_cache_mb` 24 -> 256: 26 -> 65 % of the
  frames answered, the XMA Decoder 15.2 -> 10.9 ms a frame, 23.84 -> 24.77 fps; shipped), and from sasrt-nx's finding,
  tools/leaf_dead_stores.py counted app/indirect_promotions.txt and the hand natives' *.draft.txt as naming 216 functions
  (133 in the race, 32.5 ms a frame of CPU): their copies wrote back every volatile register at a return. Fixed in the
  tool; it reaches the copies with the next codegen (batch 4, with the rebase).

## 7. CG-13 built as a switch (2026-11-12 evening; the user: "build CG 13 and do that process please ... it probably should be off by default")

- **The switch.** CMake `BLUR_FPCR_ONE_MODE` (OFF) defines `REX_FPCR_ONE_MODE`: rex/ppc/context.h keeps the flush mode on
  for all guest code (InitHost sets it for a guest thread; any other thread's first switch on writes it once; every
  switch off is nothing; the generated pch's REX_FLUSH_KNOWN_* / REX_FLUSH_HOIST_KNOWN_* are nothing), and
  rex/ppc/diet_ops.h's any-mode forms are the plain operations. `GeZeroAnyMode` (fsel's test on the bits) stays: it is as
  cheap as the plain compare and exact.
- **The census (PC, game code, `banjo_diag_runtime_census`).** Two counts of the scalar results the switch would change:
  the any-mode helpers' cold paths (scalar results in VMX code that met a denormal; counted since sa2-nx's round 7), and,
  new, the FPU-mode regions whose x86 sticky flags (MXCSR DE / UE) show a denormal operand or an underflow (a lower
  bound: x86 raises UE only on an inexact tiny result). Both name their code addresses in a table of their own:
  `[census] denormal sites (CG-13 ...)`, read by `tools/census_sites.py --denormal`.
- **What is known already.** census_flush1 (this morning's PC race, the cold paths alone): 2,500-3,800 a second in the
  race, 0-900 in the menus; the FPU-mode regions are counted from the next PC run on.
