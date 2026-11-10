/**
 * blur-nx (2026-11-11, diagnostic, Windows PC): which recompiled function writes a guest buffer, found by its contents.
 *
 * blur_diag_cube_writer: Blur's 8x8 ambient cube is filled by the CPU every frame into a staging buffer, then copied into
 * the cube texture face by face (the texture updater sub_82332B98 locks a face through D3D's LockSurface and copies).
 * When face 1 of an 8x8 cube is locked, face 0 holds what was just copied: its first 16 bytes (row 0's first four texels)
 * are looked for in every committed page of the guest's virtual view, and each other place found (up to 8) is
 * write-protected. A write there faults; a vectored handler names the host function at the faulting instruction from the
 * PDB (sub_XXXXXXXX, the recompiled guest function, or the helper it inlined), lets the write through by lifting the
 * protection for one single-stepped instruction, and protects the page again: up to 48 writes are logged, as "[watch]".
 * Off by default; costs nothing when off.
 */
#include "blur_write_watch.h"

#if defined(_WIN32)
#include <windows.h>
#include <dbghelp.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/ppc/context.h>

#include <chrono>
#include <cstdio>

extern "C" void __imp__sub_8216AE98(PPCContext &__restrict ctx, uint8_t *base);  // the generated cube filler
extern "C" void __imp__sub_8216CA28(PPCContext &__restrict ctx, uint8_t *base);  // the light slots' writer

