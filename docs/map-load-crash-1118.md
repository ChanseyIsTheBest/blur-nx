# The map-load crashes (2026-11-17 to 11-18)

The user (11-17 22:58): "work independently for 12 hours to fix the crashes". Two console crashes had happened while
loading maps: the split-screen Amboy race at 22:25, and a single-player map at 22:38. The user named a reproducer:
Proving Grounds' third event, Destruction at Brighton, crashes without a shader cache.

## The signature

- Renut's render thread: Video::KickLocked -> plume Deko3DCommandQueue::executeCommandLists
- -> dkQueueSubmitCommands (or dkQueueFlush)
- -> dk::detail::Queue::appendGpfifoEntries (or Queue::flush)
- -> dk::detail::RaiseError(DkResult_Fail) -> libnx diagAbortWithResult
- Atmosphère: "User Break", 2359-0001. rex_crash.log misses it (svcBreak).
- The crash came 16-22 s into the event's load, on every build (b3, b3menu3, b3menu4, b3diag), with or without the
  cache.

## What it was not

- **Stale L2 shader code:** the GPU prefetches 1 KiB past a program, and the Shader invalidate doesn't drop L2.
  26c0dae7 fixed that hazard, and the crash stayed. The commit is kept: it is defensive and costs a little code memory
  and an L2 invalidate after each code load.
- **A GPU fault:** 003229a1's diagnostic waited for every submission and logged nothing. Its option,
  banjo_deko3d_diag_gpu_fault, stays off.
- **Toml settings:** boost, fast first compiles, async variants, the cache, batch 3 and a2c were all ruled out.

## The cause (from deko3d 0.5.0's and libnx's disassembly, then the console's log)

- **deko3d** raises the error only when libnx's kickoff fails *and* Queue::checkError() finds no channel error. So
  nvservices refused the submission; the GPU had not faulted.
- **libnx**'s nvGpuChannelKickoff retries Busy (every 100 ms) and returns any other refusal.
- **The ring:** libnx gives the channel a GPFIFO ring of 2048 entries and kicks off when 2040 to 2048 are pending.
- **The refusal:** at Brighton's load, one guest frame (4717) creates the level's textures and buffers, and the kick
  holds about 5000 gpfifo entries. Compute dispatches cost about two each: deko3d's ComputeQueue::dispatch writes the
  queue's own entries. libnx sent 2041 entries, then 2040 more 15 ms later, while the GPU was still on the first.
  nvservices answered **0xD5C, LibnxNvidiaError_Timeout**: no room in the ring.

## The fix (dcdf68a6)

plume wraps libnx's KickoffPb ioctl (`-Wl,--wrap=nvioctlChannel_KickoffPb`, an INTERFACE link option in plume's
CMakeLists.txt; `__wrap_nvioctlChannel_KickoffPb` at the end of plume_deko3d.cpp).

- **Logging:** the largest submissions are logged, from 64 entries up: "nvservices: a GPU submission of N gpfifo
  entries, the most so far".
- **A refusal** is logged with its result, entries, words and memory figures ("nvservices REFUSED a GPU
  submission"). The submission is then sent again in halves, in order, and halves again if a half is refused.
- **The fence:** the first half carries the submission's syncpoint increments (flag 0x100, fence.value). nvservices'
  expected value then runs ahead of the GPU until the second half's increment commands run. The second half is a
  plain kickoff.
- **Busy** inside the halves is waited out there, because libnx's retry would resend everything. Busy at the top
  level goes back to libnx as before.
- **Checked in the ELF:** nvGpuChannelKickoff calls `__wrap_nvioctlChannel_KickoffPb`, and the wrapper calls the real
  ioctl.

## Console runs (build 2026-11-18_b3kick, main dcdf68a6)

| Run | Cache | Log | Result |
|---|---|---|---|
| pg3_kick1 | the card's (mixed) | 1 refusal: 2040 entries, Timeout; halves in | loaded; the event ran to its results |
| pg3_kick2 | none (1304 compiles, 220 blocking) | 2 refusals: 2040, then 950 with +3 increments; halves in | loaded; ran to its results; no fence hang |
| pg3_ship (shipping toml) | the user's | 2 refusals: 2040, then its first half of 1020; quarters in | loaded; ran to its results |
| pg2_ship (shipping toml): race 2, Amboy, single player | the user's | none; the largest submission 731 entries | loaded; raced 2 minutes |

Logs: C:/blur-nx-test/kick1, kick2, ship1, ship2. Screens: C:/blur-nx-test/console_runs/pg3_kick1, pg3_kick2,
pg3_ship, pg2_ship. Each run ended with the user's cache put back as it was.

## Not tested

The split-screen Amboy race needs a second controller, which the console tools can't drive. Its crash had the same
signature, through the same path. Single-player Amboy peaks at 731 entries; two views load more.
