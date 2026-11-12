// banjo-nx: profile collection for profile-guided optimisation (PGO).
// From sa2-nx (app/src/sa2_pgo.cpp, GPL-3.0, same project family); only the names differ.
//
// The instrumented Switch build (cmake -DBANJO_PGO=generate) counts every branch of the game code and the app. GCC's
// usual way to save the counts (libgcov writing a .gcda file per object at exit, at the build machine's paths) does
// not fit the Switch: the process is often closed from the HOME menu without exiting, and those paths do not exist
// there. So the build registers each object's counts in a section (-fprofile-info-section=gcov_info) instead, and
// this file streams them all into one file, pgo/banjo_pgo_<N>.stream next to the NRO (N = this session):
//   - every 2 minutes, when the game loses focus (HOME menu) and at exit;
//   - the whole session each time (the counts only grow), replacing the previous dump through a .tmp file, so a dump
//     cut short (console switched off) leaves the previous one.
// On the PC, tools/pgo_merge.ps1 turns every session's stream into .gcda files in the profile folder
// (gcov-tool merge-stream, which adds sessions together), and the next build with -DBANJO_PGO=use reads them.

#include "banjo_pgo.h"

#if defined(BANJO_PGO_GENERATE)

#include <chrono>
#include <condition_variable>
#include <cstdint>  // blur-nx (2026-11-12): the value profiler's 64-bit counters
#include <cstdio>
#include <cstdlib>  // blur-nx (2026-11-12): calloc (Allocate)
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

extern "C" {  // GCC's gcov.h has no C++ guard of its own
#include <gcov.h>
}

#include <rex/filesystem.h>

// One pointer per instrumented object, in the section GNU ld brackets with these two symbols.
extern "C" const struct gcov_info* const __start_gcov_info[];
extern "C" const struct gcov_info* const __stop_gcov_info[];

// blur-nx (2026-11-12, CMake BANJO_PGO_VALUES: value profiles in the training build). With -fprofile-values every
// indirect call writes its callee and counters into a thread-local tuple, __gcov_indirect_call, and the callee's entry
// calls __gcov_indirect_call_profiler_v4[_atomic], which reads it back. devkitA64's libgcov.a has that one object
// (_gcov_indirect_call_profiler_v4.o) built for the hardware thread pointer (`mrs x4, tpidr_el0`), which is 0 on the
// Switch: the build uses libnx's soft one (-mtp=soft), so the first instrumented NRO stopped 8 s into Ryujinx on a
// null access inside it. These definitions take its place (the archive member is no longer pulled in: all three of its
// symbols are here), compiled with the build's own TLS access; the counting is libgcov's own top-n profiler (no TLS).
// The same as libgcc's libgcov-profiler.c (GCC 16.1), minus the function-descriptor case (AArch64 has none).
extern "C" {
struct BanjoGcovIndirectCall {
  void* callee;
  int64_t* counters;  // gcov_type: 64-bit
};
[[gnu::used]] __thread BanjoGcovIndirectCall __gcov_indirect_call;
void __gcov_topn_values_profiler(int64_t* counters, int64_t value);
void __gcov_topn_values_profiler_atomic(int64_t* counters, int64_t value);
[[gnu::used, gnu::no_profile_instrument_function]] void __gcov_indirect_call_profiler_v4(int64_t value,
                                                                                        void* cur_func) {
  if (cur_func == __gcov_indirect_call.callee) __gcov_topn_values_profiler(__gcov_indirect_call.counters, value);
  __gcov_indirect_call.callee = nullptr;
}
[[gnu::used, gnu::no_profile_instrument_function]] void __gcov_indirect_call_profiler_v4_atomic(int64_t value,
                                                                                               void* cur_func) {
  if (cur_func == __gcov_indirect_call.callee)
    __gcov_topn_values_profiler_atomic(__gcov_indirect_call.counters, value);
  __gcov_indirect_call.callee = nullptr;
}
}  // extern "C"