REXCVAR_DEFINE_INT32(blur_diag_cube_lights_s, 0, "Blur/Diagnostics",
                     "blur-nx (2026-11-11): from this many seconds in (0: off), the inputs of the function that fills the "
                     "8x8 ambient cubes (sub_8216AE98, reached through a function pointer: wrapped in the indirect-call "
                     "table): its light slots and cube records as floats, every 15 s, four times (\"[lights]\" lines; PC "
                     "only, in Renut's renderer and in the emulated one alike)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(blur_diag_watch_light0, false, "Blur/Diagnostics",
                    "blur-nx (2026-11-11): with blur_diag_cube_lights_s, the first dump also write-protects the page of "
                    "light slot 0's colour, so each write there names its function (\"[watch]\" lines; PC only)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(blur_diag_watch_light_scale, false, "Blur/Diagnostics",
                    "blur-nx (2026-11-11): with blur_diag_cube_lights_s, the light writer's first dump write-protects the "
                    "page of its object's +2684 (the scale of light 0's colour), so each write there names its function "
                    "and line (\"[watch]\" lines; PC only)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(blur_diag_cube_writer, false, "Blur/Diagnostics",
                    "blur-nx (2026-11-11): find the function that writes the 8x8 ambient cube's staging buffer: its "
                    "copy is looked for in guest memory when the cube is locked, the places found are write-protected, "
                    "and each write's function is logged from the PDB (\"[watch]\" lines, PC only)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace blur::write_watch {
namespace {

constexpr size_t kPage = 4096;
constexpr int kMaxPages = 8;
constexpr int kMaxLogs = 48;

std::mutex g_mutex;               // arming, and DbgHelp (not thread-safe)
std::atomic<int> g_state{0};      // 0 waiting for face 0, 1 face 0 seen, 2 armed (or given up)
u32 g_texture = 0, g_face0 = 0;   // the cube's header and face 0's guest address (from LockSurface)
uintptr_t g_pages[kMaxPages] = {};
std::atomic<int> g_page_count{0};
std::atomic<int> g_logs{0};
thread_local uintptr_t t_reprotect = 0;
thread_local uintptr_t t_logged = 0;      // a focused write's address: its new value is logged after the step
thread_local u8 t_before[16];             // its 16-byte block before the write (only a change is logged)
thread_local char t_who[512];             // the writer, logged with the change
std::atomic<uintptr_t> g_focus_lo{0}, g_focus_hi{0};  // only writes in [lo, hi) are logged (0: all)

bool Watched(uintptr_t page) {
  const int n = g_page_count.load(std::memory_order_acquire);
  for (int i = 0; i < n; ++i)
    if (g_pages[i] == page)
      return true;
  return false;
}

std::string Symbol(uintptr_t address) {
  alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 256] = {};
  auto *info = reinterpret_cast<SYMBOL_INFO *>(buffer);
  info->SizeOfStruct = sizeof(SYMBOL_INFO);
  info->MaxNameLen = 255;
  DWORD64 displacement = 0;
  if (!SymFromAddr(GetCurrentProcess(), DWORD64(address), &displacement, info))
    return "?";
  std::string text = std::string(info->Name) + "+" + std::to_string(displacement);
  // The generated source's line: each sits under a comment with its guest instruction.
  IMAGEHLP_LINE64 line = {};
  line.SizeOfStruct = sizeof(line);
  DWORD line_displacement = 0;
  if (SymGetLineFromAddr64(GetCurrentProcess(), DWORD64(address), &line_displacement, &line) && line.FileName) {
    const char *file = std::strrchr(line.FileName, '\\');
    text += std::string(" [") + (file ? file + 1 : line.FileName) + ":" + std::to_string(line.LineNumber) + "]";
  }
  return text;
}

LONG CALLBACK Handler(EXCEPTION_POINTERS *e) {
  const DWORD code = e->ExceptionRecord->ExceptionCode;
  if (code == EXCEPTION_SINGLE_STEP && t_reprotect) {
    if (t_logged && std::memcmp(reinterpret_cast<const void *>(t_logged & ~uintptr_t(15)), t_before, 16) == 0) {
      t_logged = 0;  // the same value written again: not counted
    }
    if (t_logged && g_logs.fetch_add(1, std::memory_order_relaxed) < kMaxLogs) {
      REXLOG_INFO("{}", t_who);
      const u8 *v = reinterpret_cast<const u8 *>(t_logged & ~uintptr_t(3));
      const u32 bits = u32(v[0]) << 24 | u32(v[1]) << 16 | u32(v[2]) << 8 | u32(v[3]);
      float f;
      std::memcpy(&f, &bits, 4);
      const u8 *s = reinterpret_cast<const u8 *>((t_logged & ~uintptr_t(15)) + 12);
      const u32 sbits = u32(s[0]) << 24 | u32(s[1]) << 16 | u32(s[2]) << 8 | u32(s[3]);
      float sf;
      std::memcpy(&sf, &sbits, 4);
      REXLOG_INFO("[watch]     now {:08X} ({}); its 16-byte block's last word {}", bits, f, sf);
      t_logged = 0;
    }
    DWORD old;
    if (g_logs.load(std::memory_order_relaxed) < kMaxLogs)
      VirtualProtect(reinterpret_cast<void *>(t_reprotect), kPage, PAGE_READONLY, &old);
    t_reprotect = 0;
    return EXCEPTION_CONTINUE_EXECUTION;
  }
  if (code != EXCEPTION_ACCESS_VIOLATION || e->ExceptionRecord->NumberParameters < 2 ||
      e->ExceptionRecord->ExceptionInformation[0] != 1)
    return EXCEPTION_CONTINUE_SEARCH;
  const uintptr_t address = uintptr_t(e->ExceptionRecord->ExceptionInformation[1]);
  const uintptr_t page = address & ~uintptr_t(kPage - 1);
  if (!Watched(page))
    return EXCEPTION_CONTINUE_SEARCH;
  const uintptr_t rip = uintptr_t(e->ContextRecord->Rip);
  const uintptr_t lo = g_focus_lo.load(std::memory_order_relaxed), hi = g_focus_hi.load(std::memory_order_relaxed);
  const bool focused = !lo || (address >= lo && address < hi);
  if (focused && g_logs.load(std::memory_order_relaxed) < kMaxLogs) {
    t_logged = address;
    std::memcpy(t_before, reinterpret_cast<const void *>(address & ~uintptr_t(15)), 16);
    std::lock_guard lock(g_mutex);
    u8 *vbase = REX_KERNEL_MEMORY()->virtual_membase();
    std::snprintf(t_who, sizeof(t_who), "[watch] write to host %016llX (guest ~%08X) by %s (rip %016llX)",
                  (unsigned long long)address, u32(address - uintptr_t(vbase)), Symbol(rip).c_str(),
                  (unsigned long long)rip);
  }
  DWORD old;
  VirtualProtect(reinterpret_cast<void *>(page), kPage, PAGE_READWRITE, &old);
  t_reprotect = page;
  e->ContextRecord->EFlags |= 0x100;  // the trap flag: one instruction, then EXCEPTION_SINGLE_STEP protects again
  return EXCEPTION_CONTINUE_EXECUTION;
}

void EnsureHandler() {
  static bool installed = false;
  if (installed)
    return;
  installed = true;
  SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
  SymInitialize(GetCurrentProcess(), nullptr, TRUE);
  AddVectoredExceptionHandler(1, Handler);
}

// Write-protects the page of 'host' (one of kMaxPages); g_mutex held.
void ArmPage(const void *host) {
  EnsureHandler();
  const uintptr_t page = uintptr_t(host) & ~uintptr_t(kPage - 1);
  int n = g_page_count.load(std::memory_order_relaxed);
  for (int i = 0; i < n; ++i)
    if (g_pages[i] == page)
      return;
  if (n >= kMaxPages)
    return;
  g_pages[n] = page;
  g_page_count.store(n + 1, std::memory_order_release);
  DWORD old;
  VirtualProtect(reinterpret_cast<void *>(page), kPage, PAGE_READONLY, &old);
}

// Every place in the guest's virtual view (committed, readable pages) whose 16 bytes are 'pattern', but 'skip'.
void Find(const u8 *pattern, const u8 *skip, std::vector<u8 *> &out) {
  const size_t limit = out.size() + size_t(kMaxPages);
  u8 *vbase = REX_KERNEL_MEMORY()->virtual_membase();
  u8 *at = vbase;
  u8 *const end = vbase + 0x100000000ull;
  MEMORY_BASIC_INFORMATION mbi;
  while (at < end && VirtualQuery(at, &mbi, sizeof(mbi)) == sizeof(mbi)) {
    u8 *region = static_cast<u8 *>(mbi.BaseAddress);
    u8 *region_end = region + mbi.RegionSize;
    const DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE;
    if (mbi.State == MEM_COMMIT && (mbi.Protect & readable) && !(mbi.Protect & PAGE_GUARD)) {
      for (u8 *p = region; p + 16 <= region_end && out.size() < limit; p += 4) {
        if (p != skip && std::memcmp(p, pattern, 16) == 0)
          out.push_back(p);
      }
    }
    at = region_end;
  }
}

}  // namespace

