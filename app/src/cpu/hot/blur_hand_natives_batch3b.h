// blur-nx (2026-11-11): hand-written natives, batch 3b (the user's request: 30 natives from the console race profile,
// 2026-11-11 15:18, rebased build 46b3f6d6; hand_batch3b.draft.txt and docs/hand-natives-batch3b.md say which and
// why). DRAFT: included inside `namespace banjo::hand` after banjo_hand_natives.h's own helpers; no #include lines. Its
// helpers are in blur_b3b (nothing is taken from another batch's namespace, so the include order does not matter).
//
// Keys the main session has to add (enum values and keys), one per native below:
//   kN8230A4D8 kN822D92D8 kN822D9040 kN822D79A8
#pragma once

namespace blur_b3b {

// A big-endian guest byte / halfword / word through its own host pointer (as the generated loads and stores).
[[gnu::always_inline]] inline uint32_t LdW(uint8_t* base, uint32_t ea) {
  const uint8_t* p = ::banjo::hot::Raw(base, ea);
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | uint32_t(p[3]);
}
[[gnu::always_inline]] inline uint32_t LdH(uint8_t* base, uint32_t ea) {
  const uint8_t* p = ::banjo::hot::Raw(base, ea);
  return uint32_t(p[0]) << 8 | uint32_t(p[1]);
}
[[gnu::always_inline]] inline uint32_t LdB(uint8_t* base, uint32_t ea) { return *::banjo::hot::Raw(base, ea); }
[[gnu::always_inline]] inline void StW(uint8_t* base, uint32_t ea, uint32_t w) {
  uint8_t* p = ::banjo::hot::Raw(base, ea);
  p[0] = uint8_t(w >> 24);
  p[1] = uint8_t(w >> 16);
  p[2] = uint8_t(w >> 8);
  p[3] = uint8_t(w);
}
[[gnu::always_inline]] inline void StD(uint8_t* base, uint32_t ea, uint64_t d) {
  StW(base, ea, uint32_t(d >> 32));
  StW(base, ea + 4u, uint32_t(d));
}
// blur-nx (2026-11-16, the 15 ms batch's PC guard run): a stwu's back-chain word, as the generated code stores it: not at
// all under REX_BACK_CHAIN_ELIDE (#145: tools/frame_traffic.py marks these four functions' chain stores REX_CHAIN_STORE,
// nothing reads them). Written unconditionally before, the guard found the word the original no longer writes.
[[gnu::always_inline]] inline void ChainW(uint8_t* base, uint32_t ea, uint32_t sp) {
#if !defined(REX_BACK_CHAIN_ELIDE)
  StW(base, ea, sp);
#else
  (void)base;
  (void)ea;
  (void)sp;
#endif
}
// Sets the low word of a 64-bit register image only (the generated `reg.u32 = x` leaves the upper word).
[[gnu::always_inline]] inline uint64_t SetLo(uint64_t r, uint32_t v) { return (r & 0xFFFFFFFF00000000ull) | v; }
// An int16 as the generated scalar path leaves it: double(float(v)) (exact) and v * 2^-15 as a single in a double.
[[gnu::always_inline]] inline double Fs(int32_t v) { return double(float(v)); }
[[gnu::always_inline]] inline double Fm(int32_t v) { return double(float(v) * 0x1p-15f); }
// n big-endian int16 at `in` (any alignment) -> n big-endian floats v * 2^-15 at `out` (host pointers, no overlap).
[[gnu::always_inline]] inline void CvtI16F32(const uint8_t* in, uint8_t* out, uint32_t n) {
  const simde__m128i swap16 = simde_mm_set_epi8(14, 15, 12, 13, 10, 11, 8, 9, 6, 7, 4, 5, 2, 3, 0, 1);
  const simde__m128i swap32 = simde_mm_set_epi8(12, 13, 14, 15, 8, 9, 10, 11, 4, 5, 6, 7, 0, 1, 2, 3);
  const simde__m128 k = simde_mm_set1_ps(0x1p-15f);
  uint32_t i = 0;
  for (; i + 8u <= n; i += 8u) {
    const simde__m128i v = simde_mm_shuffle_epi8(simde_mm_loadu_si128((const simde__m128i*)(in + 2u * i)), swap16);
    const simde__m128 a = simde_mm_mul_ps(simde_mm_cvtepi32_ps(simde_mm_cvtepi16_epi32(v)), k);
    const simde__m128 b =
        simde_mm_mul_ps(simde_mm_cvtepi32_ps(simde_mm_cvtepi16_epi32(simde_mm_unpackhi_epi64(v, v))), k);
    simde_mm_storeu_si128((simde__m128i*)(out + 4u * i), simde_mm_shuffle_epi8(simde_mm_castps_si128(a), swap32));
    simde_mm_storeu_si128((simde__m128i*)(out + 4u * i + 16u),
                          simde_mm_shuffle_epi8(simde_mm_castps_si128(b), swap32));
  }
  for (; i < n; ++i) {
    const int16_t v = int16_t(uint16_t(in[2u * i]) << 8 | in[2u * i + 1u]);
    const float f = float(v) * 0x1p-15f;
    uint32_t w;
    std::memcpy(&w, &f, 4);
    out[4u * i] = uint8_t(w >> 24);
    out[4u * i + 1u] = uint8_t(w >> 16);
    out[4u * i + 2u] = uint8_t(w >> 8);
    out[4u * i + 3u] = uint8_t(w);
  }
}
// [a, a + an) and [b, b + bn) share a byte (neither wraps 4 GB: the caller has checked Straight).
[[gnu::always_inline]] inline bool Hit(uint32_t a, uint32_t an, uint32_t b, uint32_t bn) {
  return uint32_t(a - b) < bn || uint32_t(b - a) < an;
}

}  // namespace blur_b3b

