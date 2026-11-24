// blur-nx 1.0.1 (2026-11-21, the user: "rencode video and audio to opus and h265", "also Bink videos ZXC compressed
// loading as well like sart"): Blur's movies (RAD's Bink 1, BIKi, 1280x720, decoded by the Bink library linked into
// the game's own code) shown from H.265 on the Switch's NVDEC instead of decoded as Bink by the guest. The method is
// rr6-nx's for its Sofdec movies (app/src/cpu/rr6_hevc_movies.cpp, credit rr6-nx and culdcept-nx; the decoder machinery
// below is rr6-nx's): NAME.bik.hevc (rex/filesystem/sfd_hevc.h, container-agnostic: segments of stored bytes, zeros and
// tag bytes) shows the game NAME.bik as it was, the same size, every header, index and audio packet byte as it was, but
// each frame's video data is zero except a 10-byte tag at its start (the movie and the frame's index; Bink has no
// reordering: the frame index is the display order). tools/bik_hevc_pack.py and the web installer make the files.
//
// Found in the game's code (2026-11-21):
//   sub_82A309F8                        the game's movie update (its movie object +156 the HBINK): BinkWait 82AF8688,
//                                       BinkDoFrame 82AF8F30, BinkShouldSkip 82AF8628, BinkNextFrame 82AF8F38
//   sub_82AF8F30 BinkDoFrame            li r4,775; b sub_82AF8D38
//   sub_82AF8D38(bink, flags)           the frame's packet bink+256, its length bink+260; the audio tracks (bink+224
//                                       of them): u32 size then the data each, the enabled ones to sub_82AF84D0; then
//                                       the video data: sub_82AFF648(bink+188, bink+208, bink+196, video data, bink+268,
//                                       the packet's end, bink+288, flags, ...)
//   sub_82AFF648(fb, ..., data r6, ..., end r8, ...)
//                                       Bink 1's video decode into the frame buffers fb (RAD's BINKFRAMEBUFFERS, the
//                                       game's own: +4 / +8 the Y and alpha planes' width and height, +12 / +16 the
//                                       chroma planes', +20 FrameNum, then the plane sets from +24, 48 bytes each: Y,
//                                       cR, cB, A, each {Allocate, Buffer, BufferPitch}); it decodes from set FrameNum
//                                       (the previous picture) into set FrameNum ^ 1 when that set has a Y buffer, else
//                                       into FrameNum's own; it returns a statistic (a percentage the caller keeps the
//                                       largest of at bink+324). It does not change FrameNum.
//
// By the time sub_82AFF648 runs, Bink has byte-swapped the frame's packet in 32-bit words (its bit reader's order), so
// the tag is read back through that swap (FindTag; found on the PC 2026-11-22, after two green console runs).
// The hook: sub_82AFF648 with tagged video data decodes nothing; the H.265 picture of the tag's frame goes into the
// plane set Bink would have decoded into (Y, then U into cB and V into cR), the written rows' store cards marked so the
// renderer takes them again, and 0 is returned. Timing, audio, the frame buffers' flips and the game's drawing (its
// "BinkTextures" shader) stay Bink's and the game's: only the pixels come from elsewhere.
#include <rex/cvar.h>
#include <rex/filesystem/sfd_hevc.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/func.h>
#include <rex/system/xmemory.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if RENUT_NXVIDEO
#include <switch.h>
#include "nxvideo.h"
#endif