namespace {
// The cube filler's inputs (sub_8216AE98, as its code reads them): r3 = the job; job +0 = a block whose first word
// points at the light slots (15 of 64 bytes from +16) and whose own records (64 bytes from +16) are the cubes; job +16 /
// +20 = the first and end cube index of this call.
std::chrono::steady_clock::time_point g_lights_start{};
std::atomic<int> g_lights_dumps{0};
std::chrono::steady_clock::time_point g_lights_last{};

float GuestFloat(const u8 *p) {
  const u32 bits = u32(p[0]) << 24 | u32(p[1]) << 16 | u32(p[2]) << 8 | u32(p[3]);
  float f;
  std::memcpy(&f, &bits, 4);
  return f;
}
u32 GuestWord(const u8 *p) { return u32(p[0]) << 24 | u32(p[1]) << 16 | u32(p[2]) << 8 | u32(p[3]); }

void DumpRecord(const char *what, int index, const u8 *p) {
  std::string line;
  for (int i = 0; i < 16; ++i) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), " %.5g", GuestFloat(p + 4 * i));
    line += buf;
  }
  REXLOG_INFO("[lights]   {} {}:{}", what, index, line);
}

void CubeFillerWrapper(PPCContext &ctx, uint8_t *base) {
  static const int after_s = REXCVAR_GET(blur_diag_cube_lights_s);
  const auto now = std::chrono::steady_clock::now();
  if (g_lights_dumps.load(std::memory_order_relaxed) < 4 && now - g_lights_start >= std::chrono::seconds(after_s) &&
      now - g_lights_last >= std::chrono::seconds(15)) {
    std::lock_guard lock(g_mutex);
    if (now - g_lights_last >= std::chrono::seconds(15)) {
      g_lights_last = now;
      g_lights_dumps.fetch_add(1, std::memory_order_relaxed);
      auto *memory = REX_KERNEL_MEMORY();
      const u32 job = ctx.r3.u32;
      const u8 *j = memory->TranslateVirtual<const u8 *>(job);
      const u32 block = j ? GuestWord(j) : 0, first = j ? GuestWord(j + 16) : 0, end = j ? GuestWord(j + 20) : 0;
      const u8 *b = block ? memory->TranslateVirtual<const u8 *>(block) : nullptr;
      const u32 lights = b ? GuestWord(b) : 0;
      const u8 *l = lights ? memory->TranslateVirtual<const u8 *>(lights) : nullptr;
      REXLOG_INFO("[lights] cube filler: job {:08X} block {:08X} cubes {}..{} light slots at {:08X}", job, block, first,
                  end, lights);
      if (l)
        for (int i = 0; i < 15; ++i)
          DumpRecord("light", i, l + 16 + 64 * i);
      static const bool watch_light0 = REXCVAR_GET(blur_diag_watch_light0);
      if (l && watch_light0 && g_lights_dumps.load(std::memory_order_relaxed) == 1) {
        REXLOG_INFO("[watch] light slot 0's colour at guest {:08X}: its page write-protected, writes to the colour logged",
                    lights + 16 + 16);
        g_focus_lo.store(uintptr_t(l + 16 + 16), std::memory_order_relaxed);
        g_focus_hi.store(uintptr_t(l + 16 + 16 + 12), std::memory_order_relaxed);
        ArmPage(l + 16 + 16);
      }
      if (b)
        for (u32 i = first; i < end && i < first + 4; ++i)
          DumpRecord("cube", int(i), b + 16 + 64 * i);
    }
  }
  __imp__sub_8216AE98(ctx, base);
}
}  // namespace

