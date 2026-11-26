# GitHub issue #9: a crash while saving after a race (1.0.1), and the save "corrupted"

The report (2026-11-26): Switch Lite, 1.0.1, no overclock. The game closed after the second race of the second stage;
at the next start the game said the save was corrupted and would be recreated. Its `rex_crash.log` held three reports:

1. A `std::bad_alloc` with no build line: 1.0.0's (its code span 0x3E90000 is 1.0.0's NRO exactly), the 11-22
   race-end crash that 1.0.1 fixed (ae5e607a, sbrk under malloc's lock).
2. **1.0.1's** (build `8536019be0+changes`): a data abort in `CheckHeap` (blur_heap_watch) called from
   `operator new[]` ← `PosixFileHandle::Bounce` ← `Write` ← `HostPathFile::WriteSync` ← `NtWriteFile` on a game
   thread. The address was in a heap region with no permission at all (`type 0x5 permissions 0x0`, 8 MB + 32 KB):
   one of the guest memory's chunks, taken from newlib and mapped away into the guest window.
3. The exception handler's own abort after report 2.

So: newlib could not give the thread its first 4 MB host buffer (`filesystem_bounce_piece_mb`), and the heap watch,
gathering its report, walked into a mapped-away chunk and faulted before its mend. The save's container had been
recreated empty a moment before by `XamContentCreate`'s CREATE_ALWAYS (the old one deleted), so the save was lost.

## Why the heap ran out: a session of races

Every earlier console test was one race from boot. On 2026-11-26 a session script (scratch_scripts_1120:
multi_session.sh, multi_rotate.sh, multi_drive.py; the results' Next, then the Options menu's Continue, which saves the
career, then the next event of Proving Grounds: Barcelona Oval, Brighton, Amboy in turn) measured the memory reports
(mem_series.py). 1.0.1, handheld, 720 lines, five races in 32 minutes:

| | first race | fifth race |
|---|---|---|
| the heap's room (never handed out) | 1210 MB | 263 MB, still falling |
| newlib in use | 1355 MB | 1713 MB |
| GPU region (at the heap's top) | 413 MB | 1069 MB peak, 48 MB given back in all |
| XMA frame cache | 1 MB | 178 MB, 0 evicted |

Half of newlib's growth was the XMA frame cache: Blur's `banjo_xma_frame_cache_mb` was 384 (the 2026-11-16 CPU
batch, measured on single races from the start, "~600 MB still free at the race's end"), and nothing made it give
memory back. What it frees inside newlib never reaches the GPU region either (the region grows only into newlib's
unused top). The GPU region keeps its peak (its free space is in the middle; only its free bottom can go back).

At 1080 lines (handheld, set in the toml) the same session on 1.0.2 lasted seven races and ended in a GPU fault once
the GPU region was full (see "Open" below).

## 1.0.2's changes

- **Heap watch** (app/src/blur_small_alloc.cpp): every read and write of heap memory is asked of the kernel first
  (`svcQueryMemory`); the walk ends at memory that cannot be read and says so. A failure is reported and the top
  chunk mended, never a fault.
- **Host buffer** (sdk/src/core/filesystem_posix.cpp, every port): asked for without a throw, halving from 4 MB to
  64 KB; with none, a 256 KB reserve serves one transfer at a time.
- **Atomic saves** (sdk content_manager / xam_content, every port): CREATE_ALWAYS and TRUNCATE_EXISTING set the old
  save aside (`renut/<profile>/41560845/Rewriting/<type>/<name>`) until the new one is closed; one still set aside at
  the next start (the game closed mid-write) is put back before the game lists, looks for or opens its saves.
- **Save backups** for Blur (`content_backup_root = "/switch/blur-nx/save_backup"`): each changed save with data is
  copied to `save_backup/<save>/current`, the copy before it to `previous`. Reads and empty saves move nothing.
  (Known gap: the folder is named by the save's name, not the profile.)
- **XMA frame cache**: `rex::audio::LimitXmaFrameCache`; the memory governor (sasrt_memory_governor, surfaces.cpp)
  holds it at 50 % while low, 25 % while critical, whole again only with twice both low marks free. Blur's cap
  384 -> 192 MB.
- The RAM-1600 hint in every port's crash report no longer names Mass Effect's key and toml.

## Tests (console, 2026-11-26)

- **Interrupted save** (1.0.2b): the live `AMAXSAVE/savegame` emptied and the player's save put in `Rewriting/`, then a
  boot: "its last write never finished (the game closed while saving; it held 1 file(s), 0 B) ... the save before it
  is back -> 2192 B"; no corruption message (Career 3/450 as before); the game's own save then went through the set-aside
  and was dropped at its close; the save afterwards byte-identical to the player's (md5 3c7fc4c2).
- **Ten races** (1.0.2b, handheld 720, the player's settings with the log on, 62 minutes): no crash; the heap's room
  742 -> 256 MB over five races, then flat: 177 -> 154 MB over races 6-10 (+8 MB taken in 30 minutes); GPU region
  peak 1110 MB; the frame cache cut to 96 MB at race 6 and 48 MB at race 9, still 95.5 % of the frames answered.
  Run folders: C:/blur-nx-test/console_runs/1126_multi_c, logs in out/console_runs/1126_multi_c.

## Open

At 1080 lines handheld (1.0.2 without the cache change), race 7: textures could no longer be made
("texture 1920x1080x1 format 10: no memory for 17694720 bytes" x16, "Resolve destination ...: target creation failed"
x16), the governor stepped the render scale down to 1x, two depth targets grew (rr6_depth_covers_colour), and 50 ms
later the GPU faulted: GrErrorSwNotify, class B197 method 0x0FE8 (ZETA_FORMAT) data 0x00100000, every later
submission refused (0xD5C, Timeout), deko3d's abort (0x367). Heap 206 MB free, GPU region 1192 MB. A depth image with
no valid format reached a bind; plume's `createTexture` returns a texture with `valid = false` when its memory cannot
be had, and the renderer's target paths check `TextureCreated`, so the stale one was not found by reading. Sent to RR6
for the base (it needs a repro with a trace at bind time).