// blur-nx (2026-11-12, BANJO_PGO_VALUES): libgcov's top-n value profilers (the targets of indirect calls, the common
// sizes and divisors) take each new list node from calloc, from whatever instrumented code is running on whatever
// thread; with them the value-profile training NRO corrupted the heap (two Ryujinx runs, ~2.5 min in: a null lock in
// malloc and free on the main thread). This libgcov build never uses its node pool (__gcov_kvp_dynamic_pool: no
// reference from the profiler). So both profilers are here, with libgcov's exact behaviour (gcov_topn_add_value,
// count 1, the total counted; GCC 16.1's object disassembled: 24-byte nodes {value, count, next}, at most 32 values a
// list, the list's head or its tail's next set by compare-and-swap, then the node count; a full list decrements its
// least counted node and takes the value over when that reaches 0), but with nodes from a static pool and never the
// heap. An exhausted pool drops the new value (counted in pgo/banjo_pgo_pool.txt at each dump).
namespace banjo::pgo {
namespace {
struct Kvp {
  int64_t value;
  int64_t count;
  Kvp* next;
};
static_assert(sizeof(Kvp) == 24, "libgcov's gcov_kvp");
constexpr unsigned kKvpPool = 1u << 19;  // 12 MB
constexpr int64_t kTopnMaxTracked = 32;  // GCOV_TOPN_MAXIMUM_TRACKED_VALUES
Kvp g_kvp_pool[kKvpPool];
unsigned g_kvp_next = 0;
unsigned g_kvp_dropped = 0;

[[gnu::no_profile_instrument_function]] inline void Add(int64_t* where, int64_t n, bool atomic) {
  if (atomic)
    __atomic_fetch_add(where, n, __ATOMIC_RELAXED);
  else
    *where += n;
}

[[gnu::no_profile_instrument_function]] Kvp* NewKvp() {
  const unsigned i = __atomic_fetch_add(&g_kvp_next, 1u, __ATOMIC_RELAXED);
  if (i < kKvpPool) return &g_kvp_pool[i];
  __atomic_fetch_add(&g_kvp_dropped, 1u, __ATOMIC_RELAXED);
  return nullptr;
}

// blur-nx (2026-11-12): the lists stand still while a dump walks them. libgcov's writer (write_topn_counters) walks
// each list twice, the first time for its length, the second for its pairs; a list that changed in between made it
// read past a null node (the console's crash 2 min in, at the first dump: libgcov-driver.c:497, on the dump thread).
// A dump sets g_topn_frozen and waits until no update is inside TopnAdd; updates arriving meanwhile are skipped (a few
// seconds of samples per dump). The flag and the count are sequentially consistent on both sides (store, then load).
int g_topn_frozen = 0;
int g_topn_inside = 0;

[[gnu::no_profile_instrument_function]] void TopnAddBody(int64_t* counters, int64_t value, bool atomic);

[[gnu::no_profile_instrument_function]] void TopnAdd(int64_t* counters, int64_t value, bool atomic) {
  if (__atomic_load_n(&g_topn_frozen, __ATOMIC_SEQ_CST)) return;
  __atomic_fetch_add(&g_topn_inside, 1, __ATOMIC_SEQ_CST);
  if (__atomic_load_n(&g_topn_frozen, __ATOMIC_SEQ_CST) || counters[0] < 0) {  // (a negative total: libgcov bails out)
    __atomic_fetch_sub(&g_topn_inside, 1, __ATOMIC_SEQ_CST);
    return;
  }
  TopnAddBody(counters, value, atomic);
  __atomic_fetch_sub(&g_topn_inside, 1, __ATOMIC_SEQ_CST);
}

[[gnu::no_profile_instrument_function]] void TopnAddBody(int64_t* counters, int64_t value, bool atomic) {
  Add(&counters[0], 1, atomic);
  Kvp* prev = nullptr;
  Kvp* minimal = nullptr;
  for (Kvp* node = reinterpret_cast<Kvp*>(static_cast<intptr_t>(counters[2])); node; node = node->next) {
    if (node->value == value) {
      Add(&node->count, 1, atomic);
      return;
    }
    if (!minimal || node->count < minimal->count) minimal = node;
    prev = node;
  }
  if (counters[1] == kTopnMaxTracked) {
    if (--minimal->count < 1) {
      minimal->value = value;
      minimal->count = 1;
    }
    return;
  }
  Kvp* fresh = NewKvp();
  if (!fresh) return;
  fresh->value = value;
  fresh->count = 1;
  bool linked = false;
  if (!counters[2]) {
    if (atomic) {
      Kvp** head = reinterpret_cast<Kvp**>(&counters[2]);
      linked = __sync_val_compare_and_swap(head, static_cast<Kvp*>(nullptr), fresh) == nullptr;
    } else {
      counters[2] = static_cast<int64_t>(reinterpret_cast<intptr_t>(fresh));
      linked = true;
    }
  } else if (prev && !prev->next) {
    if (atomic) {
      linked = __sync_val_compare_and_swap(&prev->next, static_cast<Kvp*>(nullptr), fresh) == nullptr;
    } else {
      prev->next = fresh;
      linked = true;
    }
  }
  if (linked) Add(&counters[1], 1, atomic);  // a node that lost its race stays unused, as in libgcov
}
}  // namespace
}  // namespace banjo::pgo

extern "C" {
[[gnu::used, gnu::no_profile_instrument_function]] void __gcov_topn_values_profiler(int64_t* counters, int64_t value) {
  banjo::pgo::TopnAdd(counters, value, false);
}
[[gnu::used, gnu::no_profile_instrument_function]] void __gcov_topn_values_profiler_atomic(int64_t* counters,
                                                                                          int64_t value) {
  banjo::pgo::TopnAdd(counters, value, true);
}
}  // extern "C"