// The light slots' writer (sub_8216CA28): light 0's colour = base x s1 x min(v123.w x s3, 1), with v123 the vector at
// its object (r3's first word) +2672, s1 and s3 the floats at +36 of the records at 82C62128 and 82C62160.
std::chrono::steady_clock::time_point g_writer_last{};
std::atomic<int> g_writer_dumps{0};
void DumpWords(const char *what, u32 address, int count) {
  auto *memory = REX_KERNEL_MEMORY();
  const u8 *p = memory->TranslateVirtual<const u8 *>(address);
  if (!p)
    return;
  std::string line;
  for (int i = 0; i < count; ++i) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), " %08X(%.5g)", GuestWord(p + 4 * i), GuestFloat(p + 4 * i));
    line += buf;
  }
  REXLOG_INFO("[lights]   {} {:08X}:{}", what, address, line);
}
std::atomic<u32> g_writer_object{0};
std::atomic<int> g_object_changes{0};
void LightWriterWrapper(PPCContext &ctx, uint8_t *base) {
  static const int after_s = REXCVAR_GET(blur_diag_cube_lights_s);
  static const bool watch_scale_new = REXCVAR_GET(blur_diag_watch_light_scale);
  // A new lighting object (the race's after the menu's): its light vector as first seen, and with the watch on, its
  // +2684 protected from here, to catch the write that sets it.
  if (const u8 *self = REX_KERNEL_MEMORY()->TranslateVirtual<const u8 *>(ctx.r3.u32)) {
    const u32 object = GuestWord(self);
    if (object && g_writer_object.exchange(object, std::memory_order_relaxed) != object &&
        g_object_changes.fetch_add(1, std::memory_order_relaxed) < 6) {
      std::lock_guard lock(g_mutex);
      REXLOG_INFO("[lights] light writer: a new object {:08X}", object);
      DumpWords("object +2672 (v123; .w the scale) as first seen", object + 2672, 4);
      if (watch_scale_new) {
        // The whole vector: a vector store (stvx) faults at its 16-byte block's start, +2672.
        u8 *w = REX_KERNEL_MEMORY()->TranslateVirtual<u8 *>(object + 2672);
        g_focus_lo.store(uintptr_t(w), std::memory_order_relaxed);
        g_focus_hi.store(uintptr_t(w) + 16, std::memory_order_relaxed);
        ArmPage(w);
      }
    }
  }
  const auto now = std::chrono::steady_clock::now();
  if (g_writer_dumps.load(std::memory_order_relaxed) < 4 && now - g_lights_start >= std::chrono::seconds(after_s) &&
      now - g_writer_last >= std::chrono::seconds(15)) {
    std::lock_guard lock(g_mutex);
    if (now - g_writer_last >= std::chrono::seconds(15)) {
      g_writer_last = now;
      g_writer_dumps.fetch_add(1, std::memory_order_relaxed);
      const u8 *self = REX_KERNEL_MEMORY()->TranslateVirtual<const u8 *>(ctx.r3.u32);
      const u32 object = self ? GuestWord(self) : 0;
      REXLOG_INFO("[lights] light writer: this {:08X} object {:08X}", ctx.r3.u32, object);
      if (object)
        DumpWords("object +2672 (v123; .w the scale)", object + 2672, 4);
      static const bool watch_scale = REXCVAR_GET(blur_diag_watch_light_scale);
      if (object && watch_scale && g_writer_dumps.load(std::memory_order_relaxed) == 1) {
        u8 *w = REX_KERNEL_MEMORY()->TranslateVirtual<u8 *>(object + 2684);
        REXLOG_INFO("[watch] the light writer's object +2684 at guest {:08X}: its page write-protected, its writes logged",
                    object + 2684);
        g_focus_lo.store(uintptr_t(w), std::memory_order_relaxed);
        g_focus_hi.store(uintptr_t(w) + 4, std::memory_order_relaxed);
        ArmPage(w);
      }
      DumpWords("record s3", 0x82C62160, 16);
      DumpWords("record s1", 0x82C62128, 16);
    }
  }
  // The scale's course: once a second from blur_diag_cube_lights_s - 40 s on, for 100 s.
  {
    static std::chrono::steady_clock::time_point last_series{};
    const auto from = g_lights_start + std::chrono::seconds(std::max(0, after_s - 40));
    if (now >= from && now < from + std::chrono::seconds(100) && now - last_series >= std::chrono::seconds(1)) {
      last_series = now;
      if (const u8 *self = REX_KERNEL_MEMORY()->TranslateVirtual<const u8 *>(ctx.r3.u32)) {
        const u32 object = GuestWord(self);
        if (const u8 *o = object ? REX_KERNEL_MEMORY()->TranslateVirtual<const u8 *>(object + 2672) : nullptr)
          REXLOG_INFO("[lights] t {} s: object {:08X} light vector {} {} {} scale {}",
                      std::chrono::duration_cast<std::chrono::seconds>(now - g_lights_start).count(), object,
                      GuestFloat(o), GuestFloat(o + 4), GuestFloat(o + 8), GuestFloat(o + 12));
      }
    }
  }
  __imp__sub_8216CA28(ctx, base);
}