// sub_8230A4D8 - clears or moves the blocks of a 1 KB-block buffer: r3 = the object, r4 = a second object (read only
// at +0 and +28). obj[4] == -1: nothing to do but obj[8] = r4[0]. Otherwise dst = (obj[4] * 4 + r4[28]), src =
// obj[36], n = the halfword at obj[22]; for i < n (the count and the flag byte obj[31] read again after every block):
// with the flag set, sub_822EA3C0 copies the block src + 1024 i -> dst + 1024 i (lvx / stvx: both rounded down to 16;
// its eight dcbz pieces leave only [(dst & ~15) - 16, dst & ~15) zero, when bit 4 of dst is set) and the source block
// is then zeroed by eight dcbzl (the 1024 bytes from src & ~127); with the flag clear the block dst + 1024 i is zeroed
// by eight dcbzl from dst & ~127. Then obj[31] = 0, obj[8] = r4[0]. A stwu of the frame (-144) leaves its back-chain
// word at r1 - 144, and the savegprlr stubs are not emitted. Console race (2026-11-11 15:18, rebased build 46b3f6d6):
// job workers 3 and 4, 1.42 % of a core; 20 % of the samples on the first load of an object field (cache miss), the
// rest across the dcbzl memsets, each followed by its own store-card mark, the copy's inlined callee after them. Here:
// the whole loop at once: the blocks are consecutive, so one 1024 n copy and one zero of the source (a zero of the
// destination with the flag clear), the 16-byte lead pieces zeroed where the next block's dcbz would have, the cards
// marked once a range, no loop and no re-read of obj[22] / obj[31].
// Registers (the generated function writes r3-r10 back; the rest are its locals): n == 0: r9 = 0; flag set: r3 = 96, r4 =
// 112 (the callee's last values), r5-r9 = 896, 768, 640, 512, 384; flag clear: r4-r9 = 896, 768, 640, 512, 384, 256; r10 =
// r4[0] whenever the count is not -1 (the early exit writes no register).
// Declined: any overlap of the written ranges (the destination with its lead piece, the source with its dcbzl reach, the
// frame word) with each other or with the two objects' fields the original reads (obj .. obj + 40 and r4 .. r4 + 32: it
// reads the count and the flag again after every block); obj[31] inside r4[0] (the original reads r4[0] after clearing the
// flag); ranges that wrap 4 GB or cross the physical-view boundary.
namespace n_8230A4D8 {

template <class S>
[[gnu::always_inline]] inline bool Native(PPCContext& __restrict ctx, uint8_t* base, [[maybe_unused]] S& s) {
  if (!On(kN8230A4D8)) [[unlikely]] return false;
  using namespace blur_b3b;
  const uint32_t obj = ctx.r3.u32, o4 = ctx.r4.u32, frame = ctx.r1.u32 - 144u;
  if (!detail::Straight(obj, 40u) || !detail::Straight(o4, 32u) || !detail::Straight(frame, 4u)) [[unlikely]]
    return false;
  if (Hit(frame, 4u, obj, 40u) || Hit(frame, 4u, o4, 32u) || Hit(obj + 31u, 1u, o4, 4u)) [[unlikely]] return false;
  const uint32_t idx = LdW(base, obj + 4u);
  if (idx == 0xFFFFFFFFu) {
    s.Hand(kN8230A4D8, 0);
    if (!s.Note(frame, 4u) || !s.Note(obj + 8u, 4u)) [[unlikely]] return true;
    ChainW(base, frame, ctx.r1.u32);
    StW(base, obj + 8u, LdW(base, o4));
    ::banjo::hot::Card(base, obj + 8u, 4u);
    return true;
  }
  const uint32_t dst0 = (idx << 2) + LdW(base, o4 + 28u);
  const uint32_t n = LdH(base, obj + 22u);
  const uint32_t srcb = LdW(base, obj + 36u);
  const bool flag = LdB(base, obj + 31u) != 0;
  const uint32_t tail = LdW(base, o4);  // r4[0]: read by the original after obj[31] = 0
  if (n == 0) {
    s.Hand(kN8230A4D8, 0);
    if (!s.Note(frame, 4u) || !s.Note(obj + 31u, 1u) || !s.Note(obj + 8u, 4u)) [[unlikely]] return true;
    ChainW(base, frame, ctx.r1.u32);
    *::banjo::hot::Raw(base, obj + 31u) = 0;
    ::banjo::hot::Card(base, obj + 31u, 1u);
    StW(base, obj + 8u, tail);
    ::banjo::hot::Card(base, obj + 8u, 4u);
    ctx.r9.s64 = 0;
    ctx.r10.u64 = tail;
    return true;
  }
  const uint32_t len = n << 10;
  // The destination (with the lead piece below it) and the source (from its dcbzl reach to the end of its reads).
  uint32_t w0, wlen, z0, ulen;
  if (flag) {
    const uint32_t d = dst0 & ~15u, lead = dst0 & 16u;
    w0 = d - lead;
    wlen = len + lead;
    z0 = srcb & ~127u;
    ulen = (srcb & ~15u) - z0 + len;
  } else {
    w0 = dst0 & ~127u;
    wlen = len;
    z0 = 0;
    ulen = 0;
  }
  if (!detail::Straight(w0, wlen)) [[unlikely]] return false;
  if (flag && !detail::Straight(z0, ulen)) [[unlikely]] return false;
  if (Hit(w0, wlen, obj, 40u) || Hit(w0, wlen, o4, 32u) || Hit(w0, wlen, frame, 4u)) [[unlikely]] return false;
  if (flag && (Hit(z0, ulen, obj, 40u) || Hit(z0, ulen, o4, 32u) || Hit(z0, ulen, frame, 4u) ||
               Hit(z0, ulen, w0, wlen)))
    [[unlikely]] return false;
  s.Hand(kN8230A4D8, len);
  if (!s.Note(frame, 4u) || !s.Note(w0, wlen) || (flag && !s.Note(z0, len)) || !s.Note(obj + 31u, 1u) ||
      !s.Note(obj + 8u, 4u)) [[unlikely]]
    return true;
  ChainW(base, frame, ctx.r1.u32);
  ::banjo::hot::Card(base, w0, wlen);
  if (flag) {
    const uint32_t d = dst0 & ~15u, lead = dst0 & 16u, s16 = srcb & ~15u;
    blur_copy::Copy64s(::banjo::hot::Raw(base, d), ::banjo::hot::Raw(base, s16), len);  // VS-07 (blur_block_copy.h)
    if (lead)  // the next block's dcbz zeroes the last 16 bytes of this one, and the first block's the 16 below it
      for (uint32_t i = 0; i < n; ++i) std::memset(::banjo::hot::Raw(base, d - 16u + (i << 10)), 0, 16);
    ::banjo::hot::Card(base, z0, len);
    std::memset(::banjo::hot::Raw(base, z0), 0, len);
    ctx.r3.s64 = 96;
    ctx.r4.s64 = 112;
    ctx.r5.s64 = 896;
    ctx.r6.s64 = 768;
    ctx.r7.s64 = 640;
    ctx.r8.s64 = 512;
    ctx.r9.s64 = 384;
  } else {
    std::memset(::banjo::hot::Raw(base, w0), 0, len);
    ctx.r4.s64 = 896;
    ctx.r5.s64 = 768;
    ctx.r6.s64 = 640;
    ctx.r7.s64 = 512;
    ctx.r8.s64 = 384;
    ctx.r9.s64 = 256;
  }
  *::banjo::hot::Raw(base, obj + 31u) = 0;
  ::banjo::hot::Card(base, obj + 31u, 1u);
  StW(base, obj + 8u, tail);
  ::banjo::hot::Card(base, obj + 8u, 4u);
  ctx.r10.u64 = tail;
  return true;
}

}  // namespace n_8230A4D8