REXCVAR_DEFINE_BOOL(blur_hevc_movies, true, "Blur",
                    "The movies the installer made H.265 (NAME.bik.hevc[.lz4b] beside the game files) shown from H.265 "
                    "on the Switch's video hardware; false: their tags are not looked for (such a movie then shows "
                    "nothing)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
// As rr6-nx's rr6_hevc_nvdec: Ryujinx opens nvtegra's NVDEC for H.265 but emulates no H.265 decoding.
REXCVAR_DEFINE_BOOL(blur_hevc_nvdec, true, "Blur",
                    "The H.265 movies on the Switch's video hardware (NVDEC); false: FFmpeg's software decoder (slow: for "
                    "an emulator, which has no H.265 NVDEC)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(blur_hevc_log, false, "Blur",
                    "One [hevc] line every 300 pictures shown from H.265: the time reading, decoding and copying, and "
                    "nxvideo's own steps and clocks")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
// As rr6-nx's rr6_hevc_decode_priority (culdcept-nx's culdcept_movie_decode_priority): the decode waits on the hardware
// on the game's own thread (0x3B); woken, it would wait for a core behind the game's other threads.
REXCVAR_DEFINE_INT32(blur_hevc_decode_priority, 0x2C, "Blur",
                     "The thread's priority while it waits for an H.265 picture on NVDEC (0x1C-0x3B, lower runs first; "
                     "the game's threads 0x3B); 0 = the thread's own")
    .range(0, 0x3B)
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

REX_EXTERN(__imp__sub_82AFF648);  // Bink 1's video decode into the frame buffers

namespace {

uint8_t* Host(uint8_t* base, uint32_t a) { return rex::memory::GuestPtr(base, a); }
uint32_t Ld32(uint8_t* base, uint32_t a) {
  const uint8_t* p = Host(base, a);
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

std::atomic<uint64_t> g_tagged{0}, g_shown{0}, g_missing{0}, g_failed{0};

// The tag at a frame's video data. Blur's Bink (the 360's) has byte-swapped the frame's packet in 32-bit words by the
// time its video decode runs (seen 2026-11-22: the file's A5 5A C3 80 80 81 80 80 read 80 C3 5A A5 80 80 81 80), so the
// tag is read back through the swap: each byte from its address's mirror in its word. Then the same with the words
// counted from the video data's start (should a packet ever be swapped from there), then as it is. The tag's magic and
// checksum make a wrong form fail. Returns the form that matched (0, 1, 2) or -1.
int FindTag(uint8_t* base, uint32_t data, uint32_t* id, uint32_t* index) {
  uint8_t t[rex::filesystem::kSfdHevcTagBytes];
  for (int form = 0; form < 3; ++form) {
    if (form == 1 && (data & 3) == 0) continue;  // the same bytes as form 0
    for (uint32_t i = 0; i < rex::filesystem::kSfdHevcTagBytes; ++i) {
      const uint32_t a = data + i;
      const uint32_t at = form == 0   ? (a & ~3u) | (3 - (a & 3))
                          : form == 1 ? data + ((i & ~3u) | (3 - (i & 3)))
                                      : a;
      t[i] = *Host(base, at);
    }
    if (rex::filesystem::ParseSfdHevcTag(t, id, index)) return form;
  }
  return -1;
}

// The plane set sub_82AFF648 decodes into: FrameNum ^ 1 when that set has a Y buffer, else FrameNum's own.
struct Planes {
  uint32_t y = 0, u = 0, v = 0, y_pitch = 0, c_pitch = 0, w = 0, h = 0;
};
bool BinkPlanes(uint8_t* base, uint32_t fb, Planes* p) {
  const uint32_t current = Ld32(base, fb + 20);
  if (current > 1) return false;
  const uint32_t other = current ^ 1u;
  const uint32_t set = fb + 24 + 48 * (Ld32(base, fb + 24 + 48 * other + 4) ? other : current);
  p->y = Ld32(base, set + 4);
  p->y_pitch = Ld32(base, set + 8);
  p->v = Ld32(base, set + 16);  // cR
  p->c_pitch = Ld32(base, set + 20);
  p->u = Ld32(base, set + 28);  // cB
  p->w = Ld32(base, fb + 4);
  p->h = Ld32(base, fb + 8);
  const uint32_t cb_pitch = Ld32(base, set + 32);
  return p->y && p->u && p->v && p->w && p->h && p->w <= 4096 && p->h <= 4096 && p->y_pitch >= p->w &&
         p->c_pitch >= p->w / 2 && cb_pitch == p->c_pitch;
}

#if RENUT_NXVIDEO
// rr6-nx's read-ahead (2026-11-21, its console's second test): the frames a movie will ask for next are read by a
// thread of their own, kAhead of them ahead of the decoder.
constexpr int64_t kAhead = 45;  // 1.5 s at 30 frames a second

struct Prefetch {
  std::shared_ptr<const rex::filesystem::SfdHevcMovie> movie;
  std::mutex m;
  int64_t want = 0;  // the first frame the decoder still needs
  std::map<uint32_t, std::vector<uint8_t>> ready;
  uint64_t hits = 0, misses = 0;
};

std::mutex g_ahead_mutex;
std::condition_variable g_ahead_cv;
std::vector<std::weak_ptr<Prefetch>> g_ahead;

void AheadLoop() {
  std::vector<uint8_t> bytes;
  for (;;) {
    std::shared_ptr<Prefetch> job;
    uint32_t frame = 0;
    {
      std::unique_lock<std::mutex> lock(g_ahead_mutex);
      for (;;) {
        g_ahead.erase(std::remove_if(g_ahead.begin(), g_ahead.end(), [](const auto& w) { return w.expired(); }),
                      g_ahead.end());
        for (const auto& w : g_ahead) {
          auto p = w.lock();
          if (!p) continue;
          std::lock_guard<std::mutex> pl(p->m);
          const int64_t end = std::min<int64_t>(p->want + kAhead, int64_t(p->movie->frames.size()));
          for (int64_t f = std::max<int64_t>(p->want, 0); f < end; ++f) {
            if (!p->ready.count(uint32_t(f))) {
              job = std::move(p);
              frame = uint32_t(f);
              break;
            }
          }
          if (job) break;
        }
        if (job) break;
        g_ahead_cv.wait_for(lock, std::chrono::milliseconds(50));
      }
    }
    if (!job->movie->ReadFrame(frame, &bytes)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));  // read again later rather than spin on a failure
      continue;
    }
    std::lock_guard<std::mutex> pl(job->m);
    if (int64_t(frame) >= job->want && int64_t(frame) < job->want + kAhead) job->ready[frame].swap(bytes);
  }
}

// The read-ahead thread, once (a libnx thread, as rr6-nx found a std::thread started from the game's thread aborted the
// process in Ryujinx). Null (each frame read on the game's thread) if it did not start.
std::shared_ptr<Prefetch> StartAhead(std::shared_ptr<const rex::filesystem::SfdHevcMovie> movie) {
  static const bool running = [] {
    static Thread thread;
    const int prio = REXCVAR_GET(blur_hevc_decode_priority);
    const bool ok = R_SUCCEEDED(threadCreate(&thread, [](void*) { AheadLoop(); }, nullptr, nullptr, 256 * 1024,
                                             prio > 0 ? prio : 0x2C, -2)) &&
                    R_SUCCEEDED(threadStart(&thread));
    if (!ok) REXLOG_WARN("[hevc] the read-ahead thread did not start: the frames are read on the game's thread");
    return ok;
  }();
  if (!running) return nullptr;
  auto p = std::make_shared<Prefetch>();
  p->movie = std::move(movie);
  {
    std::lock_guard<std::mutex> lock(g_ahead_mutex);
    g_ahead.push_back(p);
  }
  g_ahead_cv.notify_one();
  return p;
}

bool TakeFrame(Prefetch& p, uint32_t f, std::vector<uint8_t>* out) {
  bool hit = false;
  {
    std::lock_guard<std::mutex> lock(p.m);
    p.want = int64_t(f) + 1;
    auto it = p.ready.find(f);
    if (it != p.ready.end()) {
      out->swap(it->second);
      hit = true;
    }
    for (auto i = p.ready.begin(); i != p.ready.end();) {
      if (int64_t(i->first) < p.want || int64_t(i->first) >= p.want + kAhead)
        i = p.ready.erase(i);
      else
        ++i;
    }
    ++(hit ? p.hits : p.misses);
  }
  g_ahead_cv.notify_one();
  return hit || p.movie->ReadFrame(f, out);
}

struct Decoder {
  std::shared_ptr<const rex::filesystem::SfdHevcMovie> movie;
  std::shared_ptr<Prefetch> ahead;
  NxvDecoder* d = nullptr;
  int64_t next = 0;  // the next frame nxv_decode would give
  std::vector<uint8_t> bytes;
  std::chrono::steady_clock::time_point used;
  double decode_ms = 0, copy_ms = 0, read_ms = 0, nxv_ms = 0;
  uint64_t frames = 0;
};

int DecodeRaised(Decoder& dc, NxvPicture* pic) {
  static const int boost = REXCVAR_GET(blur_hevc_decode_priority);
  s32 own = 0;
  const bool raised = boost > 0 && R_SUCCEEDED(svcGetThreadPriority(&own, CUR_THREAD_HANDLE)) && own > boost &&
                      R_SUCCEEDED(svcSetThreadPriority(CUR_THREAD_HANDLE, u32(boost)));
  const auto t0 = std::chrono::steady_clock::now();
  const int r = nxv_decode(dc.d, dc.bytes.data(), int(dc.bytes.size()), pic);
  dc.nxv_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  if (raised) svcSetThreadPriority(CUR_THREAD_HANDLE, u32(own));
  return r;
}

void NxvLine(Decoder& dc) {
  NxvStats st;
  nxv_stats(dc.d, &st);
  if (!st.frames) return;
  const double n = double(st.frames);
  REXLOG_INFO("[hevc] {}: nxvideo a picture ({}): send {:.2f} ms, receive {:.2f} ms, transfer {:.2f} ms; NVDEC {} MHz "
              "(DFS's start {} MHz, put back {} times), VIC {} MHz; copied out by the VIC {}, by the CPU {}",
              dc.movie->name, st.frames, st.send_ms / n, st.receive_ms / n, st.transfer_ms / n, st.nvdec_hz / 1000000,
              st.nvdec_max_hz / 1000000, st.restores, st.vic_hz / 1000000, st.vic_frames, st.cpu_frames);
}

std::mutex g_mutex;
std::map<uint32_t, Decoder> g_decoders;

void CloseIdle() {
  const auto now = std::chrono::steady_clock::now();
  for (auto it = g_decoders.begin(); it != g_decoders.end();) {
    if (now - it->second.used > std::chrono::seconds(15)) {
      if (it->second.d) nxv_close(it->second.d);
      it = g_decoders.erase(it);
    } else {
      ++it;
    }
  }
}

// Frame `index` into `pic`: decoded in order from the last one, or from the keyframe at or before it (a jump back:
// the game's looping attract movie starts again at frame 0, a keyframe).
bool DecodeTo(Decoder& dc, uint32_t index, NxvPicture* pic) {
  const auto& frames = dc.movie->frames;
  if (index >= frames.size()) return false;
  if (!dc.d) {
    char why[256] = {};
    static const bool hw = REXCVAR_GET(blur_hevc_nvdec);
    dc.d = nxv_open(NXV_HEVC, nullptr, 0, int(dc.movie->width), int(dc.movie->height), hw ? 1 : 0, why, sizeof(why));
    REXLOG_INFO("[hevc] {}: {}", dc.movie->name, dc.d ? why : "the H.265 decoder did not open");
    if (!dc.d) return false;
    dc.next = 0;
  }
  if (int64_t(index) < dc.next || int64_t(index) > dc.next + 120) {
    int64_t k = index;
    while (k > 0 && !frames[size_t(k)].keyframe) --k;
    if (!(int64_t(index) >= dc.next && k <= dc.next)) {
      nxv_flush(dc.d);
      dc.next = k;
    }
  }
  bool got = false;
  while (dc.next <= int64_t(index)) {
    const uint32_t f = uint32_t(dc.next++);
    const auto t0 = std::chrono::steady_clock::now();
    if (!(dc.ahead ? TakeFrame(*dc.ahead, f, &dc.bytes) : dc.movie->ReadFrame(f, &dc.bytes))) return false;
    dc.read_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    const int r = DecodeRaised(dc, pic);
    got = r == 1 && f == index;
    if (r < 0) {
      REXLOG_WARN("[hevc] {}: frame {} did not decode", dc.movie->name, f);
      return false;
    }
  }
  return got;
}

// The picture into Bink's plane set: Y as it is, U into cB and V into cR (NV12's interleaved chroma split).
void WritePicture(uint8_t* base, const NxvPicture& pic, const Planes& pl) {
  const uint32_t fw = std::min<uint32_t>(pl.w, uint32_t(pic.width)), fh = std::min<uint32_t>(pl.h, uint32_t(pic.height));
  for (uint32_t y = 0; y < fh; ++y)
    std::memcpy(Host(base, pl.y + y * pl.y_pitch), pic.data[0] + size_t(y) * size_t(pic.linesize[0]), fw);
  const uint32_t cw = fw / 2, ch = fh / 2;
  for (uint32_t y = 0; y < ch; ++y) {
    uint8_t* u = Host(base, pl.u + y * pl.c_pitch);
    uint8_t* v = Host(base, pl.v + y * pl.c_pitch);
    if (pic.nv12) {
      const uint8_t* s = pic.data[1] + size_t(y) * size_t(pic.linesize[1]);
      for (uint32_t x = 0; x < cw; ++x) {
        u[x] = s[2 * x];
        v[x] = s[2 * x + 1];
      }
    } else {
      std::memcpy(u, pic.data[1] + size_t(y) * size_t(pic.linesize[1]), cw);
      std::memcpy(v, pic.data[2] + size_t(y) * size_t(pic.linesize[2]), cw);
    }
  }
  rex::memory::MarkStoreCardsAt(base, pl.y, pl.y_pitch * fh);
  rex::memory::MarkStoreCardsAt(base, pl.u, pl.c_pitch * ch);
  rex::memory::MarkStoreCardsAt(base, pl.v, pl.c_pitch * ch);
}
#endif

}  // namespace

