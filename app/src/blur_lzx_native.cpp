// blur-nx (2026-11-13, the user: "add the LZX native to the 24-hour work"; M-29 of docs/cpu-moonshots-50-1113.md): Blur's
// LZX buffer decompression (XMemDecompress with a buffer context: sub_829CC028) as a host native.
//
// The guest decoder is Microsoft's (the xcompress library: sub_829CD598 frames the buffer, sub_829CDF48 decodes a frame,
// sub_829D0300 is decode_data with its block, tree and bit-reading helpers; read 2026-11-13). In races the game's job
// workers (JQ Worker 1 and 2) spend 4-10 % of their samples in it (the streamed resources, ~1.5-2 ms a frame). The
// base's and the installer's decoder is libmspack's lzxd (the XEX's LZX), whose framing differs from Microsoft's in ways
// that change bytes (an odd uncompressed block's pad byte, one continuous bit stream instead of a restart at every
// frame, the E8 rule at an absolute offset of 0, its errors), so this is a decoder of Microsoft's framing written from
// the guest's own code, with libmspack's standard parts (the trees, the matches):
//   - a buffer context is reset at every call (sub_829CD510): R0-R2 = 1, the previous code lengths 0, the E8 header
//     read with the first block; the call's output depends on its input alone, and no game code reads the context
//     (the callers read the HRESULT and *pDstSize only), so the native keeps its state on the host;
//   - frames: 0xFF + BE16 uncompressed + BE16 compressed (5 bytes, the last frame) or BE16 compressed (2 bytes, 32 KB);
//     the remaining count starts at srcSize - 5 and the loop ends when it reaches 0 exactly (sub_829CD598);
//   - the bit reader: a 32-bit buffer of 16-bit little-endian words, restarted at every frame (two words, count 16;
//     not inside an uncompressed block), refilled by one or two words when the count drops to 0 or below, an error flag
//     past the frame's end (payload + compressed size + 4) (sub_829D0570 / 829D05D8 / 829D06A8);
//   - an uncompressed block backs the input up 2 bytes, reads R0-R2 as 12 little-endian bytes, copies its bytes, and the
//     next block header restarts the bit buffer where they end (no pad byte) (sub_829D23E8, 829D2200, 829D0300);
//   - E8 translation on each frame's output copy, Microsoft's rules (sub_829D0168, sub_829D3318).
// Anything the native does not decode exactly as the guest (an error, an empty or malformed tree, a match past the
// end of its block, a stream context, another window size) falls back to the guest call for the whole buffer, which
// then writes its own result; the native writes the destination only after the whole buffer decoded.
//
// cpu_lzx_native: 0 off (the guest decoder), 1 the native, 2 verify (PC: the native decodes into a host buffer, then
// the guest call runs and the two outputs, sizes and results are compared; the guest's stay). Every 10 s the log line
// "[lzx] ..." counts the calls, the bytes, the fallbacks by reason and (2) the differences.

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <fmt/format.h>
#include <rex/hash.h>  // blur-nx (2026-11-13, #67): XXH3

#include "cpu/banjo_hot_common.h"