// sub_822D92D8 - int16 -> float with a scale: r3 = n (at most 1024), r4 = the big-endian int16 input (any 2-byte
// alignment), r5 = the float output; out[k] = float(in[k]) * 2^-15 (0x38000000), k < n. The code (the XDK's vector
// converter) goes in five stretches: (1) while out & 15 is not 0 and fewer than 4 floats have been written: one element
// at a time (lhzu, extsh, std to 80(r1), lfd, fcfid, frsp, fmuls, stfsu: a doubleword stored and loaded back); (2) blocks
// of 32 elements (the input through lvlx128 + lvrx128 pairs, vupkhsb128 / vupklsb128, vcsxwfp128 by 15, eight stvx128);
// (3) blocks of 8; (4) one group of 4 through the stack (std 80 / 88 / 96 / 104, lfd, fcfid, frsp, fmuls); (5) single
// elements through 104(r1). n > 1024 first calls a library routine. Console race (2026-11-11 15:18, rebased build
// 46b3f6d6): job workers 3 and 4, 1.42 % of a core, no bucket above 9 % of its samples (the block bodies, the stack round
// trips of the scalar stretches, the prefetch loop and the store-card marks between). Every result is exact (an int16 times
// a power of two), so the order of the stretches does not matter for the floats. Here: the whole conversion as one
// pass of unaligned 16-byte loads, a 16-bit byte swap, cvtepi16_epi32 + cvtepi32_ps + one multiply and a 32-bit byte
// swap (8 elements a pass), one store-card range for the output, no lvlx / lvrx pairs, no stack round trips, no
// prefetch loop; the registers and stack words the stretches leave are computed from the lengths of the stretches.
// Registers (everything the generated function writes back): r3 (16 after a 32-block), r4 = 16, r5-r10 and f1-f13 as the
// last pass of the last stretch leaves them (the formulas follow the generated statements: 64-bit values where it adds
// 64-bit, a pointer updated by lhzu / stfsu keeps its upper word); the stack: the back-chain word at r1 - 192 and the
// doublewords at (r1 - 192) + 80, 88, 96, 104 that the scalar stretches stored last. The flush mode: off (the lfs of the
// scale on every path turns it off; since VX-01 the vector stretches' conversions leave it alone).
// Declined: n > 1024, an output that is not 4-byte aligned (stvx would round it down), the input and output ranges or the
// frame (r1 - 192 .. r1) overlapping each other, ranges that wrap 4 GB or cross the physical-view boundary.
namespace n_822D92D8 {

template <class S>
[[gnu::always_inline]] inline bool Native(PPCContext& __restrict ctx, uint8_t* base, [[maybe_unused]] S& s) {
  if (!On(kN822D92D8)) [[unlikely]] return false;
  using namespace blur_b3b;
  const uint64_t n64 = ctx.r3.u64, in64 = ctx.r4.u64, out64 = ctx.r5.u64;
  const uint32_t n = uint32_t(n64), in = uint32_t(in64), out = uint32_t(out64), sp = ctx.r1.u32;
  if (n > 1024u || (out & 3u)) [[unlikely]] return false;
  const uint32_t frame = sp - 192u;
  if (!detail::Straight(frame, 192u)) [[unlikely]] return false;
  if (n != 0) {
    if (!detail::Straight(in, 2u * n) || !detail::Straight(out, 4u * n)) [[unlikely]] return false;
    if (Hit(in, 2u * n, out, 4u * n) || Hit(in, 2u * n, frame, 192u) || Hit(out, 4u * n, frame, 192u)) [[unlikely]]
      return false;
  }
  const auto half = [&](uint32_t k) -> int32_t { return int16_t(LdH(base, in + 2u * k)); };  // in[k], sign-extended
  uint64_t g3 = n64, g4 = in64, g5 = out64, g6 = ctx.r6.u64, g7 = ctx.r7.u64, g8 = 0, g9 = 0, g10 = 0;
  PPCRegister F[14];
  F[1] = ctx.f1; F[2] = ctx.f2; F[3] = ctx.f3; F[4] = ctx.f4; F[5] = ctx.f5; F[6] = ctx.f6; F[7] = ctx.f7;
  F[8] = ctx.f8; F[9] = ctx.f9; F[10] = ctx.f10; F[11] = ctx.f11; F[12] = ctx.f12; F[13] = ctx.f13;
  int mode = 0;  // 0: the flush mode is as it came, 1: on, 2: off
  bool sv[4] = {false, false, false, false};  // the doublewords at 80, 88, 96, 104
  uint64_t sd[4] = {0, 0, 0, 0};
  uint32_t covered = 0;
  // (1) the head: one element at a time until the output is 16-byte aligned
  const uint32_t r11h = out & 0xFu;
  if (r11h != 0) {
    g6 = out64 - 4u;
    g7 = in64 - 2u;
    const uint32_t hmax = (16u - r11h + 3u) / 4u;
    const uint32_t h = hmax < n ? hmax : n;
    if (h != 0) {
      const int32_t v = half(h - 1u);
      g7 = SetLo(g7, uint32_t(g7) + 2u * h);
      g6 = SetLo(g6, uint32_t(g6) + 4u * h);
      g9 = uint16_t(v);
      g5 = uint64_t(int64_t(v));
      g10 = h;
      g8 = h;
      F[13].u64 = uint64_t(int64_t(v));
      F[12].f64 = double(int64_t(v));
      F[11].f64 = Fs(v);
      F[10].f64 = Fm(v);
      sv[0] = true;
      sd[0] = uint64_t(int64_t(v));
      mode = 2;
      covered = h;
    }
  }
  g4 = 16;
  // (2) blocks of 32
  {
    const uint32_t nb = uint32_t(n64 - g10) & ~31u;
    g9 = uint64_t(nb) + g10;
    const uint32_t end = uint32_t(g9) & ~31u;
    if (uint32_t(g10) < end) {
      const uint64_t r11 = uint64_t(end) - g10;
      g6 = g8 + 16u;
      g7 = r11 - 1u;
      g9 = g10 + 8u;
      const uint32_t q = uint32_t(g7) >> 5;
      g9 = uint32_t(uint32_t(g9) << 2);
      g7 = uint64_t(q) + 1u;
      g5 = uint32_t(uint32_t(g6) << 1);
      g6 = uint32_t(uint32_t(g7) << 5);
      g9 = g5 + in64;
      g10 = g6 + g10;
      g8 = g6 + g8;
      g3 = g4;
      const uint32_t cnt = uint32_t(g7);
      const uint64_t last = g9 + 64ull * (cnt - 1u);
      g7 = last - 32u;
      g6 = last - 16u;
      g5 = last + 16u;
      g9 = last + 64u;
      mode = 1;
      covered += 32u * cnt;
    }
  }
  // (3) blocks of 8
  {
    const uint32_t nb = uint32_t(n64 - g10) & ~7u;
    g9 = uint64_t(nb) + g10;
    const uint32_t end = uint32_t(g9) & ~7u;
    if (uint32_t(g10) < end) {
      uint64_t r11 = uint64_t(end) - g10;
      g9 = uint32_t(uint32_t(g8) << 1);
      r11 = r11 - 1u;
      g6 = uint32_t(uint32_t(g10) << 2);
      g7 = uint32_t(r11) >> 3;
      g7 = g7 + 1u;
      g9 = g6 + out64;
      g6 = uint32_t(uint32_t(g7) << 3);
      g10 = g6 + g10;
      const uint32_t cnt = uint32_t(g7);
      g8 = g6 + g8;
      g7 = g4;
      g9 = g9 + 32ull * cnt;
      mode = 1;
      covered += 8u * cnt;
    }
  }
  // (4) one group of 4 and (5) the single elements
  if (uint32_t(g10) < n) {
    const uint64_t rem = n64 - g10;
    if (int32_t(uint32_t(rem)) >= 4) {
      uint64_t r11 = uint32_t(uint32_t(g10) << 2);
      g9 = uint32_t(uint32_t(g8) << 1);
      r11 = r11 + out64;
      g7 = g9 + in64;
      g9 = r11 - 4u;
      g6 = n64 - 3u;
      const uint32_t t = uint32_t(rem) >> 2;
      const uint32_t e = uint32_t(g8) + 4u * (t - 1u);  // the first element of the last group
      const int32_t a = half(e), b = half(e + 1u), c = half(e + 2u), d = half(e + 3u);
      g10 = g10 + 4u * t;
      g8 = g8 + 4u * t;
      g5 = uint64_t(int64_t(c));
      g3 = uint64_t(int64_t(d));
      g4 = uint16_t(c);
      g7 = uint16_t(d);
      g9 = SetLo(g9, uint32_t(g9) + 16u * t);
      F[7].f64 = double(a);
      F[8].f64 = double(b);
      F[9].f64 = double(c);
      F[6].f64 = double(d);
      F[10].u64 = uint64_t(int64_t(c));
      F[3].f64 = Fs(a);
      F[4].f64 = Fs(b);
      F[5].f64 = Fs(c);
      F[2].f64 = Fs(d);
      F[12].f64 = Fm(a);
      F[13].f64 = Fm(b);
      F[1].f64 = Fm(c);
      F[11].f64 = Fm(d);
      sv[0] = sv[1] = sv[2] = sv[3] = true;
      sd[0] = uint64_t(int64_t(a));
      sd[1] = uint64_t(int64_t(b));
      sd[2] = uint64_t(int64_t(d));
      sd[3] = uint64_t(int64_t(c));
      mode = 2;
      covered += 4u * t;
    }
    if (uint32_t(g10) < n) {
      const uint64_t cnt = n64 - g10;
      uint64_t r11 = uint32_t(uint32_t(g10) << 2);
      g7 = cnt;
      g9 = uint32_t(uint32_t(g8) << 1);
      r11 = r11 + out64;
      const uint64_t p0 = r11 - 4u;
      const int32_t v = half(n - 1u);
      g10 = SetLo(p0, uint32_t(p0) + 4u * uint32_t(cnt));
      g8 = uint64_t(int64_t(v));
      g9 = uint16_t(v);
      F[13].u64 = uint64_t(int64_t(v));
      F[12].f64 = double(int64_t(v));
      F[11].f64 = Fs(v);
      F[10].f64 = Fm(v);
      sv[3] = true;
      sd[3] = uint64_t(int64_t(v));
      mode = 2;
      covered += uint32_t(cnt);
    }
  }
  if (covered != n) [[unlikely]] return false;  // the stretches did not add up: not understood, nothing written yet
  s.Hand(kN822D92D8, 4u * n);
  if (!s.Note(frame, 4u)) [[unlikely]] return true;
  if ((sv[0] || sv[1] || sv[2] || sv[3]) && !s.Note(frame + 80u, 32u)) [[unlikely]] return true;
  if (n != 0 && !s.Note(out, 4u * n)) [[unlikely]] return true;
  ChainW(base, frame, sp);
  for (uint32_t k = 0; k < 4; ++k)
    if (sv[k]) StD(base, frame + 80u + 8u * k, sd[k]);
  if (n != 0) {
    ::banjo::hot::Card(base, out, 4u * n);
    CvtI16F32(::banjo::hot::Raw(base, in), ::banjo::hot::Raw(base, out), n);
  }
  // blur-nx (2026-11-12, after CPU batch 2's VX-01): the vector stretches' vcsxwfp128 no longer set the flush mode, and
  // every path of the original runs the lfs of the scale (0x8202E414) with the mode off before the stretches, n == 0
  // included, so the original always leaves it off (the guarded PC verify of 2026-11-12: the copy left it on).
  (void)mode;
  ctx.fpscr.disableFlushMode();
  ctx.r3.u64 = g3;
  ctx.r4.u64 = g4;
  ctx.r5.u64 = g5;
  ctx.r6.u64 = g6;
  ctx.r7.u64 = g7;
  ctx.r8.u64 = g8;
  ctx.r9.u64 = g9;
  ctx.r10.u64 = g10;
  ctx.f1 = F[1]; ctx.f2 = F[2]; ctx.f3 = F[3]; ctx.f4 = F[4]; ctx.f5 = F[5]; ctx.f6 = F[6]; ctx.f7 = F[7];
  ctx.f8 = F[8]; ctx.f9 = F[9]; ctx.f10 = F[10]; ctx.f11 = F[11]; ctx.f12 = F[12]; ctx.f13 = F[13];
  return true;
}

}  // namespace n_822D92D8