REX_HOOK_RAW(sub_82AFF648) {
  static const bool on = REXCVAR_GET(blur_hevc_movies);
  const uint32_t fb = ctx.r3.u32, data = ctx.r6.u32, end = ctx.r8.u32;
  uint32_t id = 0, index = 0;
  // blur_hevc_log: the first calls and every 60th (whether the decode reaches this hook, the video data it is given and
  // the tag's form), and for a Bink frame, calls 60 and 120, a strip of the picture Bink decoded (its planes' byte
  // order and which of cR / cB is U, against FFmpeg's decode of the same movie).
  static const bool trace = REXCVAR_GET(blur_hevc_log);
  static std::atomic<uint32_t> g_calls{0};
  const uint32_t call = trace ? g_calls.fetch_add(1, std::memory_order_relaxed) : 0;
  const int form = on && fb && data && end >= data + rex::filesystem::kSfdHevcTagBytes
                       ? FindTag(base, data, &id, &index)
                       : -1;
  if (trace && (call < 6 || call % 60 == 0) && data) {
    const uint8_t* p = Host(base, data);
    REXLOG_INFO("[hevc] Bink's video decode, call {}: frame buffers {:08X}, video data {:08X} to {:08X}, its first "
                "bytes {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X}; tag form {} (movie id {}, "
                "frame {})",
                call, fb, data, end, p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], form, id, index);
  }
  if (form < 0) {
    __imp__sub_82AFF648(ctx, base);  // a Bink frame: Bink decodes it
    Planes pl;
    if (trace && (call == 60 || call == 120) && fb && BinkPlanes(base, fb, &pl) && pl.w >= 640 && pl.h >= 368) {
      auto hex = [&](uint32_t at, uint32_t n) {
        std::string s;
        char b[4];
        for (uint32_t i = 0; i < n; ++i) {
          std::snprintf(b, sizeof(b), "%02X", *Host(base, at + i));
          s += b;
        }
        return s;
      };
      REXLOG_INFO("[hevc] Bink decoded call {} ({}x{}, pitch {} / {}): Y row 360 x 600-631 {}; cB row 180 x 300-315 "
                  "{}; cR row 180 x 300-315 {}",
                  call, pl.w, pl.h, pl.y_pitch, pl.c_pitch, hex(pl.y + 360 * pl.y_pitch + 600, 32),
                  hex(pl.u + 180 * pl.c_pitch + 300, 16), hex(pl.v + 180 * pl.c_pitch + 300, 16));
    }
    return;
  }
  // A tagged frame: Bink decodes nothing (its video data is zero but for the tag).
  ctx.r3.u64 = 0;
  g_tagged.fetch_add(1, std::memory_order_relaxed);
  Planes pl;
  if (!BinkPlanes(base, fb, &pl)) {
    if (g_failed.fetch_add(1, std::memory_order_relaxed) < 4)
      REXLOG_WARN("[hevc] movie id {} frame {}: Bink's frame buffers {:08X} are not as expected: the frame is not shown",
                  id, index, fb);
    return;
  }