void InstallCubeFillerWrapper() {
  if (REXCVAR_GET(blur_diag_cube_lights_s) <= 0)
    return;
  g_lights_start = std::chrono::steady_clock::now();
  const bool set = REX_KERNEL_MEMORY()->SetFunction(0x8216AE98, &CubeFillerWrapper);
  const bool set2 = REX_KERNEL_MEMORY()->SetFunction(0x8216CA28, &LightWriterWrapper);
  REXLOG_INFO("[lights] the cube filler sub_8216AE98 and the light writer sub_8216CA28 wrapped in the indirect-call "
              "table: {} {}", set ? "yes" : "no", set2 ? "yes" : "no");
}

void NoteCubeLock(u32 texture, u32 slice, u32 address) {
  static const bool on = REXCVAR_GET(blur_diag_cube_writer);
  if (!on || g_state.load(std::memory_order_relaxed) == 2)
    return;
  std::lock_guard lock(g_mutex);
  if (slice == 0) {
    g_texture = texture;
    g_face0 = address;
    g_state.store(1, std::memory_order_relaxed);
    return;
  }
  if (slice != 1 || texture != g_texture || g_state.load(std::memory_order_relaxed) != 1 || !g_face0)
    return;
  u8 *face0 = REX_KERNEL_MEMORY()->TranslateVirtual<u8 *>(g_face0);
  if (!face0)
    return;
  u8 pattern[16];
  std::memcpy(pattern, face0, 16);
  // A cube not filled yet (zeros at load time) or a flat one matches memory everywhere: wait for another.
  bool seen[256] = {};
  int distinct = 0;
  for (u8 b : pattern)
    distinct += !seen[b] ? (seen[b] = true, 1) : 0;
  if (distinct < 3) {
    g_state.store(0, std::memory_order_relaxed);
    return;
  }
  g_state.store(2, std::memory_order_relaxed);
  // The texture's words may be stored byte-swapped (its fetch's 8-in-32 endian): the staging copy is looked for in
  // both orders, and the texture's own page (the lock's view, which the copy writes through) is watched too.
  u8 swapped[16];
  for (int i = 0; i < 16; ++i)
    swapped[i] = pattern[(i & ~3) + 3 - (i & 3)];
  std::vector<u8 *> found;
  found.push_back(face0);
  Find(pattern, face0, found);
  Find(swapped, face0, found);
  u8 *vbase = REX_KERNEL_MEMORY()->virtual_membase();
  REXLOG_INFO("[watch] cube {:08X} face 0 at {:08X}: first words {:02X}{:02X}{:02X}{:02X} {:02X}{:02X}{:02X}{:02X}; {} "
              "other place(s) with them", texture, g_face0, pattern[0], pattern[1], pattern[2], pattern[3], pattern[4],
              pattern[5], pattern[6], pattern[7], found.size());
  EnsureHandler();
  int n = 0;
  for (u8 *p : found) {
    if (n >= kMaxPages)
      break;
    const uintptr_t page = uintptr_t(p) & ~uintptr_t(kPage - 1);
    bool dup = false;
    for (int i = 0; i < n; ++i)
      dup |= g_pages[i] == page;
    REXLOG_INFO("[watch]   at host {:016X} (guest ~{:08X}){}", uintptr_t(p), u32(p - vbase), dup ? " (same page)" : "");
    if (dup || n >= kMaxPages)
      continue;
    g_pages[n++] = page;
  }
  g_page_count.store(n, std::memory_order_release);
  for (int i = 0; i < n; ++i) {
    DWORD old;
    VirtualProtect(reinterpret_cast<void *>(g_pages[i]), kPage, PAGE_READONLY, &old);
  }
}

}  // namespace blur::write_watch
#endif
