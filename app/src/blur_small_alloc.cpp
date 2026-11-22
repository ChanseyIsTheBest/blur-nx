// blur-nx (2026-11-17, #220 of docs/cpu-ideas-196-225-1116.md; the 24-hour run): a per-thread small-block front for the
// host's C++ allocations (Switch only). The console's sampled stacks (15 ms build) put the host allocator on the frame
// chain: operator new -> _memalign_r -> _malloc_r on Game Master (Renut's D3D hooks: BeginVertices, RunCommandBuffer),
// on the job workers (BeginCommandBuffer, NewBuffer), on the render thread (the replay closures) and on the runtime's
// timer thread, every call through newlib's one recursive lock (rmutexLock: 237 sampled stacks).
//
// The global operator new / delete (the standard's replaceable forms; the aligned ones stay the library's) take
// requests up to 512 bytes from ten size classes carved out of one arena, each thread keeping its own free lists: no
// lock on the way in or out except when a list is refilled or handed back (32 blocks at a time). Everything else, and
// everything allocated before the arena exists, is the system allocator's: delete tells the two apart by the arena's
// address range, which is exact. The arena (blur_small_alloc_mb: 32 MB in the base, 0: off) is taken once, after setup
// (RenutApp::OnPostSetup: blur::small_alloc::Enable); when it is used up the system allocator answers again.
//
// Switch only: the NRO is one linked image, so every new and delete in it are these. The PC build's runtime is a DLL
// with its own operator delete: a block allocated here and released there would reach the CRT's free().
//
// What it changes: only where small C++ objects live and how fast they come and go. A block of the arena handed to
// free() instead of delete would be a bug in the caller (C++ forbids it), which the system allocator tolerated.

#include <rex/cvar.h>
#include <rex/logging.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <new>