namespace blur_b3b {
// lvlx128's lane 0 (the word the vectors below keep): the 16 aligned bytes holding ea, shifted left by ea & 15 with
// zeros coming in (a word at an offset above 12 is cut short), as the generated REX_SHUFFLE_TABLE(VectorMaskL) gives
// it; returned as the float's bits.
[[gnu::always_inline]] inline uint32_t LvlxW(uint8_t* base, uint32_t ea) {
  const uint32_t o = ea & 15u;
  const uint8_t* q = ::banjo::hot::Raw(base, ea & ~15u);
  // blur-nx (2026-11-11, CPU batch 2): the word through one pointer (q[o + 1u] could wrap in 32 bits for all GCC
  // knows, which kept it four byte loads); the same bytes, one load and a rev.
  if (o <= 12u) [[likely]]
    return blur_gather::Be32(q, o);
  uint32_t w = 0;
  for (uint32_t j = 0; j < 4; ++j) w = w << 8 | (o + j < 16u ? q[o + j] : 0u);
  return w;
}
}  // namespace blur_b3b

// sub_822D9040 - a table lookup with linear interpolation, 32 x 8 positions: r4 = the position step s (a 32-bit
// word), r5 = the table (floats), r6 = the output (1024 bytes), f1 != 1.0 (with f1 == 1.0 it copies 1 KB from r9 by a
// library routine: declined). Position p(m) = m * s for m < 256; entry address = table + ((p >> 6) & 0x3FFFFFC) (the
// word index (p >> 8) times 4, wrapping at 24 bits); lo = that float, hi = the next; the fraction comes from a running
// vector of positions (v63, v62 = (0..3, 4..7) * s * 8 * K, K = the float at 0x82B9D114 (guest data), incremented by
// 8 positions each pass; trunc / sub), out = lo + (hi - lo) * frac (vmaddfp), eight floats a pass in two stvx128
// at r6 & ~15 + 32 i and + 16. Setup: s is stored to 172(r1) and s << 3 to 80(r1) (r1 = the frame after stwu -144) and
// loaded back by lvlx128 (a quadword load right after a word store: no store forwarding); vcuxwfp128 and splats.
// Console race (2026-11-11 15:18, rebased build 46b3f6d6): job workers 3 and 4, 0.80 % of a core; the samples
// spread over the 16 table loads of a pass, the lvlx128 + vmrghw128 merges and the setup. Here: the setup values
// from registers (the two stack words still stored, the lvlx128 of them replaced by the values; lane 0 only is used by
// vspltw128), each gather as 16 word loads (lane 0 of each lvlx128 is the only lane vmrghw128 keeps: eight lo and eight
// hi words go straight into two vectors each: [lo3 lo2 lo1 lo0], [lo7 .. lo4], [hi3 .. hi0], [hi7 .. hi4]), the same
// vsubfp / vrfiz / REX_VMADDFP, the cards marked once for the 1 KB, no loop-carried card marks.
// Registers (the generated function writes back): r3 = A0, r4 = the table, r5 = A2, r6 = A1, r7 = A3 (the last pass's
// entry addresses of positions 0, 2, 1, 3), r8 = 256 s, r9 = r6 + 1024 (as it came), r10 = 4; v0 = the last result
// (positions 4-7), v11 = hi(0-3) - lo(0-3), v12 = hi(4-7) - lo(4-7), v13 = lo(4-7) of the last pass; the stack:
// the back-chain word at r1 - 144, s at (r1 - 144) + 172, s << 3 at (r1 - 144) + 80. The flush mode: on.
// Declined: f1 == 1.0, the output or the frame overlapping, ranges that wrap 4 GB or cross the physical-view boundary.
// (Table reads overlapping the output are exact: every pass loads before it stores, as the original does.)
namespace n_822D9040 {

template <class S>
[[gnu::always_inline]] inline bool Native(PPCContext& __restrict ctx, uint8_t* base, [[maybe_unused]] S& s) {
  if (!On(kN822D9040)) [[unlikely]] return false;
  using namespace blur_b3b;
  if (ctx.f1.f64 == 1.0) return false;
  const uint64_t step64 = ctx.r4.u64, tab = ctx.r5.u64, out64 = ctx.r6.u64;
  const uint32_t sp = ctx.r1.u32, frame = sp - 144u, step = uint32_t(step64);
  const uint32_t o16 = uint32_t(out64) & ~15u;
  if (!detail::Straight(frame, 176u) || !detail::Straight(o16, 1024u)) [[unlikely]] return false;
  if (Hit(o16, 1024u, frame, 176u)) [[unlikely]] return false;
  // lane 0 of the two stack loads (the words just stored, unless the offset cuts them short)
  const uint32_t a172 = frame + 172u, a80 = frame + 80u;
  const uint32_t w172 = (a172 & 15u) <= 12u ? step : 0u, w80 = (a80 & 15u) <= 12u ? uint32_t(step << 3) : 0u;
  s.Hand(kN822D9040, 1024);
  if (!s.Note(frame, 4u) || !s.Note(a80, 4u) || !s.Note(a172, 4u) || !s.Note(o16, 1024u)) [[unlikely]] return true;
  ChainW(base, frame, sp);
  StW(base, a80, uint32_t(step << 3));
  StW(base, a172, step);
  ::banjo::hot::Card(base, o16, 1024u);
  const uint32_t x172 = (a172 & 15u) <= 12u ? w172 : LvlxW(base, a172);
  const uint32_t x80 = (a80 & 15u) <= 12u ? w80 : LvlxW(base, a80);
  const uint32_t kbits = LvlxW(base, 0x82B9D114u);
  ctx.fpscr.enableFlushMode();
  const simde__m128i v62i = simde_mm_set1_epi32(int(x172));
  const simde__m128i v59i = simde_mm_set1_epi32(int(x80));
  const simde__m128i v57i = simde_mm_set1_epi32(int(kbits));
  const simde__m128 v58 = rex::ppc::simde_mm_cvtepu32_ps_(v62i);
  const simde__m128 v56 = rex::ppc::simde_mm_cvtepu32_ps_(v59i);
  const simde__m128 v57 = simde_mm_castsi128_ps(v57i);
  const simde__m128 v55 = simde_mm_mul_ps(v58, v57);
  const simde__m128 v61 = simde_mm_mul_ps(v56, v57);
  simde__m128 v63 = simde_mm_castsi128_ps(REX_RO_V128(0x8202E510, 0x000000003F800000ull, 0x4000000040400000ull));
  simde__m128 v62 = simde_mm_castsi128_ps(REX_RO_V128(0x8202E520, 0x4080000040A00000ull, 0x40C0000040E00000ull));
  v63 = simde_mm_mul_ps(v63, v55);
  v62 = simde_mm_mul_ps(v62, v55);
  uint64_t b = 0;
  uint64_t r9 = out64;
  simde__m128 vr0 = simde_mm_setzero_ps(), vr11 = vr0, vr12 = vr0, vr13 = vr0;
  // blur-nx (2026-11-11, CPU batch 2, blur_gather.h): every entry address is the table plus a multiple of 4, so with a
  // 4-aligned table each (lo, hi) is one 8-byte load (unless an address is one PairOk excludes).
  const bool tab_aligned = (uint32_t(tab) & 3u) == 0;
  for (uint32_t it = 0; it < 32; ++it) {
    const simde__m128 v54 = simde_mm_round_ps(v63, SIMDE_MM_FROUND_TO_ZERO | SIMDE_MM_FROUND_NO_EXC);
    const simde__m128 v53 = v63;
    const simde__m128 v52 = v62;
    const simde__m128 v51 = v62;
    v63 = simde_mm_add_ps(v63, v61);
    v62 = simde_mm_add_ps(v62, v61);
    const simde__m128 v50 = simde_mm_round_ps(v52, SIMDE_MM_FROUND_TO_ZERO | SIMDE_MM_FROUND_NO_EXC);
    const simde__m128 v12a = simde_mm_sub_ps(v53, v54);
    uint32_t ad[8];
    for (uint32_t k = 0; k < 8; ++k) {
      const uint64_t pos = b + uint64_t(k) * step64;
      ad[k] = uint32_t(((uint32_t(pos) >> 6) & 0x3FFFFFCu) + tab);
    }
    b += 8ull * step64;
    bool pairs = tab_aligned;
    for (uint32_t k = 0; k < 8; ++k) pairs &= blur_gather::PairOk(ad[k]);
    simde__m128 g0, g1, h0, h1;
    if (pairs) [[likely]] {
      using ::banjo::hot::Raw;
      blur_gather::Pairs4(Raw(base, ad[0]), Raw(base, ad[1]), Raw(base, ad[2]), Raw(base, ad[3]), g0, h0);
      blur_gather::Pairs4(Raw(base, ad[4]), Raw(base, ad[5]), Raw(base, ad[6]), Raw(base, ad[7]), g1, h1);
    } else {
      uint32_t lo[8], hi[8];
      for (uint32_t k = 0; k < 8; ++k) lo[k] = LvlxW(base, ad[k]);
      for (uint32_t k = 0; k < 8; ++k) hi[k] = LvlxW(base, ad[k] + 4u);
      g0 = simde_mm_castsi128_ps(simde_mm_set_epi32(int(lo[0]), int(lo[1]), int(lo[2]), int(lo[3])));
      g1 = simde_mm_castsi128_ps(simde_mm_set_epi32(int(lo[4]), int(lo[5]), int(lo[6]), int(lo[7])));
      h0 = simde_mm_castsi128_ps(simde_mm_set_epi32(int(hi[0]), int(hi[1]), int(hi[2]), int(hi[3])));
      h1 = simde_mm_castsi128_ps(simde_mm_set_epi32(int(hi[4]), int(hi[5]), int(hi[6]), int(hi[7])));
    }
    const simde__m128 v11 = simde_mm_sub_ps(h0, g0);
    simde__m128 r = REX_VMADDFP(v12a, v11, g0);
    simde_mm_store_si128((simde__m128i*)::banjo::hot::Raw(base, uint32_t(r9) & ~15u),
                         REX_BSWAP128(simde_mm_castps_si128(r)));
    const simde__m128 v12 = simde_mm_sub_ps(h1, g1);
    r = REX_VMADDFP(simde_mm_sub_ps(v51, v50), v12, g1);
    simde_mm_store_si128((simde__m128i*)::banjo::hot::Raw(base, (uint32_t(r9) + 16u) & ~15u),
                         REX_BSWAP128(simde_mm_castps_si128(r)));
    r9 += 32;
    vr0 = r;
    vr11 = v11;
    vr12 = v12;
    vr13 = g1;
  }
  // The entry addresses as the generated code keeps them: a 24-bit-wrapped index plus the table, 64 bits wide.
  const auto lastidx = [&](uint32_t k) { return uint64_t((uint32_t((b - 8ull * step64 + uint64_t(k) * step64)) >> 6) & 0x3FFFFFCu); };
  ctx.r3.u64 = lastidx(0) + tab;
  ctx.r4.u64 = tab;
  ctx.r5.u64 = lastidx(2) + tab;
  ctx.r6.u64 = lastidx(1) + tab;
  ctx.r7.u64 = lastidx(3) + tab;
  ctx.r8.u64 = b;
  ctx.r9.u64 = r9;
  ctx.r10.s64 = 4;
  simde_mm_store_ps(ctx.v0.f32, vr0);
  simde_mm_store_ps(ctx.v11.f32, vr11);
  simde_mm_store_ps(ctx.v12.f32, vr12);
  simde_mm_store_ps(ctx.v13.f32, vr13);
  return true;
}

}  // namespace n_822D9040