REXCVAR_DEFINE_INT32(cpu_lzx_native, 0, "CPU",
                     "Blur's LZX buffer decompression (XMemDecompress, sub_829CC028) as a host native: 0 off, 1 on, "
                     "2 verify (both run, compared; the guest's output stays)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
// blur-nx (2026-11-13, #67 of docs/cpu-near-exact-50-1113.md's second wave; exact): the decoded buffers kept, keyed by
// their compressed bytes (an XXH3 of them, a hit checked byte for byte against a kept copy, with the window size and
// slot count: a buffer context is reset every call, so nothing else decides the output). The 10 s line counts the
// repeats either way (a buffer whose bytes were decoded before among the last 1,024).
REXCVAR_DEFINE_INT32(cpu_lzx_cache_kb, 0, "CPU",
                     "cpu_lzx_native: kilobytes of decoded LZX buffers kept for a buffer decoded again (its compressed "
                     "bytes compared in full; exact). 0: none (the repeats are still counted)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

REX_EXTERN(__imp__sub_829CC028);

namespace {

using u8 = uint8_t;
using u32 = uint32_t;

// The census (the 10 s line).
std::atomic<uint64_t> g_calls{0}, g_native{0}, g_bytes_in{0}, g_bytes_out{0}, g_stream{0}, g_verified{0}, g_differ{0};
std::atomic<uint64_t> g_repeats{0}, g_repeat_bytes{0}, g_cache_hits{0};  // blur-nx (2026-11-13, #67)
std::atomic<uint64_t> g_fallback[8];  // by Fail reason
std::atomic<int64_t> g_last_report{0};
const char* const kFailNames[8] = {"stream context", "window size", "error flag", "tree", "match past its block",
                                   "uncompressed block", "block type", "framing"};
enum Fail : int { kOk = -1, kStream = 0, kWindow, kErrorFlag, kTree, kMatchEnd, kUncompressed, kBlockType, kFraming };

inline u32 Be32(const u8* p) { return (u32(p[0]) << 24) | (u32(p[1]) << 16) | (u32(p[2]) << 8) | p[3]; }
inline u32 Le32(const u8* p) { return u32(p[0]) | (u32(p[1]) << 8) | (u32(p[2]) << 16) | (u32(p[3]) << 24); }

constexpr int kMaxSlots = 51;
constexpr int kMainMax = 256 + kMaxSlots * 8;
constexpr int kLenSyms = 249;

// A canonical Huffman decoder: a direct table of kTableBits, then the longer codes by length.
struct Huffman {
  static constexpr int kTableBits = 11;
  uint16_t table[1 << kTableBits];  // (symbol << 5) | length, 0 = a longer code
  uint16_t sorted[kMainMax];
  int first_code[17], first_index[17], count[17];
  bool empty = true;
  // false: a malformed tree (the native falls back; an empty tree is valid until a symbol is asked of it).
  bool Build(const u8* lens, int n) {
    std::memset(count, 0, sizeof(count));
    for (int i = 0; i < n; ++i) {
      if (lens[i] > 16) return false;
      ++count[lens[i]];
    }
    count[0] = 0;
    int total = 0;
    for (int l = 1; l <= 16; ++l) total += count[l];
    empty = total == 0;
    std::memset(table, 0, sizeof(table));
    if (empty) return true;
    // Completeness (Kraft sum exactly 1; a single code of length 1 is the one incomplete tree LZX encoders write).
    int64_t left = 1;
    for (int l = 1; l <= 16; ++l) {
      left <<= 1;
      left -= count[l];
      if (left < 0) return false;
    }
    if (left != 0 && !(total == 1)) return false;
    int code = 0, index = 0;
    for (int l = 1; l <= 16; ++l) {
      code = (code + (l > 1 ? count[l - 1] : 0)) << (l > 1 ? 1 : 0);
      if (l == 1) code = 0;
      first_code[l] = code;
      first_index[l] = index;
      index += count[l];
    }
    // The symbols in canonical order.
    int next[17];
    for (int l = 1; l <= 16; ++l) next[l] = first_index[l];
    for (int i = 0; i < n; ++i)
      if (lens[i]) sorted[next[lens[i]]++] = uint16_t(i);
    // The direct table.
    for (int l = 1; l <= kTableBits; ++l) {
      for (int k = 0; k < count[l]; ++k) {
        const int sym = sorted[first_index[l] + k];
        const int c = first_code[l] + k;
        const int lo = c << (kTableBits - l), hi = (c + 1) << (kTableBits - l);
        for (int e = lo; e < hi; ++e) table[e] = uint16_t((sym << 5) | l);
      }
    }
    return true;
  }
};

struct Decoder {
  // The window (W bytes) and the decode state of one call.
  std::vector<u8> window;
  u32 wsize = 0, wmask = 0, pos = 0;
  u32 r0 = 1, r1 = 1, r2 = 1;
  int slots = 0;
  u8 main_len[kMainMax], prev_main[kMainMax], len_len[kLenSyms], prev_len[kLenSyms], aligned_len[8];
  Huffman main_tree, len_tree, aligned_tree, pre_tree;
  int block_type = 0;  // 0 none, 1 verbatim, 2 aligned, 3 uncompressed
  int32_t block_remaining = 0;
  bool new_block = true, first_block = true;
  u32 e8_size = 0, e8_pos = 0, frames = 0;
  // The bit reader (Microsoft's).
  const u8* in = nullptr;
  const u8* in_end = nullptr;
  u32 bitbuf = 0;
  int bitcount = 0;
  bool error = false;
  // blur-nx (2026-11-13, the verify race's 6 % fallbacks): the block decoders (sub_829D2E80 / sub_829D2720) refill
  // inline with no end check at all; their one check is the input pointer against the end before each main symbol is
  // taken. Off inside Compressed(), where that check is made instead (the refill then reads what the guest's reads).
  bool check_end = true;
  u32 extra_bits[kMaxSlots], pbase[kMaxSlots];

  void Reset(u32 w, int s) {
    if (window.size() != w) window.assign(w, 0);
    wsize = w;
    wmask = w - 1;
    slots = s;
    pos = 0;
    r0 = r1 = r2 = 1;
    std::memset(prev_main, 0, sizeof(prev_main));
    std::memset(prev_len, 0, sizeof(prev_len));
    std::memset(main_len, 0, sizeof(main_len));
    std::memset(len_len, 0, sizeof(len_len));
    block_type = 0;
    block_remaining = 0;
    new_block = true;
    first_block = true;
    e8_size = e8_pos = frames = 0;
    error = false;
    for (int i = 0, b = 0; i < kMaxSlots; ++i) {
      extra_bits[i] = i < 4 ? 0 : std::min<u32>(u32(i - 2) >> 1, 17);
      pbase[i] = u32(b);
      b += 1 << extra_bits[i];
    }
  }
  // sub_829D0570: the bit buffer from the input (not in an uncompressed block; not with fewer than 4 bytes left).
  void InitBits() {
    if (block_type == 3) return;
    if (in + 4 > in_end) return;
    bitbuf = (u32(in[1]) << 24) | (u32(in[0]) << 16) | (u32(in[3]) << 8) | u32(in[2]);
    bitcount = 16;
    in += 4;
  }
  // sub_829D05D8: consume n bits, refill one or two 16-bit words when the count drops to 0 or below.
  inline void Fill(int n) {
    bitcount -= n;
    bitbuf <<= n;
    if (bitcount > 0) return;
    if (check_end && in >= in_end) {
      error = true;
      return;
    }
    bitbuf |= (u32(in[0]) | (u32(in[1]) << 8)) << (-bitcount);
    in += 2;
    bitcount += 16;
    if (bitcount > 0) return;
    if (check_end && in >= in_end) {
      error = true;
      return;
    }
    bitbuf |= (u32(in[0]) | (u32(in[1]) << 8)) << (-bitcount);
    in += 2;
    bitcount += 16;
  }
  inline u32 Get(int n) {
    const u32 v = n ? bitbuf >> (32 - n) : 0;
    Fill(n);
    return v;
  }
  // A symbol of a tree; -1 when the tree cannot give one.
  inline int Symbol(const Huffman& h) {
    if (h.empty) return -1;
    const u32 e = h.table[bitbuf >> (32 - Huffman::kTableBits)];
    if (e) {
      Fill(int(e & 31));
      return int(e >> 5);
    }
    for (int l = Huffman::kTableBits + 1; l <= 16; ++l) {
      const int c = int(bitbuf >> (32 - l));
      const int k = c - h.first_code[l];
      if (k >= 0 && k < h.count[l]) {
        Fill(l);
        return h.sorted[h.first_index[l] + k];
      }
    }
    return -1;
  }
  // sub_829D33C8: code lengths through the pretree, as deltas from the previous ones (LZX's rules).
  Fail ReadLengths(u8* lens, const u8* prev, int first, int last) {
    u8 pre_lens[20];
    for (int i = 0; i < 20; ++i) pre_lens[i] = u8(Get(4));
    if (!pre_tree.Build(pre_lens, 20)) return kTree;
    for (int x = first; x < last;) {
      int z = Symbol(pre_tree);
      if (z < 0) return kTree;
      // (a run past the tree's end: the native does not guess what the guest does; it falls back)
      if (z == 17) {
        int y = int(Get(4)) + 4;
        if (x + y > last) return kTree;
        while (y--) lens[x++] = 0;
      } else if (z == 18) {
        int y = int(Get(5)) + 20;
        if (x + y > last) return kTree;
        while (y--) lens[x++] = 0;
      } else if (z == 19) {
        int y = int(Get(1)) + 4;
        if (x + y > last) return kTree;
        z = Symbol(pre_tree);
        if (z < 0 || z > 16) return kTree;
        int v = int(prev[x]) - z;
        if (v < 0) v += 17;
        while (y--) lens[x++] = u8(v);
      } else {
        int v = int(prev[x]) - z;
        if (v < 0) v += 17;
        lens[x++] = u8(v);
      }
      if (error) return kErrorFlag;
    }
    return kOk;
  }
  // The block header and its trees (sub_829D0300's new-block part).
  Fail NewBlock() {
    if (first_block) {
      first_block = false;
      if (Get(1)) {
        const u32 hi = Get(16), lo = Get(16);
        e8_size = (hi << 16) | lo;
      } else {
        e8_size = 0;
      }
    }
    if (block_type == 3) {
      block_type = 0;
      InitBits();
    }
    block_type = int(Get(3));
    const u32 a = Get(8), b = Get(8), c = Get(8);
    block_remaining = int32_t((a << 16) | (b << 8) | c);
    if (block_type == 2) {
      for (int i = 0; i < 8; ++i) aligned_len[i] = u8(Get(3));
      if (error) return kErrorFlag;
      if (!aligned_tree.Build(aligned_len, 8)) return kTree;
    }
    if (block_type == 1 || block_type == 2) {
      const int main_syms = 256 + slots * 8;
      std::memcpy(prev_main, main_len, size_t(main_syms));
      std::memcpy(prev_len, len_len, kLenSyms);
      Fail f = ReadLengths(main_len, prev_main, 0, 256);
      if (f != kOk) return f;
      f = ReadLengths(main_len, prev_main, 256, main_syms);
      if (f != kOk) return f;
      if (!main_tree.Build(main_len, main_syms)) return kTree;
      f = ReadLengths(len_len, prev_len, 0, kLenSyms);
      if (f != kOk) return f;
      if (!len_tree.Build(len_len, kLenSyms)) return kTree;
    } else if (block_type == 3) {
      // sub_829D23E8: back up 2 bytes, R0-R2 as 12 little-endian bytes.
      in -= 2;
      if (in + 4 >= in_end) return kUncompressed;
      r0 = Le32(in);
      r1 = Le32(in + 4);
      r2 = Le32(in + 8);
      in += 12;
    } else {
      return kBlockType;
    }
    if (error) return kErrorFlag;
    new_block = false;
    return kOk;
  }
  // n bytes of the current block from window[pos] (verbatim / aligned); a match may not run past them.
  Fail Compressed(u32 n) {
    struct Unchecked {  // blur-nx (2026-11-13): the block's refills without the end check (check_end)
      Decoder& d;
      explicit Unchecked(Decoder& x) : d(x) { d.check_end = false; }
      ~Unchecked() { d.check_end = true; }
    } unchecked(*this);
    const u32 end = pos + n;
    const bool aligned = block_type == 2;
    while (pos < end) {
      // The guest's one check: the input pointer against the end, before the main symbol is taken (its table
      // look-up reads only the bit buffer); past it, a failure (the native falls back and the guest fails the same).
      if (in >= in_end || error) return kErrorFlag;
      const int sym = Symbol(main_tree);
      if (sym < 0) return kTree;
      if (sym < 256) {
        window[pos++ & wmask] = u8(sym);
        continue;
      }
      const int m = sym - 256;
      u32 len = u32(m & 7);
      const int slot = m >> 3;
      if (len == 7) {
        const int ls = Symbol(len_tree);
        if (ls < 0) return kTree;
        len += u32(ls);
      }
      len += 2;
      u32 off;
      if (slot == 0) {
        off = r0;
      } else if (slot == 1) {
        off = r1;
        r1 = r0;
        r0 = off;
      } else if (slot == 2) {
        off = r2;
        r2 = r0;
        r0 = off;
      } else {
        const u32 extra = extra_bits[slot];
        off = pbase[slot] - 2;
        if (aligned && extra >= 3) {
          if (extra > 3) off += Get(int(extra - 3)) << 3;
          const int a = Symbol(aligned_tree);
          if (a < 0) return kTree;
          off += u32(a);
        } else if (extra) {
          off += Get(int(extra));
        }
        r2 = r1;
        r1 = r0;
        r0 = off;
      }
      if (error) return kErrorFlag;
      if (pos + len > end) return kMatchEnd;
      // LZ copy (byte by byte when the ranges overlap or wrap).
      u32 from = (pos - off) & wmask;
      u32 to = pos & wmask;
      if (off >= len && from + len <= wsize && to + len <= wsize) {
        std::memcpy(&window[to], &window[from], len);
      } else {
        for (u32 i = 0; i < len; ++i) window[(to + i) & wmask] = window[(from + i) & wmask];
      }
      pos += len;
    }
    pos &= wmask;
    return kOk;
  }
  // sub_829D2200: n bytes of an uncompressed block (as many as the frame holds; fewer: an error).
  Fail Uncompressed(u32 n) {
    const intptr_t avail = in_end - in;
    if (intptr_t(n) > avail) return kUncompressed;
    for (u32 i = 0; i < n; ++i) window[(pos + i) & wmask] = in[i];
    in += n;
    pos = (pos + n) & wmask;
    return kOk;
  }
  // sub_829D0300: 'amount' bytes into the window; the frame's output after it.
  Fail DecodeData(u32 amount, u32& done) {
    done = 0;
    int32_t left = int32_t(amount);
    while (left > 0) {
      if (new_block) {
        const Fail f = NewBlock();
        if (f != kOk) return f;
      }
      while (block_remaining > 0 && left > 0) {
        const u32 n = u32(std::min(block_remaining, left));
        const Fail f = block_type == 3 ? Uncompressed(n) : (block_type == 1 || block_type == 2) ? Compressed(n)
                                                                                                : kBlockType;
        if (f != kOk) return f;
        left -= int32_t(n);
        done += n;
        block_remaining -= int32_t(n);
      }
      if (block_remaining == 0) new_block = true;
      if (left == 0) InitBits();  // (the guest re-reads the bit buffer here; the next frame starts its own)
    }
    return kOk;
  }
  // sub_829D0168: E8 translation of a frame's output copy, Microsoft's rules.
  void E8(u8* buf, u32 n) {
    if (n <= 6) {
      e8_pos += n;
      return;
    }
    u8 saved[6];
    std::memcpy(saved, buf + n - 6, 6);
    std::memset(buf + n - 6, 0xE8, 6);
    const u32 end = e8_pos + n - 10;
    u8* p = buf;
    for (;;) {
      u32 skipped = 0;
      while (*p != 0xE8) {
        ++p;
        ++skipped;
      }
      e8_pos += skipped;
      const u32 cur = e8_pos;
      u8* q = p + 1;
      if (cur >= end) break;
      const u32 abs = Le32(q);
      u32 rel;
      bool write = false;
      if (abs < e8_size) {
        rel = abs - cur;
        write = true;
      } else if (u32(0u - abs) <= cur) {
        rel = abs + e8_size;
        write = true;
      }
      if (write) {
        q[0] = u8(rel);
        q[1] = u8(rel >> 8);
        q[2] = u8(rel >> 16);
        q[3] = u8(rel >> 24);
      }
      e8_pos = cur + 5;
      p = q + 4;
    }
    e8_pos = end + 10;
    std::memcpy(buf + n - 6, saved, 6);
  }
};

thread_local Decoder t_decoder;
thread_local std::vector<u8> t_out;

// blur-nx (2026-11-13, #67): the repeat census (the last 1,024 buffers' keys) and the cache (least recently used out).
struct CacheEntry {
  uint64_t key;
  std::vector<u8> in;   // the compressed bytes, compared on a hit
  std::vector<u8> out;  // the decoded buffer
};
std::mutex g_cache_mutex;
uint64_t g_recent[1024];
u32 g_recent_at = 0;
std::list<std::shared_ptr<const CacheEntry>> g_lru;
std::unordered_map<uint64_t, std::list<std::shared_ptr<const CacheEntry>>::iterator> g_cache;
size_t g_cache_bytes = 0;

uint64_t BufferKey(const u8* src, u32 size, u32 w, int slots) {
  return XXH3_64bits_withSeed(src, size, (uint64_t(w) << 8) ^ uint64_t(slots) ^ (uint64_t(size) << 32));
}

// Notes the key (the census); the kept buffer for these exact bytes, if any (moved to the front).
std::shared_ptr<const CacheEntry> CacheLook(uint64_t key, const u8* src, u32 size, size_t budget) {
  std::lock_guard<std::mutex> lock(g_cache_mutex);
  bool seen = false;
  for (const uint64_t k : g_recent)
    if (k == key) {
      seen = true;
      break;
    }
  if (seen) {
    g_repeats.fetch_add(1, std::memory_order_relaxed);
    g_repeat_bytes.fetch_add(size, std::memory_order_relaxed);
  } else {
    g_recent[g_recent_at++ & 1023] = key;
  }
  if (!budget) return nullptr;
  const auto it = g_cache.find(key);
  if (it == g_cache.end()) return nullptr;
  const std::shared_ptr<const CacheEntry> e = *it->second;
  if (e->in.size() != size || std::memcmp(e->in.data(), src, size) != 0) return nullptr;
  g_lru.splice(g_lru.begin(), g_lru, it->second);
  return e;
}

void CacheKeep(uint64_t key, const u8* src, u32 size, const std::vector<u8>& out, size_t budget) {
  const size_t bytes = size_t(size) + out.size();
  if (!budget || bytes > budget / 4) return;
  auto e = std::make_shared<CacheEntry>();
  e->key = key;
  e->in.assign(src, src + size);
  e->out = out;
  std::lock_guard<std::mutex> lock(g_cache_mutex);
  if (g_cache.count(key)) return;
  while (g_cache_bytes + bytes > budget && !g_lru.empty()) {
    const auto& last = g_lru.back();
    g_cache_bytes -= last->in.size() + last->out.size();
    g_cache.erase(last->key);
    g_lru.pop_back();
  }
  g_lru.push_front(std::move(e));
  g_cache[key] = g_lru.begin();
  g_cache_bytes += bytes;
}

// The whole buffer (sub_829CD598's framing); the output in t_out. kOk, or why the guest must decode it.
Fail DecodeBuffer(uint8_t* base, u32 ctx, u32 src, u32 src_size) {
  const u8* c = ::banjo::hot::Raw(base, ctx);
  if (Be32(c + 8) & 1) return kStream;
  const u8* s = c + 20;
  const u32 w = Be32(s + 4);
  const int slots = s[11957];
  if (w < 0x8000 || w > 0x200000 || (w & (w - 1)) || slots < 30 || slots > kMaxSlots) return kWindow;
  Decoder& d = t_decoder;
  d.Reset(w, slots);
  t_out.clear();
  u32 remaining = src_size > 5 ? src_size - 5 : 0;
  const u8* p = ::banjo::hot::Raw(base, src);
  while (remaining != 0) {
    u32 comp, uncomp;
    // (the guest's count wraps where it would run short, and it then reads past the buffer: the native falls back)
    if (p[0] == 0xFF) {
      if (remaining < 5) return kFraming;
      remaining -= 5;
      uncomp = (u32(p[1]) << 8) | p[2];
      comp = (u32(p[3]) << 8) | p[4];
      p += 5;
      if (comp < remaining) remaining = comp;
    } else {
      if (remaining < 2) return kFraming;
      comp = (u32(p[0]) << 8) | p[1];
      uncomp = 0x8000;
      p += 2;
      remaining -= 2;
    }
    if (comp > remaining || uncomp > 0x8000 || uncomp == 0) return kFraming;
    // sub_829CDF48: the frame.
    d.in = p;
    d.in_end = p + comp + 4;
    d.InitBits();
    u32 done = 0;
    const Fail f = d.DecodeData(uncomp, done);
    if (f != kOk) return f;
    if (d.error) return kErrorFlag;
    // The output copy (sub_829D3318): the frame's bytes end at pos (W when pos is 0), E8 on the copy.
    const u32 last = d.pos ? d.pos : d.wsize;
    if (done > last) return kFraming;
    const size_t at = t_out.size();
    t_out.resize(at + done);
    std::memcpy(t_out.data() + at, &d.window[last - done], done);
    if (d.e8_size != 0 && d.frames < 32768) d.E8(t_out.data() + at, done);
    ++d.frames;
    remaining -= comp;
    p += comp;
  }
  return kOk;
}

void Report() {
  const int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
                          std::chrono::steady_clock::now().time_since_epoch()).count();
  int64_t last = g_last_report.load(std::memory_order_relaxed);
  if (last == 0) {
    g_last_report.compare_exchange_strong(last, now);
    return;
  }
  if (now - last < 10 || !g_last_report.compare_exchange_strong(last, now)) return;
  std::string fails;
  for (int i = 0; i < 8; ++i) {
    const uint64_t n = g_fallback[i].exchange(0, std::memory_order_relaxed);
    if (n) fails += fmt::format(", {} {}", kFailNames[i], n);
  }
  REXLOG_INFO("[lzx] {} s: {} buffer calls ({} native), {:.1f} KB in, {:.1f} KB out, {} stream calls (guest); "
              "verify {} compared, {} differ; fallbacks{}; #67: {} buffers decoded before ({:.1f} KB in), {} from the "
              "cache",
              now - last, g_calls.exchange(0), g_native.exchange(0), double(g_bytes_in.exchange(0)) / 1024.0,
              double(g_bytes_out.exchange(0)) / 1024.0, g_stream.exchange(0), g_verified.exchange(0),
              g_differ.load(), fails.empty() ? std::string(": none") : fails, g_repeats.exchange(0),
              double(g_repeat_bytes.exchange(0)) / 1024.0, g_cache_hits.exchange(0));
}

}  // namespace

// XMemDecompress's LZX codec (r3 = context, r4 = dst, r5 = pDstSize, r6 = src, r7 = srcSize; r3 = HRESULT).
REX_HOOK_RAW(sub_829CC028) {
  static const int mode = REXCVAR_GET(cpu_lzx_native);
  const u32 ctx_va = ctx.r3.u32, dst = ctx.r4.u32, p_size = ctx.r5.u32, src = ctx.r6.u32, src_size = ctx.r7.u32;
  if (mode == 0 || src_size == 0) {
    __imp__sub_829CC028(ctx, base);
    return;
  }
  Report();
  // blur-nx (2026-11-13, #67): the cache (and the repeat census) by the compressed bytes, the window and the slots.
  static const size_t budget = size_t(std::clamp(REXCVAR_GET(cpu_lzx_cache_kb), 0, 262144)) * 1024;
  std::shared_ptr<const CacheEntry> hit;
  uint64_t key = 0;
  const u8* const src_bytes = ::banjo::hot::Raw(base, src);
  {
    const u8* c = ::banjo::hot::Raw(base, ctx_va);
    if (!(Be32(c + 8) & 1)) {  // a buffer context (a stream one goes to the guest below)
      key = BufferKey(src_bytes, src_size, Be32(c + 24), c[20 + 11957]);
      hit = CacheLook(key, src_bytes, src_size, budget);
    }
  }
  if (hit && mode == 1) {
    g_calls.fetch_add(1, std::memory_order_relaxed);
    g_bytes_in.fetch_add(src_size, std::memory_order_relaxed);
    g_cache_hits.fetch_add(1, std::memory_order_relaxed);
    t_out = hit->out;
  }
  const Fail f = hit && mode == 1 ? kOk : DecodeBuffer(base, ctx_va, src, src_size);
  if (f == kStream) {
    g_stream.fetch_add(1, std::memory_order_relaxed);
    __imp__sub_829CC028(ctx, base);
    return;
  }
  if (!(hit && mode == 1)) {
    g_calls.fetch_add(1, std::memory_order_relaxed);
    g_bytes_in.fetch_add(src_size, std::memory_order_relaxed);
  }
  if (f != kOk) {
    g_fallback[f].fetch_add(1, std::memory_order_relaxed);
    __imp__sub_829CC028(ctx, base);
    return;
  }
  const u32 total = u32(t_out.size());
  g_bytes_out.fetch_add(total, std::memory_order_relaxed);
  if (!hit && key) CacheKeep(key, src_bytes, src_size, t_out, budget);  // blur-nx (2026-11-13, #67)
  if (hit && mode == 2 && hit->out != t_out) {  // the verify: a kept buffer against a fresh decode of the same bytes
    g_differ.fetch_add(1, std::memory_order_relaxed);
    REXLOG_WARN("[lzx] verify: the cache's buffer for {} bytes in differs from the decode", src_size);
  }
  if (mode == 2) {  // verify: the guest's result stays
    __imp__sub_829CC028(ctx, base);
    const u32 guest_total = Be32(::banjo::hot::Raw(base, p_size));
    const u8* guest_out = ::banjo::hot::Raw(base, dst);
    g_verified.fetch_add(1, std::memory_order_relaxed);
    const bool same = ctx.r3.u32 == 0 && guest_total == total && std::memcmp(guest_out, t_out.data(), total) == 0;
    if (!same) {
      const uint64_t k = g_differ.fetch_add(1, std::memory_order_relaxed);
      if (k < 16) {
        u32 at = 0;
        while (at < std::min(total, guest_total) && guest_out[at] == t_out[at]) ++at;
        REXLOG_WARN("[lzx] verify: {} bytes in, the guest {} bytes (hr {:08X}), the native {} bytes; first difference "
                    "at {} (frame {})",
                    src_size, guest_total, ctx.r3.u32, total, at, at / 0x8000);
      }
    }
    return;
  }
  g_native.fetch_add(1, std::memory_order_relaxed);
  if (total) {
    std::memcpy(::banjo::hot::Raw(base, dst), t_out.data(), total);
    ::banjo::hot::Card(base, dst, total);
  }
  u8* ps = ::banjo::hot::Raw(base, p_size);
  ps[0] = u8(total >> 24);
  ps[1] = u8(total >> 16);
  ps[2] = u8(total >> 8);
  ps[3] = u8(total);
  ::banjo::hot::Card(base, p_size, 4);
  ctx.r3.u64 = 0;  // S_OK
}