#if RENUT_NXVIDEO
  std::lock_guard<std::mutex> lock(g_mutex);
  auto it = g_decoders.find(id);
  if (it == g_decoders.end()) {
    CloseIdle();
    auto movie = rex::filesystem::FindSfdHevcMovie(id);
    if (!movie) {
      if (g_missing.fetch_add(1, std::memory_order_relaxed) < 4)
        REXLOG_WARN("[hevc] a tagged frame of movie id {} that is not open", id);
      return;
    }
    it = g_decoders.emplace(id, Decoder{}).first;
    it->second.ahead = StartAhead(movie);
    it->second.movie = std::move(movie);
  }
  Decoder& dc = it->second;
  dc.used = std::chrono::steady_clock::now();
  const auto t0 = std::chrono::steady_clock::now();
  NxvPicture pic{};
  if (!DecodeTo(dc, index, &pic)) {
    if (g_failed.fetch_add(1, std::memory_order_relaxed) < 4)
      REXLOG_WARN("[hevc] {}: frame {} of {} was not decoded", dc.movie->name, index, dc.movie->frames.size());
    return;
  }
  const auto t1 = std::chrono::steady_clock::now();
  WritePicture(base, pic, pl);
  const auto t2 = std::chrono::steady_clock::now();
  dc.decode_ms += std::chrono::duration<double, std::milli>(t1 - t0).count();
  dc.copy_ms += std::chrono::duration<double, std::milli>(t2 - t1).count();
  ++dc.frames;
  const uint64_t shown = g_shown.fetch_add(1, std::memory_order_relaxed) + 1;
  static const bool log = REXCVAR_GET(blur_hevc_log);
  if (shown == 1 || (log && dc.frames % 300 == 0)) {
    uint64_t hits = 0, misses = 0;
    if (dc.ahead) {
      std::lock_guard<std::mutex> al(dc.ahead->m);
      hits = dc.ahead->hits;
      misses = dc.ahead->misses;
    }
    REXLOG_INFO("[hevc] {}: frame {} of {} shown from H.265 ({}x{} into Bink's buffers, pitch {} / {}); {} frames, "
                "{:.2f} ms decoding ({:.2f} taking the frames: {} from the read-ahead, {} read here; {:.2f} in "
                "nxv_decode) and {:.2f} ms copying a frame",
                dc.movie->name, index, dc.movie->frames.size(), pl.w, pl.h, pl.y_pitch, pl.c_pitch, dc.frames,
                dc.decode_ms / double(dc.frames), dc.read_ms / double(dc.frames), hits, misses,
                dc.nxv_ms / double(dc.frames), dc.copy_ms / double(dc.frames));
    if (log) NxvLine(dc);
  }
#else
  (void)index;
  if (g_missing.fetch_add(1, std::memory_order_relaxed) == 0)
    REXLOG_WARN("[hevc] movie id {}: an H.265 movie, and this build has no H.265 decoder (the Switch's nxvideo)", id);
#endif
}