namespace banjo::pgo {
namespace {

constexpr auto kPeriod = std::chrono::minutes(2);

std::mutex g_dump_mutex;  // one dump at a time (the thread, focus loss, exit)
std::filesystem::path g_file;
std::mutex g_thread_mutex;
std::condition_variable g_wake;
bool g_stop = false;
std::thread g_thread;

struct Stream {
  std::FILE* file = nullptr;
  bool ok = true;
};

void Write(const void* data, unsigned length, void* arg) {
  auto* s = static_cast<Stream*>(arg);
  if (s->ok && length && std::fwrite(data, 1, length, s->file) != length) s->ok = false;
}

void Filename(const char* name, void* arg) {
  __gcov_filename_to_gcfn(name, Write, arg);
}

// blur-nx (2026-11-12, BANJO_PGO_VALUES): libgcov keeps what this returns. With value profiles its top-n writer
// (write_topn_counters, GCC 16.1) allocates its table of list sizes through here once, into a static pointer, and
// reuses it in every later dump (a new one only when a larger table is needed). Freed at the end of each dump (as it
// was: the buffers lived in the Stream) it was written through in the next one: the value-profile training NRO's heap
// corruption, a crash in free 2:26 into Ryujinx. So the memory stays: a few KB, allocated once or twice in a session.
// Builds without value profiles never call this.
void* Allocate(unsigned length, void* /*arg*/) { return std::calloc(length ? length : 1, 1); }

}  // namespace

void DumpNow() {
  std::lock_guard<std::mutex> lock(g_dump_mutex);
  if (g_file.empty()) return;
  std::filesystem::path temporary = g_file;
  temporary += ".tmp";
  Stream stream;
  stream.file = std::fopen(temporary.string().c_str(), "wb");
  if (!stream.file) return;
  static std::vector<char> buffer(1 << 20);  // one SD write per MB instead of per record
  std::setvbuf(stream.file, buffer.data(), _IOFBF, buffer.size());
  // The value lists stand still for the dump (above): no update starts, the ones inside finish.
  __atomic_store_n(&g_topn_frozen, 1, __ATOMIC_SEQ_CST);
  while (__atomic_load_n(&g_topn_inside, __ATOMIC_SEQ_CST) != 0) std::this_thread::yield();
  const gcov_info* const* info = __start_gcov_info;
  const gcov_info* const* const end = __stop_gcov_info;
  __asm__("" : "+r"(info));  // as in GCC's manual: the compiler must not reason about the linker's symbols
  for (; info != end && stream.ok; ++info) {
    if (*info) __gcov_info_to_gcda(*info, Filename, Write, Allocate, &stream);
  }
  __atomic_store_n(&g_topn_frozen, 0, __ATOMIC_SEQ_CST);
  const bool closed = std::fclose(stream.file) == 0;
  if (!stream.ok || !closed) return;  // the previous complete dump stays
  std::error_code ec;
  std::filesystem::rename(temporary, g_file, ec);
  if (ec) {  // a file system that does not replace on rename
    std::filesystem::remove(g_file, ec);
    std::filesystem::rename(temporary, g_file, ec);
  }
  // blur-nx (2026-11-12): the top-n node pool's use (above), for sizing it.
  const std::filesystem::path pool = g_file.parent_path() / "banjo_pgo_pool.txt";
  if (std::FILE* f = std::fopen(pool.string().c_str(), "w")) {
    std::fprintf(f, "top-n nodes used %u of %u, values dropped (pool full) %u\n",
                 __atomic_load_n(&g_kvp_next, __ATOMIC_RELAXED) < kKvpPool ? __atomic_load_n(&g_kvp_next, __ATOMIC_RELAXED)
                                                                           : kKvpPool,
                 kKvpPool, __atomic_load_n(&g_kvp_dropped, __ATOMIC_RELAXED));
    std::fclose(f);
  }
}

void Start() {
  const std::filesystem::path folder = rex::filesystem::GetExecutableFolder() / "pgo";
  std::error_code ec;
  std::filesystem::create_directories(folder, ec);
  for (int n = 1; n < 100000; ++n) {
    const std::filesystem::path candidate = folder / ("banjo_pgo_" + std::to_string(n) + ".stream");
    std::filesystem::path temporary = candidate;
    temporary += ".tmp";
    if (!std::filesystem::exists(candidate, ec) && !std::filesystem::exists(temporary, ec)) {
      g_file = candidate;
      break;
    }
  }
  if (g_file.empty()) return;
  g_thread = std::thread([] {
    std::unique_lock<std::mutex> lock(g_thread_mutex);
    while (!g_wake.wait_for(lock, kPeriod, [] { return g_stop; })) {
      lock.unlock();
      DumpNow();
      lock.lock();
    }
  });
}

void Stop() {
  {
    std::lock_guard<std::mutex> lock(g_thread_mutex);
    g_stop = true;
  }
  g_wake.notify_all();
  if (g_thread.joinable()) g_thread.join();
  DumpNow();
}

}  // namespace banjo::pgo

#else  // not the instrumented build

namespace banjo::pgo {
void Start() {}
void DumpNow() {}
void Stop() {}
}  // namespace banjo::pgo

#endif