// renut-ultimate (2026-11-17, port 151; the user: "I want small block allocator on"): 32 MB by default, Blur's size
// (Blur's console: 30.56 fps against 29.88 without it). The arena comes out of the game's heap; a title short of heap
// sets a smaller size or 0 in its toml.
REXCVAR_DEFINE_INT32(blur_small_alloc_mb, 32, "Blur/Enhancements",
                     "#220 (Switch): an arena of this many MB for the host's small C++ allocations (up to 512 bytes), "
                     "free lists per thread instead of the system allocator's lock (0: off, the system allocator for all)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// blur-nx (2026-11-23, the race-end crash of 2026-11-22: std::bad_alloc on the XMA thread, newlib's malloc null for a
// few KB with ~800 MB never handed out). devkitA64's newlib (mallocr.c) returns null for a request that fits only when
// its top chunk (the free space up to the break) cannot serve it and cannot grow, and in a sound heap the top chunk
// ends at the break exactly (malloc_extend_top, malloc_trim and every block carved from it keep top + its size ==
// sbrk(0)). A write over the top chunk's size word (an overrun of the last block carved before it, a stale pointer)
// breaks that, and then the next request the top chunk serves fails, and every one after it. With blur_heap_watch
// (Switch): the blocks operator new takes from newlib go into a ring with their callers; the top chunk is checked once
// a game frame (OnGameFrame: three loads, no lock unless they disagree) and whenever malloc returns null to operator
// new. A broken one is reported on stderr (rex_stderr.log with logging on; the crash report's stderr lines with it off):
// its words, the bytes around it, the ring's blocks just below it with their callers (image offsets: addr2line with the
// build's ELF) and the chunks from there up to it. Then its size is set back to the break (the memory between is the
// top chunk's own: no block lies there) and the request is tried again, so the game goes on. A real shortage (the top
// chunk sound) is reported the same way and throws as before.
REXCVAR_DEFINE_BOOL(blur_heap_watch, true, "Blur/Debug",
                    "Switch: newlib's heap watched (the 2026-11-22 race-end bad_alloc): the top chunk checked every game "
                    "frame and when malloc fails, a broken one reported on stderr with the blocks below it and mended")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

#if defined(__SWITCH__)

#include <malloc.h>
#include <switch.h>
#include <unistd.h>

#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <iterator>

// newlib's malloc state (devkitA64 libc.a, mallocr.c): the bins (the third word is the top chunk), the totals, the
// lock; libnx's heap bounds; the image's start (the crash report's base for image offsets).
extern "C" {
extern void* __malloc_av_[];
extern struct mallinfo __malloc_current_mallinfo;
extern char* __malloc_sbrk_base;
extern size_t __malloc_max_sbrked_mem;
extern char *fake_heap_start, *fake_heap_end;
extern char _start;
}

namespace {

constexpr uint32_t kClasses = 10;
constexpr uint32_t kSize[kClasses] = {16, 32, 48, 64, 96, 128, 192, 256, 384, 512};
constexpr size_t kMaxSmall = 512;
constexpr uint32_t kBatch = 32;       // blocks moved between a thread and the global lists at a time
constexpr uint32_t kKeep = 128;       // above this many free blocks of a class a thread hands half back
constexpr size_t kPiece = 64 * 1024;  // the arena is carved in pieces of this size, one class each
constexpr size_t kMaxArena = size_t(1024) << 20;

inline uint32_t ClassOf(size_t n) {  // n in 1..512
  if (n <= 64) return uint32_t((n - 1) >> 4);  // 16, 32, 48, 64
  if (n <= 128) return n <= 96 ? 4u : 5u;
  if (n <= 256) return n <= 192 ? 6u : 7u;
  return n <= 384 ? 8u : 9u;
}

struct Node {
  Node* next;
};

// The arena [g_begin, g_end): set once by Enable (g_end first, then g_begin with release).
std::atomic<uint8_t*> g_begin{nullptr};
std::atomic<uint8_t*> g_end{nullptr};
// Each piece's class, written under the global lock when the piece is carved, before any of its blocks is handed out
// (a thread that deletes a block got it through the program's own synchronisation with the one that allocated it).
uint8_t g_piece_class[kMaxArena / kPiece];

struct Global {
  std::mutex mutex;
  Node* free[kClasses] = {};
  uint8_t* carve = nullptr;          // the next uncarved piece
  uint8_t* cursor[kClasses] = {};    // the current piece of each class: its next block and its end
  uint8_t* limit[kClasses] = {};
  uint64_t exhausted = 0;
};
Global& G() {
  static Global g;  // a function-local static: ready at the first new (static constructors included), never destroyed
  return g;
}

struct Cache {
  Node* free[kClasses] = {};
  uint32_t count[kClasses] = {};
  ~Cache();
};
thread_local Cache t_cache;

// Up to kBatch blocks of class c into the thread's list; the caller holds the global lock.
void Refill(Global& g, Cache& tc, uint32_t c) {
  uint32_t got = 0;
  while (got < kBatch && g.free[c]) {
    Node* n = g.free[c];
    g.free[c] = n->next;
    n->next = tc.free[c];
    tc.free[c] = n;
    ++got;
  }
  const uint32_t size = kSize[c];
  uint8_t* const begin = g_begin.load(std::memory_order_relaxed);
  uint8_t* const end = g_end.load(std::memory_order_relaxed);
  while (got < kBatch) {
    if (!g.cursor[c] || g.cursor[c] + size > g.limit[c]) {
      if (!g.carve || g.carve + kPiece > end) {
        ++g.exhausted;
        break;
      }
      g_piece_class[size_t(g.carve - begin) / kPiece] = uint8_t(c);
      g.cursor[c] = g.carve;
      g.limit[c] = g.carve + kPiece;
      g.carve += kPiece;
    }
    Node* n = reinterpret_cast<Node*>(g.cursor[c]);
    g.cursor[c] += size;
    n->next = tc.free[c];
    tc.free[c] = n;
    ++got;
  }
  tc.count[c] += got;
}

// The thread's blocks of class c down to 'keep', back to the global list.
void Release(Cache& tc, uint32_t c, uint32_t keep) {
  Global& g = G();
  std::lock_guard lock(g.mutex);
  while (tc.count[c] > keep) {
    Node* n = tc.free[c];
    tc.free[c] = n->next;
    --tc.count[c];
    n->next = g.free[c];
    g.free[c] = n;
  }
}

Cache::~Cache() {
  for (uint32_t c = 0; c < kClasses; ++c)
    if (count[c]) Release(*this, c, 0);
}

[[gnu::always_inline]] inline void* SmallNew(size_t n) {
  if (n - 1 < kMaxSmall && g_begin.load(std::memory_order_acquire)) [[likely]] {
    const uint32_t c = ClassOf(n);
    Cache& tc = t_cache;
    if (!tc.free[c]) [[unlikely]] {
      Global& g = G();
      std::lock_guard lock(g.mutex);
      Refill(g, tc, c);
    }
    if (Node* blk = tc.free[c]) [[likely]] {
      tc.free[c] = blk->next;
      --tc.count[c];
      return blk;
    }
  }
  return nullptr;
}

[[gnu::always_inline]] inline bool SmallDelete(void* p) {
  uint8_t* const b = g_begin.load(std::memory_order_relaxed);
  uint8_t* const q = static_cast<uint8_t*>(p);
  if (!b || q < b || q >= g_end.load(std::memory_order_relaxed)) return false;
  const uint32_t c = g_piece_class[size_t(q - b) / kPiece];
  Cache& tc = t_cache;
  Node* n = static_cast<Node*>(p);
  n->next = tc.free[c];
  tc.free[c] = n;
  if (++tc.count[c] > kKeep) [[unlikely]] Release(tc, c, kKeep / 2);
  return true;
}

// --- the heap watch (blur_heap_watch, see above) ---

std::atomic<bool> g_watch{false};  // Enable: blur_heap_watch

// The blocks operator new took from newlib, the latest kRing (written without a lock: a slot two threads write at
// once, after the ring went round, is only a wrong line in a report).
struct BigBlock {
  uintptr_t ptr, caller;
  uint32_t size, seq;
};
constexpr uint32_t kRing = 4096;
BigBlock g_ring[kRing];
std::atomic<uint32_t> g_ring_next{0};

inline void Remember(void* p, size_t n, void* caller) {
  const uint32_t seq = g_ring_next.fetch_add(1, std::memory_order_relaxed);
  BigBlock& b = g_ring[seq & (kRing - 1)];
  b.ptr = reinterpret_cast<uintptr_t>(p);
  b.caller = reinterpret_cast<uintptr_t>(caller);
  b.size = uint32_t(std::min<size_t>(n, UINT32_MAX));
  b.seq = seq;
}

constexpr uintptr_t kMinChunk = 32;  // newlib's MINSIZE on aarch64
constexpr uintptr_t kPageSize = 0x1000;

struct TopChunk {
  uintptr_t top = 0, word = 0, brk = 0;
  bool in_heap = false;  // a 16-aligned address inside libnx's heap
  bool sound() const { return in_heap && (word & 1) && top + (word & ~uintptr_t(3)) == brk; }
};

TopChunk ReadTop() {  // under malloc's lock, or racy (the frame's look first)
  TopChunk t;
  void* volatile* const av = __malloc_av_;
  t.top = reinterpret_cast<uintptr_t>(av[2]);
  t.brk = reinterpret_cast<uintptr_t>(sbrk(0));
  const uintptr_t lo = reinterpret_cast<uintptr_t>(fake_heap_start), hi = reinterpret_cast<uintptr_t>(fake_heap_end);
  t.in_heap = t.top >= lo && t.top + 16 <= hi && !(t.top & 15);
  t.word = t.in_heap ? *reinterpret_cast<const volatile uintptr_t*>(t.top + 8) : 0;
  return t;
}

// What a report says, gathered under malloc's lock and written after it (stderr has a lock of its own).
struct Evidence {
  const char* what = "";
  size_t want = 0;
  uint64_t thread = 0, frame = 0;
  TopChunk before, after;
  bool mended = false;
  const char* not_mended = "";
  uintptr_t heap_lo = 0, heap_hi = 0, sbrk_base = 0;
  size_t taken = 0, peak = 0;
  uintptr_t below_from = 0;
  uint8_t below[256] = {};  // [top - 256, top)
  uint8_t at[64] = {};      // [top, top + 64)
  BigBlock near[12] = {};   // the ring's blocks in the 64 KB below the top chunk, the highest first
  BigBlock largest[4] = {};  // the ring's largest blocks (a shortage: who took the heap)
  uint32_t near_n = 0, ring_next = 0;
  struct Chunk {
    uintptr_t at, word;
    uint8_t head[16];
  } walk[24] = {};  // newlib's chunks from the highest of those up to the top chunk
  uint32_t walk_n = 0;
};
Evidence g_evidence;
std::atomic_flag g_reporting = ATOMIC_FLAG_INIT;
std::atomic<uint32_t> g_reports{0};
std::atomic<uint64_t> g_frames{0};

// Under malloc's lock.
void GatherLocked(Evidence& e, const TopChunk& t) {
  e.before = t;
  e.heap_lo = reinterpret_cast<uintptr_t>(fake_heap_start);
  e.heap_hi = reinterpret_cast<uintptr_t>(fake_heap_end);
  e.sbrk_base = reinterpret_cast<uintptr_t>(__malloc_sbrk_base);
  e.taken = __malloc_current_mallinfo.arena;
  e.peak = __malloc_max_sbrked_mem;
  e.ring_next = g_ring_next.load(std::memory_order_relaxed);
  e.near_n = e.walk_n = 0;
  e.below_from = 0;
  for (BigBlock& b : e.largest) b = BigBlock{};
  for (uint32_t i = 0; i < kRing; ++i) {
    const BigBlock b = g_ring[i];
    uint32_t k = uint32_t(std::size(e.largest));
    while (k > 0 && e.largest[k - 1].size < b.size) --k;
    if (k == std::size(e.largest)) continue;
    for (uint32_t j = uint32_t(std::size(e.largest)) - 1; j > k; --j) e.largest[j] = e.largest[j - 1];
    e.largest[k] = b;
  }
  if (!t.in_heap) return;
  if (t.top >= e.heap_lo + sizeof(e.below)) {
    e.below_from = t.top - sizeof(e.below);
    std::memcpy(e.below, reinterpret_cast<const void*>(e.below_from), sizeof(e.below));
  }
  if (t.top + sizeof(e.at) <= e.heap_hi) std::memcpy(e.at, reinterpret_cast<const void*>(t.top), sizeof(e.at));
  // The ring's blocks within 64 KB below the top chunk, the highest kept (insertion into a short sorted list).
  for (uint32_t i = 0; i < kRing; ++i) {
    const BigBlock b = g_ring[i];
    if (!b.ptr || b.ptr >= t.top || b.ptr + (64u << 10) < t.top) continue;
    uint32_t k = std::min<uint32_t>(e.near_n, uint32_t(std::size(e.near)) - 1);
    if (e.near_n == std::size(e.near) && b.ptr <= e.near[k].ptr) continue;
    for (; k > 0 && (e.near[k - 1].ptr < b.ptr || (e.near[k - 1].ptr == b.ptr && e.near[k - 1].seq < b.seq)); --k)
      e.near[k] = e.near[k - 1];
    e.near[k] = b;
    e.near_n = std::min<uint32_t>(e.near_n + 1, uint32_t(std::size(e.near)));
  }
  // newlib's chunks from the highest ring block's up to the top chunk: each one's size word (its in-use bit is the
  // next one's bit 0) and first 16 bytes. A word that is not a chunk size ends the walk.
  if (e.near_n) {
    uintptr_t c = e.near[0].ptr - 16;
    while (e.walk_n < std::size(e.walk) && c + 16 <= t.top) {
      Evidence::Chunk& w = e.walk[e.walk_n++];
      w.at = c;
      w.word = reinterpret_cast<const uintptr_t*>(c)[1];
      std::memcpy(w.head, reinterpret_cast<const void*>(c + 16), std::min<uintptr_t>(16, t.top - (c + 16)));
      const uintptr_t size = w.word & ~uintptr_t(3);
      if (size < kMinChunk || (size & 15) || c + size > t.top) break;
      c += size;
    }
  }
}

// The top chunk's size set back to the break: [top, break) is the top chunk's own while top is below the break. Top at
// or past the break (blocks carved from a size too big, beyond the break): the break moved over them first. Under
// malloc's lock.
bool MendLocked(TopChunk& t, const char** why) {
  if (!t.in_heap) {
    *why = "the top chunk is not in the heap";
    return false;
  }
  if (t.top + kMinChunk > t.brk) {
    const uintptr_t want = (t.top + kMinChunk + kPageSize - 1) & ~(kPageSize - 1);
    if (want > reinterpret_cast<uintptr_t>(fake_heap_end)) {
      *why = "the top chunk is past the heap's end";
      return false;
    }
    if (sbrk(intptr_t(want - t.brk)) == reinterpret_cast<void*>(-1)) {
      *why = "sbrk refused the break over the top chunk";
      return false;
    }
    const uintptr_t brk = reinterpret_cast<uintptr_t>(sbrk(0));
    __malloc_current_mallinfo.arena += brk - t.brk;
    t.brk = brk;
  }
  reinterpret_cast<uintptr_t*>(t.top)[1] = (t.brk - t.top) | 1;  // its size, PREV_INUSE (as newlib keeps the top's)
  t.word = (t.brk - t.top) | 1;
  return true;
}

void Hex(char* line, size_t cap, const uint8_t* b, size_t n) {
  size_t at = 0;
  for (size_t i = 0; i < n && at + 4 < cap; ++i) at += size_t(std::snprintf(line + at, cap - at, i % 8 ? " %02x" : "  %02x", b[i]));
}

char g_text[8192];

void Write(const Evidence& e) {
  const uintptr_t base = reinterpret_cast<uintptr_t>(&_start);
  size_t at = 0;
  auto put = [&](const char* fmt, auto... args) {
    if (at + 1 < sizeof(g_text)) {
      const int n = std::snprintf(g_text + at, sizeof(g_text) - at, fmt, args...);
      if (n > 0) at = std::min(sizeof(g_text) - 1, at + size_t(n));
    }
  };
  const TopChunk& t = e.before;
  put("[heap] %s, thread %" PRIu64 ", game frame %" PRIu64 ": top chunk 0x%" PRIxPTR " word 0x%" PRIxPTR
      " (ends 0x%" PRIxPTR "), break 0x%" PRIxPTR " (a sound word: 0x%" PRIxPTR "); heap 0x%" PRIxPTR "-0x%" PRIxPTR
      " (%" PRIuPTR " MB never handed out), sbrk base 0x%" PRIxPTR ", %zu MB taken (peak %zu MB)\n",
      e.what, e.thread, e.frame, t.top, t.word, t.top + (t.word & ~uintptr_t(3)), t.brk, (t.brk - t.top) | 1,
      e.heap_lo, e.heap_hi, e.heap_hi > t.brk ? (e.heap_hi - t.brk) >> 20 : 0, e.sbrk_base, e.taken >> 20,
      e.peak >> 20);
  if (e.want) put("[heap] the request: %zu bytes\n", e.want);
  if (e.mended)
    put("[heap] mended: top chunk 0x%" PRIxPTR " word 0x%" PRIxPTR ", break 0x%" PRIxPTR "\n", e.after.top,
        e.after.word, e.after.brk);
  else
    put("[heap] not mended: %s\n", e.not_mended);
  char line[160];
  if (e.below_from) {
    put("[heap] the 256 bytes below the top chunk (from 0x%" PRIxPTR "):\n", e.below_from);
    for (size_t i = 0; i < sizeof(e.below); i += 16) {
      Hex(line, sizeof(line), e.below + i, 16);
      put("[heap]   +%03zx%s\n", i, line);
    }
  }
  Hex(line, sizeof(line), e.at, 32);
  put("[heap] the top chunk's first 32 bytes:%s\n", line);
  Hex(line, sizeof(line), e.at + 32, 32);
  put("[heap]                              %s\n", line);
  put("[heap] operator new's blocks from newlib in the 64 KB below it (%u remembered so far, the highest first):\n",
      e.ring_next);
  for (uint32_t i = 0; i < e.near_n; ++i) {
    const BigBlock& b = e.near[i];
    put("[heap]   0x%" PRIxPTR " %u bytes (ends 0x%" PRIxPTR "), called from image+0x%" PRIxPTR ", %u blocks ago\n",
        b.ptr, b.size, b.ptr + b.size, b.caller >= base ? b.caller - base : 0, e.ring_next - b.seq);
  }
  for (const BigBlock& b : e.largest) {
    if (b.ptr)
      put("[heap] one of the largest remembered: 0x%" PRIxPTR " %u bytes, called from image+0x%" PRIxPTR
          ", %u blocks ago\n", b.ptr, b.size, b.caller >= base ? b.caller - base : 0, e.ring_next - b.seq);
  }
  if (e.walk_n) put("[heap] newlib's chunks from the highest of those up to the top chunk:\n");
  for (uint32_t i = 0; i < e.walk_n; ++i) {
    const Evidence::Chunk& c = e.walk[i];
    Hex(line, sizeof(line), c.head, 16);
    put("[heap]   0x%" PRIxPTR " word 0x%" PRIxPTR ":%s\n", c.at, c.word, line);
  }
  std::fputs(g_text, stderr);
}

// A broken top chunk, or a request newlib did not serve ('want' bytes, 0 for the frame's look): reported, and mended
// when it is broken (under malloc's lock, so by one thread; another one that failed meanwhile finds it sound and its
// request is tried again too). True when it was mended here. One report at a time, eight in all.
bool CheckHeap(const char* what, size_t want) {
  Evidence& e = g_evidence;
  __malloc_lock(_REENT);
  TopChunk t = ReadTop();
  const bool broken = !t.sound();
  if (!broken && !want) {  // the frame's racy look disagreed, the locked one does not: a block carved in between
    __malloc_unlock(_REENT);
    return false;
  }
  bool report = false;
  if (!g_reporting.test_and_set(std::memory_order_acquire)) {
    report = g_reports.fetch_add(1, std::memory_order_relaxed) < 8;
    if (!report) g_reporting.clear(std::memory_order_release);
  }
  if (report) {
    GatherLocked(e, t);
    e.what = broken ? what : "a request newlib could not serve, the top chunk sound (a real shortage)";
    e.want = want;
    e.frame = g_frames.load(std::memory_order_relaxed);
    svcGetThreadId(&e.thread, CUR_THREAD_HANDLE);
  }
  const char* why = "the top chunk is sound";
  const bool mended = broken && MendLocked(t, &why);
  if (report) {
    e.mended = mended;
    e.after = t;
    e.not_mended = why;
  }
  __malloc_unlock(_REENT);
  if (report) {
    Write(e);
    g_reporting.clear(std::memory_order_release);
  }
  return mended;
}

void* SystemNew(size_t n, void* caller) {
  for (;;) {
    void* p = std::malloc(n ? n : 1);
    if (!p && g_watch.load(std::memory_order_relaxed)) {
      CheckHeap("operator new: malloc returned null", n ? n : 1);
      p = std::malloc(n ? n : 1);  // mended here, or by another thread meanwhile
    }
    if (p) {
      if (g_watch.load(std::memory_order_relaxed)) Remember(p, n, caller);
      return p;
    }
    std::new_handler h = std::get_new_handler();
    if (!h) throw std::bad_alloc();
    h();
  }
}

void* SystemNewNothrow(size_t n, void* caller) noexcept {
  void* p = std::malloc(n ? n : 1);
  if (!p && g_watch.load(std::memory_order_relaxed)) {
    CheckHeap("operator new (nothrow): malloc returned null", n ? n : 1);
    p = std::malloc(n ? n : 1);
  }
  if (p && g_watch.load(std::memory_order_relaxed)) Remember(p, n, caller);
  return p;
}

}  // namespace

namespace blur::small_alloc {

void Enable() {
  if (REXCVAR_GET(blur_heap_watch) && !g_watch.exchange(true)) {
    const TopChunk t = ReadTop();
    REXLOG_INFO("[heap] watch on (blur_heap_watch): top chunk 0x{:x} word 0x{:x}, break 0x{:x} ({}), heap 0x{:x}-0x{:x}",
                t.top, t.word, t.brk, t.sound() ? "sound" : "NOT SOUND",
                reinterpret_cast<uintptr_t>(fake_heap_start), reinterpret_cast<uintptr_t>(fake_heap_end));
  }
  const int mb = REXCVAR_GET(blur_small_alloc_mb);
  if (mb <= 0 || g_begin.load(std::memory_order_acquire)) return;
  const size_t bytes = std::min(size_t(mb) << 20, kMaxArena);
  void* arena = std::aligned_alloc(kPiece, bytes);
  if (!arena) {
    REXLOG_WARN("[alloc] blur_small_alloc_mb: {} MB not available; the system allocator stays", mb);
    return;
  }
  {
    Global& g = G();
    std::lock_guard lock(g.mutex);
    g.carve = static_cast<uint8_t*>(arena);
  }
  g_end.store(static_cast<uint8_t*>(arena) + bytes, std::memory_order_release);
  g_begin.store(static_cast<uint8_t*>(arena), std::memory_order_release);
  REXLOG_INFO("[alloc] #220: small C++ allocations (up to {} bytes) from a {} MB arena, free lists per thread",
              kMaxSmall, bytes >> 20);
}

uint64_t Exhausted() { return G().exhausted; }

// blur-nx (2026-11-23): the heap watch's look once a game frame (renut::frame_stats::OnGameFrame, at the Swap). Three
// plain loads; malloc's lock only when they disagree (a block carved meanwhile, or a broken top chunk).
void WatchFrame() {
  if (!g_watch.load(std::memory_order_relaxed)) return;
  g_frames.fetch_add(1, std::memory_order_relaxed);
  if (ReadTop().sound()) return;
  if (CheckHeap("the game frame's look: the top chunk does not end at the break", 0))
    REXLOG_ERROR("[heap] newlib's top chunk was broken and is mended: what was around it is on stderr "
                 "(rex_stderr.log), the blocks below it with their callers");
}

}  // namespace blur::small_alloc

void* operator new(std::size_t n) {
  if (void* p = SmallNew(n)) return p;
  return SystemNew(n, __builtin_return_address(0));
}
void* operator new[](std::size_t n) {
  if (void* p = SmallNew(n)) return p;
  return SystemNew(n, __builtin_return_address(0));
}
void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
  if (void* p = SmallNew(n)) return p;
  return SystemNewNothrow(n, __builtin_return_address(0));
}
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept {
  if (void* p = SmallNew(n)) return p;
  return SystemNewNothrow(n, __builtin_return_address(0));
}
void operator delete(void* p) noexcept {
  if (p && !SmallDelete(p)) std::free(p);
}
void operator delete[](void* p) noexcept {
  if (p && !SmallDelete(p)) std::free(p);
}
void operator delete(void* p, std::size_t) noexcept {
  if (p && !SmallDelete(p)) std::free(p);
}
void operator delete[](void* p, std::size_t) noexcept {
  if (p && !SmallDelete(p)) std::free(p);
}
void operator delete(void* p, const std::nothrow_t&) noexcept {
  if (p && !SmallDelete(p)) std::free(p);
}
void operator delete[](void* p, const std::nothrow_t&) noexcept {
  if (p && !SmallDelete(p)) std::free(p);
}

#else

namespace blur::small_alloc {
void Enable() {}
uint64_t Exhausted() { return 0; }
void WatchFrame() {}
}  // namespace blur::small_alloc

#endif
