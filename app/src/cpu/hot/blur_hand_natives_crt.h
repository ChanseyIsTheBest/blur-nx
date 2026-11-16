// blur-nx (2026-11-16, the 15 ms batch; RR6's list of tw2de_nx's speed-ups, item 3, "game CRT on the host's libc"):
// EXACT hand-written natives of Blur's own two small C runtime routines, memset (sub_82953880) and memcpy
// (sub_82954218), checked by the guard. They are not the XDK's large memcpy / memset of banjo_hand_natives.h (those
// stay unwired here): 40 and 42 instructions, a byte loop to a word boundary, words (memset: four a pass), the last
// bytes. Both are members of hundreds of copies, so in front of each of those copies.
//
// Console profiles (2026-11-11, the rebased build; localized_bulk.txt): 82954218 538 samples, 82953880 515 summed over
// the profiled races, among the 50 hottest game functions. The nx race inlines both into their callers' copies, where
// each byte and word is a separate guest store (byte swap, bounds check). Here: the host's moves (banjo_hand_natives.h's
// overlapping 16/8/4/2-byte forms up to 128 bytes, memmove / memset above, out of line), one range check and one card
// range a call.
//
// Registers: what the generated functions write back (r0, ctr and cr0 are their locals), as they leave them.
//
// The DRAFT rule as the other files: included inside `namespace banjo::hand` after banjo_hand_natives.h's own helpers;
// no #include lines.

namespace blur_crt {

// The moves above 128 bytes (memcpy) / 64 bytes (memset), out of line: hundreds of copies inline the natives.
[[gnu::noinline]] inline void MoveLarge(uint8_t* d, const uint8_t* s, uint32_t n) { std::memmove(d, s, n); }
[[gnu::noinline]] inline void FillLarge(uint8_t* d, uint8_t v, uint32_t n) { std::memset(d, v, n); }

}  // namespace blur_crt

// sub_82953880 - memset(r3 = dst, r4 = the byte, r5 = n), returning dst in r3 (untouched). Bytes one at a time until dst
// is word-aligned (at most n), then 16 bytes a pass, the last 0-3 words, the last 0-3 bytes (r6 not advanced for those).
// Registers: r4 = its low byte in all four bytes of its low word (the upper word kept: two rlwimi), r5 = n - the head
// bytes, r6 = dst + every byte but the last 0-3.
// Declined: n >= 2^31 (the count runs in 32 bits), a range that wraps 4 GB or crosses the physical-view boundary.
namespace n_82953880 {

template <class S>
[[gnu::always_inline]] inline bool Native(PPCContext& __restrict ctx, uint8_t* base, [[maybe_unused]] S& s) {
  if (!On(kN82953880)) [[unlikely]] return false;
  const uint64_t d64 = ctx.r3.u64, n64 = ctx.r5.u64;
  if (n64 >= 0x80000000ull) [[unlikely]] return false;
  const uint32_t d = uint32_t(d64), n = uint32_t(n64);
  if (n != 0 && !detail::Straight(d, n)) [[unlikely]] return false;
  const uint32_t mis = d & 3u;
  const uint32_t head = mis ? (n < 4u - mis ? n : 4u - mis) : 0u;
  const uint32_t tail = (n - head) & 3u;
  const uint8_t v = ctx.r4.u8;
  s.Hand(kN82953880, n);
  if (n != 0) {
    if (!s.Note(d, n)) [[unlikely]] return true;
    ::banjo::hot::Card(base, d, n);
    uint8_t* p = ::banjo::hot::Raw(base, d);
    if (n <= 64u)
      detail::FillSmall(p, v, n);
    else
      blur_crt::FillLarge(p, v, n);
  }
  ctx.r4.u64 = (ctx.r4.u64 & 0xFFFFFFFF00000000ull) | (uint32_t(v) * 0x01010101u);
  ctx.r5.u64 = n64 - head;
  ctx.r6.u64 = d64 + uint64_t(n - tail);
  return true;
}

}  // namespace n_82953880

// sub_82954218 - memcpy(r3 = dst, r4 = src, r5 = n), forward: bytes until dst is word-aligned (at most n), then words
// (lwz / stw, or four lbz merged by rlwimi when the source is not word-aligned), the last 0-3 bytes.
// Registers: r4 = src + n, r6 = dst + n, r5 = n - the head bytes; with any words: r7 = the last word (big-endian, its
// upper word zero), and when the source after the head is not word-aligned r8 / r9 / r10 = that word's bytes 2 / 1 / 0.
// A forward copy is a memmove unless dst lies inside (src, src + n) (then it repeats what it already copied): declined.
// Declined too: n >= 2^31, ranges that wrap 4 GB or cross the physical-view boundary.
namespace n_82954218 {

template <class S>
[[gnu::always_inline]] inline bool Native(PPCContext& __restrict ctx, uint8_t* base, [[maybe_unused]] S& s) {
  if (!On(kN82954218)) [[unlikely]] return false;
  const uint64_t d64 = ctx.r3.u64, s64 = ctx.r4.u64, n64 = ctx.r5.u64;
  if (n64 >= 0x80000000ull) [[unlikely]] return false;
  const uint32_t d = uint32_t(d64), src = uint32_t(s64), n = uint32_t(n64);
  if (n != 0) {
    if (!detail::Straight(d, n) || !detail::Straight(src, n)) [[unlikely]] return false;
    if (uint32_t(d - src) - 1u < n - 1u) [[unlikely]] return false;  // src < d < src + n: a forward smear
  }
  const uint32_t mis = d & 3u;
  const uint32_t head = mis ? (n < 4u - mis ? n : 4u - mis) : 0u;
  const uint32_t words = (n - head) >> 2;
  s.Hand(kN82954218, n);
  // The last word's source bytes, read before the copy (the copy may overwrite them when dst < src).
  uint32_t last = 0;
  if (words != 0) {
    const uint8_t* q = ::banjo::hot::Raw(base, src + head + 4u * (words - 1u));
    last = uint32_t(q[0]) << 24 | uint32_t(q[1]) << 16 | uint32_t(q[2]) << 8 | uint32_t(q[3]);
  }
  if (n != 0) {
    if (!s.Note(d, n)) [[unlikely]] return true;
    ::banjo::hot::Card(base, d, n);
    uint8_t* dp = ::banjo::hot::Raw(base, d);
    const uint8_t* sp = ::banjo::hot::Raw(base, src);
    if (n <= 64u)
      detail::CopySmall(dp, sp, n);
    else if (n <= 128u)
      detail::CopyMedium(dp, sp, n);
    else
      blur_crt::MoveLarge(dp, sp, n);
  }
  ctx.r4.u64 = s64 + n64;
  ctx.r5.u64 = n64 - head;
  ctx.r6.u64 = d64 + n64;
  if (words != 0) {
    ctx.r7.u64 = last;
    if ((src + head) & 3u) {
      ctx.r8.u64 = (last >> 8) & 0xFFu;
      ctx.r9.u64 = (last >> 16) & 0xFFu;
      ctx.r10.u64 = last >> 24;
    }
  }
  return true;
}

}  // namespace n_82954218
