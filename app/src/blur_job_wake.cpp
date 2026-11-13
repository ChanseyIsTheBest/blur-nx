// blur-nx (2026-11-13, #34 of docs/cpu-near-exact-50-1113.md's second wave; timing only, the guest sees the same
// results): Blur's job system reports progress to the SDK's yield backoff (RexProgressBump, threading_posix.cpp).
//
// The job workers (sub_8227CF60's loop) and the threads waiting for a job group (sub_822862B0) poll: they scan the
// queues (sub_8227CC58: a job whose dependencies are not done is not taken) and, finding nothing they can run, yield
// (sub_829BB640, NtYieldExecution). On the console the backoff turns a run of such yields into sleeps of "100 us" that
// last ~1 ms, about 1,700 a second (the census of 2026-11-13). What lets a poller go on is a job finishing (its group's
// counters at +48 / +52 of the 56-byte group records, *(0x82C84D38) + 456, the global one at +472) or a job being
// pushed:
//   - sub_8227C5F8 runs right after each job (sub_8227CF60 calls it once the job's function returns; it releases the
//     dependents marked -2), just before the counters are decremented: a woken poller rescans after the decrements
//     (they are ~20 instructions on; its wake takes microseconds);
//   - sub_8227E098 pushes a job (it increments +48 with lwarx / stwcx.).
// Both are called through these hooks; each runs the recompiled function unchanged, then bumps the progress word.
// With switch_yield_wake_on_progress off (the default) the bump wakes nobody: the backoff sleeps as before. Both were
// machine copies: app/src/cpu/hot/localized_bulk.txt keeps them out ("# out:" lines; ~100 console samples together).
#include <rex/hook.h>
#include <rex/ppc.h>

extern "C" void RexProgressBump();

REX_EXTERN(__imp__sub_8227C5F8);
REX_EXTERN(__imp__sub_8227E098);

REX_HOOK_RAW(sub_8227C5F8) {
  __imp__sub_8227C5F8(ctx, base);
  RexProgressBump();
}

REX_HOOK_RAW(sub_8227E098) {
  __imp__sub_8227E098(ctx, base);
  RexProgressBump();
}