// sub_822D79A8 - the same lookup as sub_822D9040 on two tables: r4 = the phase step (a 32-bit word), r5 = table A, r6 =
// table B (floats), r7 = output 1, r8 = output 2 (1024 bytes each), f1 != 1.0 (with f1 == 1.0 it copies the tables to
// the outputs with two calls of a library routine: declined). 64 passes of four chained lookups: the phase r9 grows by the
// step (an 8-bit phase: the low byte carries over), each step moves the byte offset r11 by ((phase >> 6) & 0x3FFFFFC);
// at each of the four offsets S0..S3 the words of A and B at S and S + 4 are loaded (lvlx128: only lane 0 of each
// survives the vrlimi128 blends, so each vector is [w(S3) w(S2) w(S1) w(S0)]); the fraction is v63 * v61 minus its
// truncation (v63 = (0..3) + 4 per pass, v61 = splat(float(step) * K), K the float at 0x82B9D114); out1 = (A1 - A0) *
// frac + A0, out2 = (B1 - B0) * frac + B0, two stvx128 a pass at r7 & ~15 + 16 i and r8 & ~15 + 16 i. Setup: the step
// is stored to 140(r1) (r1 = the frame after stwu -112) and loaded back by lvlx128 (no store forwarding). Console race
// (2026-11-11 15:18, rebased build 46b3f6d6): job workers 3 and 4, 0.74 % of a core; the samples spread over the
// 16 table loads of a pass (each lvlx128 + a blend) and the dependent offset chain. Here: the offsets and phases
// computed once a pass with the generated 64-bit arithmetic, each gather as word loads straight into the four vectors,
// the same vsubfp / vrfiz / REX_VMADDFP, the stack round trip of the setup replaced by the register, the cards
// marked once for each output.
// Registers (the generated function writes back r3, r5, r7-r10 and v0, v10-v13): of the last pass: r3 = ((P2 >> 6) &
// 0x3FFFFFC), r5 = S1 + 4, r7 = ((P3 >> 6) & 0x3FFFFFC), r8 = ((P4 >> 6) & 0x3FFFFFC), r9 = the phase's low byte, r10 = r8 + 1024
// (as it came); v0 = out2's last vector, v10 = out1's, v11 = B1 - B0, v12 = the fraction, v13 = B0 of the last pass; the stack: the back-chain
// word at r1 - 112 and the step at (r1 - 112) + 140. The flush mode: on.
// Declined: f1 == 1.0, an output overlapping the frame, the outputs overlapping each other, ranges that wrap 4 GB or cross the
// physical-view boundary. (Table reads overlapping an output are exact: a pass loads before it stores, as the original.)
namespace n_822D79A8 {

template <class S>
[[gnu::always_inline]] inline bool Native(PPCContext& __restrict ctx, uint8_t* base, [[maybe_unused]] S& s) {
  if (!On(kN822D79A8)) [[unlikely]] return false;
  using namespace blur_b3b;
  if (ctx.f1.f64 == 1.0) return false;
  const uint64_t step64 = ctx.r4.u64, tabA = ctx.r5.u64, tabB = ctx.r6.u64, out1 = ctx.r7.u64, out2 = ctx.r8.u64;
  const uint32_t sp = ctx.r1.u32, frame = sp - 112u, step = uint32_t(step64);
  const uint32_t o1 = uint32_t(out1) & ~15u, o2 = uint32_t(out2) & ~15u;
  if (!detail::Straight(frame, 144u) || !detail::Straight(o1, 1024u) || !detail::Straight(o2, 1024u)) [[unlikely]]
    return false;
  if (Hit(o1, 1024u, frame, 144u) || Hit(o2, 1024u, frame, 144u) || Hit(o1, 1024u, o2, 1024u)) [[unlikely]] return false;
  const uint32_t a140 = frame + 140u;
  s.Hand(kN822D79A8, 2048);
  if (!s.Note(frame, 4u) || !s.Note(a140, 4u) || !s.Note(o1, 1024u) || !s.Note(o2, 1024u)) [[unlikely]] return true;
  ChainW(base, frame, sp);
  StW(base, a140, step);
  ::banjo::hot::Card(base, o1, 1024u);
  ::banjo::hot::Card(base, o2, 1024u);
  const uint32_t x140 = (a140 & 15u) <= 12u ? step : LvlxW(base, a140);
  const uint32_t kbits = LvlxW(base, 0x82B9D114u);
  ctx.fpscr.enableFlushMode();
  const simde__m128 v62 = rex::ppc::simde_mm_cvtepu32_ps_(simde_mm_set1_epi32(int(4)));
  const simde__m128 v60 = rex::ppc::simde_mm_cvtepu32_ps_(simde_mm_set1_epi32(int(x140)));
  const simde__m128 v59 = simde_mm_castsi128_ps(simde_mm_set1_epi32(int(kbits)));
  const simde__m128 v58 = simde_mm_mul_ps(v60, v59);
  const simde__m128 v61 = simde_mm_castsi128_ps(simde_mm_shuffle_epi32(simde_mm_castps_si128(v58), 0xFF));
  simde__m128 v63 = simde_mm_castsi128_ps(REX_RO_V128(0x8202E1E0, 0x000000003F800000ull, 0x4000000040400000ull));
  uint64_t p = 0, sS = 0;  // the phase (r9) and the byte offset (r11)
  uint64_t r3 = ctx.r3.u64, r5 = ctx.r5.u64, r7 = ctx.r7.u64, r8 = ctx.r8.u64;
  uint64_t r10 = out2;
  uint32_t o1p = uint32_t(out1), o2p = uint32_t(out2);
  simde__m128 vr0 = simde_mm_setzero_ps(), vr10 = vr0, vr11 = vr0, vr12 = vr0, vr13 = vr0;
  const auto idx = [](uint64_t ph) { return uint64_t((uint32_t(ph) >> 6) & 0x3FFFFFCu); };
  const bool tabs_aligned = ((uint32_t(tabA) | uint32_t(tabB)) & 3u) == 0;
  for (uint32_t it = 0; it < 64; ++it) {
    p = p + step64;
    const simde__m128 v57 = simde_mm_mul_ps(v63, v61);
    v63 = simde_mm_add_ps(v63, v62);
    const uint64_t s0 = sS;
    const uint64_t i1 = idx(p);
    const uint64_t s1 = i1 + s0;
    const uint64_t p2 = (p & 0xFF) + step64;
    const uint64_t i2 = idx(p2);
    const uint64_t s2 = i2 + s1;
    const uint64_t p3 = (p2 & 0xFF) + step64;
    const uint64_t i3 = idx(p3);
    const uint64_t s3 = i3 + s2;
    const uint64_t p4 = (p3 & 0xFF) + step64;
    const uint64_t i4 = idx(p4);
    const uint64_t sv[4] = {s0, s1, s2, s3};
    uint32_t ea[4], eb[4];
    bool pairs = tabs_aligned;
    for (uint32_t k = 0; k < 4; ++k) {
      ea[k] = uint32_t(sv[k]) + uint32_t(tabA);
      eb[k] = uint32_t(sv[k]) + uint32_t(tabB);
      pairs &= blur_gather::PairOk(ea[k]) & blur_gather::PairOk(eb[k]);
    }
    simde__m128 g0, g1, h0, h1;
    // blur-nx (2026-11-11, CPU batch 2, blur_gather.h): the offsets are sums of multiples of 4, so with 4-aligned
    // tables each (lo, hi) is one 8-byte load (unless an address is one PairOk excludes).
    if (pairs) [[likely]] {
      using ::banjo::hot::Raw;
      blur_gather::Pairs4(Raw(base, ea[0]), Raw(base, ea[1]), Raw(base, ea[2]), Raw(base, ea[3]), g0, h0);
      blur_gather::Pairs4(Raw(base, eb[0]), Raw(base, eb[1]), Raw(base, eb[2]), Raw(base, eb[3]), g1, h1);
    } else {
      uint32_t wa[4], wb[4], na[4], nb[4];
      for (uint32_t k = 0; k < 4; ++k) {
        wa[k] = LvlxW(base, ea[k]);
        wb[k] = LvlxW(base, eb[k]);
      }
      for (uint32_t k = 0; k < 4; ++k) {
        na[k] = LvlxW(base, ea[k] + 4u);
        nb[k] = LvlxW(base, eb[k] + 4u);
      }
      g0 = simde_mm_castsi128_ps(simde_mm_set_epi32(int(wa[0]), int(wa[1]), int(wa[2]), int(wa[3])));
      g1 = simde_mm_castsi128_ps(simde_mm_set_epi32(int(wb[0]), int(wb[1]), int(wb[2]), int(wb[3])));
      h0 = simde_mm_castsi128_ps(simde_mm_set_epi32(int(na[0]), int(na[1]), int(na[2]), int(na[3])));
      h1 = simde_mm_castsi128_ps(simde_mm_set_epi32(int(nb[0]), int(nb[1]), int(nb[2]), int(nb[3])));
    }
    const simde__m128 v51 = simde_mm_round_ps(v57, SIMDE_MM_FROUND_TO_ZERO | SIMDE_MM_FROUND_NO_EXC);
    const simde__m128 v12 = simde_mm_sub_ps(v57, v51);
    const simde__m128 v10a = simde_mm_sub_ps(h0, g0);
    const simde__m128 v11 = simde_mm_sub_ps(h1, g1);
    const simde__m128 r1v = REX_VMADDFP(v10a, v12, g0);
    const simde__m128 r2v = REX_VMADDFP(v11, v12, g1);
    simde_mm_store_si128((simde__m128i*)::banjo::hot::Raw(base, o1p & ~15u), REX_BSWAP128(simde_mm_castps_si128(r1v)));
    simde_mm_store_si128((simde__m128i*)::banjo::hot::Raw(base, o2p & ~15u), REX_BSWAP128(simde_mm_castps_si128(r2v)));
    o1p += 16u;
    o2p += 16u;
    r10 += 16;
    p = p4 & 0xFF;
    sS = i4 + s3;
    r3 = i2;
    r5 = s1 + 4u;
    r7 = i3;
    r8 = i4;
    vr0 = r2v;
    vr10 = r1v;
    vr11 = v11;
    vr12 = v12;
    vr13 = g1;
  }
  ctx.r3.u64 = r3;
  ctx.r5.u64 = r5;
  ctx.r7.u64 = r7;
  ctx.r8.u64 = r8;
  ctx.r9.u64 = p;
  ctx.r10.u64 = r10;
  simde_mm_store_ps(ctx.v0.f32, vr0);
  simde_mm_store_ps(ctx.v10.f32, vr10);
  simde_mm_store_ps(ctx.v11.f32, vr11);
  simde_mm_store_ps(ctx.v12.f32, vr12);
  simde_mm_store_ps(ctx.v13.f32, vr13);
  return true;
}

}  // namespace n_822D79A8
