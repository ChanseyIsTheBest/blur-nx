// blur-nx (2026-11-11): hand-written natives, batch 2 (the user's request: natives for the next 60 hot game functions
// of the console race profile, 2026-11-11 15:18, rebased build 46b3f6d6; hand_batch2.draft.txt and
// docs/hand-natives-batch2.md say which and why). DRAFT: included inside `namespace banjo::hand` after
// banjo_hand_natives.h's own helpers; no #include lines. Its helpers are in blur_b2 (nothing is taken from
// blur_jq012, so the include order of the two files does not matter).
//
// Keys the main session has to add (enum values and keys), one per native below:
//   kN82954F28 kN822E48F8 kN82520D78 kN8230CBB8 kN82244170 kN822E9F20
#pragma once

namespace blur_b2 {

[[gnu::always_inline]] inline uint32_t Ld32(const uint8_t* p) {
  uint32_t v;
  std::memcpy(&v, p, 4);
  return __builtin_bswap32(v);
}
[[gnu::always_inline]] inline uint64_t Ld64(const uint8_t* p) {
  uint64_t v;
  std::memcpy(&v, p, 8);
  return __builtin_bswap64(v);
}
[[gnu::always_inline]] inline void St32(uint8_t* p, uint32_t v) {
  v = __builtin_bswap32(v);
  std::memcpy(p, &v, 4);
}
[[gnu::always_inline]] inline void St64(uint8_t* p, uint64_t v) {
  v = __builtin_bswap64(v);
  std::memcpy(p, &v, 8);
}
[[gnu::always_inline]] inline double D64(uint64_t b) {
  double d;
  std::memcpy(&d, &b, 8);
  return d;
}
[[gnu::always_inline]] inline uint64_t U64(double d) {
  uint64_t b;
  std::memcpy(&b, &d, 8);
  return b;
}
[[gnu::always_inline]] inline float F32(uint32_t b) {
  float f;
  std::memcpy(&f, &b, 4);
  return f;
}
[[gnu::always_inline]] inline uint32_t U32(float f) {
  uint32_t b;
  std::memcpy(&b, &f, 4);
  return b;
}

// The flush-mode switch is an `msr fpcr` with no memory operand: Fence orders loads after it, Pin ties a register
// value to its place (as Katamari's natives and blur_hand_natives_jq012.h do).
[[gnu::always_inline]] inline void Fence() { __asm__ __volatile__("" ::: "memory"); }
template <class T>
[[gnu::always_inline]] inline void Pin(T& x) {
#if defined(__aarch64__)
  __asm__ __volatile__("" : "+w"(x));
#elif defined(__x86_64__) || defined(_M_X64)
  __asm__ __volatile__("" : "+x"(x));
#endif
}

// [a, a + an) and [b, b + bn) share a byte (neither wraps 4 GB: the caller has checked Straight).
[[gnu::always_inline]] inline bool Hit(uint32_t a, uint32_t an, uint32_t b, uint32_t bn) {
  return uint32_t(a - b) < bn || uint32_t(b - a) < an;
}
// The aligned 16-byte block holding ea is one stretch of host memory.
[[gnu::always_inline]] inline bool Blk(uint32_t ea) { return ::banjo::hand::detail::Straight(ea & ~15u, 16); }

using V = simde__m128i;
using F = simde__m128;
[[gnu::always_inline]] inline F ToF(V v) { return simde_mm_castsi128_ps(v); }
[[gnu::always_inline]] inline V ToI(F v) { return simde_mm_castps_si128(v); }
[[gnu::always_inline]] inline V Splat0(V v) { return simde_mm_shuffle_epi32(v, 0xFF); }  // vspltw128 ...,0
// lvx128: the aligned 16 bytes at ea (rounded down), byte-reversed as the generated code does.
[[gnu::always_inline]] inline V LdV(uint8_t* base, uint32_t ea) {
  return REX_BSWAP128(simde_mm_load_si128((simde__m128i*)REX_RAW_ADDR(ea & ~0xFu)));
}
[[gnu::always_inline]] inline void StV(uint8_t* base, uint32_t ea, V v) {
  simde_mm_store_si128((simde__m128i*)REX_RAW_ADDR(ea & ~0xFu), REX_BSWAP128(v));
}
// lvlx128 (the generated statement).
[[gnu::always_inline]] inline V LvlxV(uint8_t* base, uint32_t ea) {
  return REX_SHUFFLE_TABLE(simde_mm_load_si128((simde__m128i*)REX_RAW_ADDR(ea & ~0xFu)), &VectorMaskL[(ea & 0xFu) * 16]);
}
// The eight dcbz of the audio kernels: 32 zero bytes at (dst + 128 k) & ~31, k = 0..7.
[[gnu::always_inline]] inline uint32_t Zone(uint32_t dst, uint32_t k) { return (dst + 128u * k) & ~31u; }

}  // namespace blur_b2

// ---------------------------------------------------------------------------------------------------------------------
// sub_82954F28 - f1 = g(f1): a reduction by a multiple of a constant (fctid / fcfid / two fnmsub), a ratio of two
// polynomials in the square of the reduced value (fdiv), then the exponent of the result adjusted by the integer part
// (done on the bits of the double), and two fsel for out-of-range arguments. 56 guest instructions, no call.
// Console samples (race, JQ Worker 3 58 %, 4 37 %; 1.17 % of a core): the original stores f0 at r1 - 16 three times
// and reloads halfwords of it (a doubleword store read back narrower, which the A57 cannot forward), stores fctiwz of
// the integer part and reloads its low word (stfd, lwz), and writes f2-f13 and r8-r10 to the context. The native keeps
// all of that in registers: the exponent step works on the bits of the double (sign and top four mantissa bits kept,
// the exponent field plus the integer part, as the original computes it).
// Writes the one doubleword at r1 - 16 that the original leaves there: the fctiwz image when the integer part plus one
// is zero (the original then skips the exponent step), else the adjusted double. Flush mode off, as the original.
// Declines: the 8 bytes at r1 - 16 wrapping or lying in 0x82000000-0x83000010 (where the constants are read).
// Scratch not reproduced (hand.txt exact=f1): f2-f13, r8-r10, cr0, xer.
// ---------------------------------------------------------------------------------------------------------------------
// blur-nx (2026-11-13, #43 of docs/cpu-near-exact-50-1113.md: memoize hot pure functions; exact): with
// cpu_nx_exp_memo, a table per thread of 64 recent (argument bits, result, the doubleword left at r1 - 16): the
// function is pure (its constants are in the image's read-only data, the flush mode is set off first), so an argument
// seen before gives the same result and the same stack word, which a hit writes as the computation would. The audio
// parameter fetch 822EB500 calls it with the same dB offsets frame after frame (a subagent's reading, 2026-11-13).
extern "C" bool blur_nx_exp_memo;  // app/src/blur_nx_mix.cpp
namespace n_82954F28 {
struct Memo {
  uint64_t x, result, mem;
  bool valid;
};
template <class S>
[[gnu::always_inline]] inline bool Native(PPCContext& __restrict ctx, uint8_t* base, S& s) {
  using namespace blur_b2;
  if (!On(kN82954F28)) [[unlikely]] return false;
  const uint32_t sp = ctx.r1.u32;
  if (!detail::Straight(sp - 16u, 8) || uint32_t(sp - 0x82000000u) < 0x01000010u) [[unlikely]] return false;
  s.Hand(kN82954F28, 0);
  const double x = ctx.f1.f64;
  Memo* slot = nullptr;
  if (blur_nx_exp_memo) {  // blur-nx (2026-11-13, #43)
    thread_local Memo memo[64];
    const uint64_t xb = U64(x);
    slot = &memo[(xb ^ (xb >> 23) ^ (xb >> 41)) & 63u];
    if (slot->valid && slot->x == xb) {
      REX_NX_CENSUS(31);
      if (!s.Note(sp - 16u, 8)) [[unlikely]] return true;
      ::banjo::hot::Card(base, sp - 16u, 8);
      St64(::banjo::hot::Raw(base, sp - 16u), slot->mem);
      ctx.f1.u64 = slot->result;
      return true;
    }
    REX_NX_CENSUS(32);
  }
  ctx.fpscr.disableFlushMode();
  Fence();
  const uint8_t* t = ::banjo::hot::Raw(base, 0x82C40350u);  // lis r11,-32060; addi r11,r11,848
  const double c0 = D64(Ld64(t)), c8 = D64(Ld64(t + 8)), c16 = D64(Ld64(t + 16)), c32 = D64(Ld64(t + 32));
  const double c40 = D64(Ld64(t + 40)), c48 = D64(Ld64(t + 48)), c56 = D64(Ld64(t + 56)), c64 = D64(Ld64(t + 64));
  const double c72 = D64(Ld64(t + 72)), c80 = D64(Ld64(t + 80)), c88 = D64(Ld64(t + 88)), c96 = D64(Ld64(t + 96));
  const double f104 = double(F32(Ld32(t + 104))), f108 = double(F32(Ld32(t + 108)));
  const double scaled = c32 * x;                         // fmul f5,f0,f1
  const double n = double(::rex::ppc::fctid(scaled));    // fctid f5,f5; fcfid f5,f5
  double f13 = -std::fma(c40, n, -x);                    // fnmsub f13,f13,f5,f1
  const int64_t whole = ::rex::ppc::fctiwz(n);           // fctiwz f4,f5; stfd f4,-16(r1)
  const uint32_t r10 = uint32_t(uint64_t(whole)) + 1u;   // lwz r10,-12(r1); addic. r10,r10,1
  f13 = -std::fma(c48, n, -f13);                         // fnmsub f13,f12,f5,f13
  double f12 = f13 * f13;                                // fmul f12,f13,f13
  double f11 = std::fma(c72, f12, c64);                  // fmadd f11,f11,f12,f10
  const double f10 = std::fma(c96, f12, c88);            // fmadd f10,f9,f12,f8
  f11 = std::fma(f11, f12, c56);                         // fmadd f11,f11,f12,f7
  f12 = std::fma(f10, f12, c80);                         // fmadd f12,f10,f12,f6
  f13 = f11 * f13;                                       // fmul f13,f11,f13
  f12 = f12 - f13;                                       // fsub f12,f12,f13
  f13 = f13 / f12;                                       // fdiv f13,f13,f12
  double f0 = f13 + f108;                                // fadd f0,f13,f0
  uint64_t mem = uint64_t(whole);                        // what is at r1 - 16 at the end
  if (r10 != 0) {
    uint64_t b = U64(f0);                                // stfd f0,-16(r1)
    const uint32_t hi = uint32_t(b >> 48);               // lhz r8,-16(r1) / lhz r9,-16(r1)
    const uint32_t keep = hi & 32783u;                   // andi. r8,r8,32783
    const int32_t e = int32_t((hi >> 4) & 0x7FFu) - 1022;  // rlwinm r9,r9,28,21,31; addi r9,r9,-1022; extsh
    uint32_t r = uint32_t(e) + r10;                      // add r10,r9,r10 (only the low word is used below)
    r += 1022u;
    r <<= 4;                                             // rlwinm r10,r10,4,0,27
    r |= keep;
    b = (b & 0x0000FFFFFFFFFFFFull) | (uint64_t(uint16_t(r)) << 48);  // sth r10,-16(r1)
    f0 = D64(b);                                         // lfd f0,-16(r1)
    mem = b;
  }
  const double f11b = x - c0;                            // fsub f11,f1,f13
  const double f10b = c8 - x;                            // fsub f10,f13,f1
  f0 = ::rex::ppc::GeZeroAnyMode(f11b) ? c16 : f0;       // fsel f0,f11,f12,f0
  double result = ::rex::ppc::GeZeroAnyMode(f10b) ? f104 : f0;  // fsel f1,f10,f13,f0
  Pin(result);
  if (slot) {  // blur-nx (2026-11-13, #43)
    slot->x = U64(x);
    slot->result = U64(result);
    slot->mem = mem;
    slot->valid = true;
  }
  if (!s.Note(sp - 16u, 8)) [[unlikely]] return true;
  ::banjo::hot::Card(base, sp - 16u, 8);
  St64(::banjo::hot::Raw(base, sp - 16u), mem);
  ctx.f1.f64 = result;
  return true;
}
}  // namespace n_82954F28

// ---------------------------------------------------------------------------------------------------------------------
// sub_822E48F8 - a linear gain ramp over n floats: r3 = destination, r4 = source (may be the same buffer), r7 = n,
// f1 = the gain at element 0, f2 = the gain after n steps: dst[i] = src[i] * (f1 + i * (f2 - f1) / n), each step a
// single-precision operation (fsubs, fdivs, fmadds, fmuls). First, when dst != src, eight dcbz (32 zero bytes at
// (dst + 128 k) & ~31, k = 0..7). Unrolled by four, then a tail.
// Console samples (race, JQ Worker 3 78 %, 4 22 %; 1.42 % of a core): the original converts the index of every element
// to a double through the stack (std of the 64-bit index, lfd of it, fcfid: four store/reload pairs per four
// elements, each a failed store forwarding) and recomputes addresses. The native converts the index directly (double
// of the integer, then single rounding), forms the ramp value and the product per element in registers, and marks the
// cards once per range.
// Writes: the eight zero zones (dst != src only), dst[0..n), and the four stack doublewords the original leaves at
// r1 - 64, -56, -48, -40 (the indices of the last group and of the last tail element; a doubleword the original never
// writes keeps its bytes). Flush mode off, as the original.
// Declines: n negative as a signed number; any range wrapping or crossing the physical-view boundary; with dst != src,
// the source overlapping the destination or its zones; the stack words overlapping the buffers or the zones.
// Scratch not reproduced (hand.txt exact=, none): f0-f13, r5-r11, cr6.
// ---------------------------------------------------------------------------------------------------------------------
namespace n_822E48F8 {
template <class S>
[[gnu::always_inline]] inline bool Native(PPCContext& __restrict ctx, uint8_t* base, S& s) {
  using namespace blur_b2;
  if (!On(kN822E48F8)) [[unlikely]] return false;
  const uint32_t dst = ctx.r3.u32, src = ctx.r4.u32, n = ctx.r7.u32, sp = ctx.r1.u32;
  if (int32_t(n) < 0) [[unlikely]] return false;
  const uint32_t bytes = n * 4u;
  const bool zero = dst != src;
  const uint32_t dlo = dst & ~31u;
  const uint32_t dlen = (dst - dlo) + (bytes > 928u ? bytes : 928u) + 32u;  // covers the zones and dst[0..n)
  if (n != 0 && (!detail::Straight(dst, bytes) || !detail::Straight(src, bytes))) [[unlikely]] return false;
  if (zero && !detail::Straight(dlo, dlen)) [[unlikely]] return false;
  if (!detail::Straight(sp - 64u, 32)) [[unlikely]] return false;
  if (zero && n != 0 && Hit(src, bytes, dlo, dlen)) [[unlikely]] return false;
  if (Hit(sp - 64u, 32, dlo, dlen) || (n != 0 && Hit(sp - 64u, 32, src, bytes))) [[unlikely]] return false;
  s.Hand(kN822E48F8, bytes);
  const double g1 = ctx.f1.f64, g2 = ctx.f2.f64;
  ctx.fpscr.disableFlushMode();
  Fence();
  const double span = double(float(g2 - g1));              // fsubs f0,f2,f1
  const double nf = double(float(double(int64_t(n))));     // std r11,-64(r1); lfd; fcfid; frsp
  const double step = REX_FDIVS(span, nf);                 // fdivs f0,f0,f11
  const uint32_t groups = n / 4u;
  const bool tail = groups * 4u < n;
  uint64_t w64 = n, w56 = 0, w48 = 0, w40 = 0;
  bool s56 = false, s48 = false, s40 = false;
  if (groups) {
    const uint32_t il = 4u * (groups - 1u);
    w64 = il;
    w56 = il + 1u;
    w48 = il + 2u;
    w40 = il + 3u;
    s56 = s48 = s40 = true;
  }
  if (tail) {
    w40 = n - 1u;
    s40 = true;
  }
  if (zero) {
    for (uint32_t k = 0; k < 8; ++k)
      if (!s.Note(Zone(dst, k), 32)) [[unlikely]] return true;
  }
  if (n != 0 && !s.Note(dst, bytes)) [[unlikely]] return true;
  if (!s.Note(sp - 64u, 32)) [[unlikely]] return true;
  if (zero) {
    for (uint32_t k = 0; k < 8; ++k) {
      ::banjo::hot::Card(base, Zone(dst, k), 32);
      std::memset(::banjo::hot::Raw(base, Zone(dst, k)), 0, 32);
    }
  }
  if (n != 0) {
    ::banjo::hot::Card(base, dst, bytes);
    const uint8_t* p = ::banjo::hot::Raw(base, src);
    uint8_t* d = ::banjo::hot::Raw(base, dst);
    for (uint32_t i = 0; i < n; ++i) {
      const double idx = double(float(double(int64_t(i))));         // fcfid; frsp
      const double ramp = REX_FMADDS(idx, step, g1);   // fmadds f2,f6,f0,f1
      const double in = double(F32(Ld32(p + 4u * i)));              // lfs
      St32(d + 4u * i, U32(float(REX_FMULS(ramp, in))));            // fmuls; stfs
    }
  }
  Fence();
  {
    uint8_t* st = ::banjo::hot::Raw(base, sp - 64u);
    ::banjo::hot::Card(base, sp - 64u, 32);
    const uint64_t o56 = s56 ? w56 : Ld64(st + 8), o48 = s48 ? w48 : Ld64(st + 16), o40 = s40 ? w40 : Ld64(st + 24);
    St64(st, w64);
    St64(st + 8, o56);
    St64(st + 16, o48);
    St64(st + 24, o40);
  }
  return true;
}
}  // namespace n_822E48F8

// ---------------------------------------------------------------------------------------------------------------------
// sub_82520D78 - one vector of a product of three scalars, stored (aligned) at r3: r4 = an object, r5 = an index.
// o = [r4 + 224], rec = [o + 7044]; each scalar is the first word of an lvlx128 found through them, splatted:
//   r5 >= 1:                  ([rec + 328] * [o + 6008]) * [[rec + 312] + 4 r5 - 4]
//   r5 < 1, float [rec + 320] > 0: sign flipped of ([rec + 328] * [o + 6008]) * [rec + 320]
//   otherwise:                sign flipped of ([rec + 328] * [o + 6008]) * [[rec + 312]]
// (multiplications left to right with the operand order of the vmulfp128 statements; the sign flip is an xor with
// 0x80000000). Console samples (race, Game Master 22 %, JQ Worker 4 33 %, 1 22 %; 0.56 % of a core): the machine
// copy leaves 14-20 vector temporaries as context locals per path and switches the mode on in each path. The native
// holds the splats in registers, switches once (on, as the original ends) and marks one card.
// Declines (nothing written): any pointer word or lvlx128 block read, or the destination block, wrapping 4 GB or
// crossing the physical-view boundary. The destination may overlap the sources (it is stored last, as in the original).
// Scratch not reproduced (hand.txt exact=, none): r5-r11, f0, f13, the vector temporaries.
// ---------------------------------------------------------------------------------------------------------------------
namespace n_82520D78 {
template <class S>
[[gnu::always_inline]] inline bool Native(PPCContext& __restrict ctx, uint8_t* base, S& s) {
  using namespace blur_b2;
  if (!On(kN82520D78)) [[unlikely]] return false;
  const uint32_t dst = ctx.r3.u32, obj = ctx.r4.u32;
  const int32_t cnt = ctx.r5.s32;
  if (!detail::Straight(obj + 224u, 4) || !Blk(dst)) [[unlikely]] return false;
  const uint32_t o = Ld32(::banjo::hot::Raw(base, obj + 224u));
  if (!detail::Straight(o + 7044u, 4) || !Blk(o + 6008u)) [[unlikely]] return false;
  const uint32_t rec = Ld32(::banjo::hot::Raw(base, o + 7044u));
  if (!Blk(rec + 328u) || !detail::Straight(rec + 312u, 4) || !Blk(rec + 320u) ||
      !detail::Straight(rec + 320u, 4)) [[unlikely]]
    return false;
  const V sign = simde_mm_set1_epi32(int(0x80000000u));
  uint32_t third;  // the address of the third lvlx128
  bool flip = false;
  if (cnt >= 1) {
    third = Ld32(::banjo::hot::Raw(base, rec + 312u)) + (uint32_t(cnt) << 2) - 4u;  // [rec+312] + 4 r5, then -4
  } else {
    const double f13 = ::rex::ppc::SingleToDoubleAnyMode(Ld32(::banjo::hot::Raw(base, rec + 320u)));
    PPCCRRegister cr{};
    ::rex::ppc::CompareAnyMode(cr, f13, 0.0);  // fcmpu cr6,f13,f0 with f0 = 0.0 (0x82000BA0)
    flip = true;
    third = cr.gt ? rec + 320u : Ld32(::banjo::hot::Raw(base, rec + 312u));
  }
  if (!Blk(third)) [[unlikely]] return false;
  F a = ToF(Splat0(LvlxV(base, o + 6008u)));
  F b = ToF(Splat0(LvlxV(base, rec + 328u)));
  F c = ToF(Splat0(LvlxV(base, third)));
  ctx.fpscr.enableFlushMode();
  Fence();
  Pin(a);
  Pin(b);
  Pin(c);
  F r = simde_mm_mul_ps(simde_mm_mul_ps(b, a), c);
  V out = ToI(r);
  if (flip) out = simde_mm_xor_si128(out, sign);
  s.Hand(kN82520D78, 0);
  if (!s.Note(dst & ~15u, 16)) [[unlikely]] return true;
  ::banjo::hot::Card(base, dst & ~15u, 16);
  StV(base, dst, out);
  return true;
}
}  // namespace n_82520D78

// ---------------------------------------------------------------------------------------------------------------------
// sub_8230CBB8 - a two-input crossfade of 1024 bytes (8 x 128): r3 = destination, r4 = A, r5 = B (float buffers of 256
// words), r6 = pointer to the start gain, r7 = pointer to the end gain (first words read with lvlx128), and a third
// scalar at 0x82B9D114; per 128-byte block k: dst = B * (X + lane ramp) + A with the ramp gains (v13 = (end - start) *
// scalar; the lane offsets 0..31 added in steps of 32 per block) as vmaddfp (x * v13 + start) then vmaddfp
// (B * ramp + A). When dst differs from A and from B, eight dcbz (32 zero bytes at (dst + 128 k) & ~31) come first.
// Console samples (race, JQ Worker 3 61 %, 4 39 %; 2.04 % of a core, 33 samples): the machine copy keeps the 16
// loaded vectors, 8 gains and 8 results of a block in registers, byte-swaps each load and store separately, marks a
// store card after each of the 8 stores per block (64 per call) and re-enables the flush mode check per block. The
// native marks one card for the 1024 bytes, enables the mode once and runs the 8 blocks over the same register values.
// Writes: the zones (when dst is neither A nor B) and dst[0..1024). Flush mode on at the end, as the original.
// Declines: any range wrapping or crossing the physical-view boundary; a source that is neither dst itself nor
// disjoint from the destination and its zones; one of the three scalar blocks overlapping the destination or zones.
// Scratch not reproduced (hand.txt exact=, none): r6-r11, r25-r31 (locals), v0-v13 and the temporaries.
// ---------------------------------------------------------------------------------------------------------------------
namespace n_8230CBB8 {
template <class S>
[[gnu::always_inline]] inline bool Native(PPCContext& __restrict ctx, uint8_t* base, S& s) {
  using namespace blur_b2;
  if (!On(kN8230CBB8)) [[unlikely]] return false;
  const uint32_t dst = ctx.r3.u32, pa = ctx.r4.u32, pb = ctx.r5.u32, p6 = ctx.r6.u32, p7 = ctx.r7.u32;
  constexpr uint32_t kGain = 0x82B9D114u;  // lis r11,-32070; addi r9,r11,-12012
  const bool zero = pa != dst && pb != dst;
  const uint32_t dlo = dst & ~31u, dlen = (dst - dlo) + 1024u + 32u;
  if (!detail::Straight(dst, 1024) || !detail::Straight(pa, 1024) || !detail::Straight(pb, 1024) ||
      !detail::Straight(dlo, dlen) || !Blk(p6) || !Blk(p7)) [[unlikely]]
    return false;
  if ((pa != dst && Hit(pa, 1024, dlo, dlen)) || (pb != dst && Hit(pb, 1024, dlo, dlen)) ||
      Hit(p6 & ~15u, 16, dlo, dlen) || Hit(p7 & ~15u, 16, dlo, dlen)) [[unlikely]]
    return false;
  s.Hand(kN8230CBB8, 1024);
  if (zero) {
    for (uint32_t k = 0; k < 8; ++k)
      if (!s.Note(Zone(dst, k), 32)) [[unlikely]] return true;
  }
  if (!s.Note(dst, 1024)) [[unlikely]] return true;
  if (zero) {
    for (uint32_t k = 0; k < 8; ++k) {
      ::banjo::hot::Card(base, Zone(dst, k), 32);
      std::memset(::banjo::hot::Raw(base, Zone(dst, k)), 0, 32);
    }
  }
  ::banjo::hot::Card(base, dst, 1024);
  F v61 = ToF(Splat0(LvlxV(base, p7)));    // lvlx128 v62,r0,r7; vspltw128 v61,v62,0
  F v0 = ToF(Splat0(LvlxV(base, p6)));     // lvlx128 v63,r0,r6; vspltw128 v0,v63,0
  F v58 = ToF(Splat0(LvlxV(base, kGain)));  // lvlx128 v59; vspltw128 v58,v59,0
  ctx.fpscr.enableFlushMode();
  Fence();
  Pin(v61);
  Pin(v0);
  Pin(v58);
  const F v60 = simde_mm_sub_ps(v61, v0);   // vsubfp128 v60,v61,v0
  const F v13 = simde_mm_mul_ps(v60, v58);  // vmulfp128 v13,v60,v58
  F g0 = ToF(REX_RO_V128(0x8202E510, 0x000000003F800000ull, 0x4000000040400000ull));  // v62
  F g1 = ToF(REX_RO_V128(0x8202E520, 0x4080000040A00000ull, 0x40C0000040E00000ull));  // v61
  F g2 = ToF(REX_RO_V128(0x8202E530, 0x4100000041100000ull, 0x4120000041300000ull));  // v12
  F g3 = ToF(REX_RO_V128(0x8202E540, 0x4140000041500000ull, 0x4160000041700000ull));  // v11
  F g4 = ToF(REX_RO_V128(0x8202E550, 0x4180000041880000ull, 0x4190000041980000ull));  // v10
  F g5 = ToF(REX_RO_V128(0x8202E560, 0x41A0000041A80000ull, 0x41B0000041B80000ull));  // v9
  F g6 = ToF(REX_RO_V128(0x8202E570, 0x41C0000041C80000ull, 0x41D0000041D80000ull));  // v8
  F g7 = ToF(REX_RO_V128(0x8202E580, 0x41E0000041E80000ull, 0x41F0000041F80000ull));  // v7
  const F step = ToF(REX_RO_V128(0x8202E500, 0x4200000042000000ull, 0x4200000042000000ull));  // v63
  for (uint32_t it = 0; it < 8; ++it) {
    const uint32_t oa = pa + 128u * it, ob = pb + 128u * it, od = dst + 128u * it;
    const F b0 = ToF(LdV(base, ob)), a0 = ToF(LdV(base, oa));
    const F b1 = ToF(LdV(base, ob + 16)), a1 = ToF(LdV(base, oa + 16));
    const F b2 = ToF(LdV(base, ob + 32)), a2 = ToF(LdV(base, oa + 32));
    const F b3 = ToF(LdV(base, ob + 48)), a3 = ToF(LdV(base, oa + 48));
    const F b4 = ToF(LdV(base, ob + 64)), a4 = ToF(LdV(base, oa + 64));
    const F b5 = ToF(LdV(base, ob + 80)), a5 = ToF(LdV(base, oa + 80));
    const F b6 = ToF(LdV(base, ob + 96)), a6 = ToF(LdV(base, oa + 96));
    const F b7 = ToF(LdV(base, ob + 112)), a7 = ToF(LdV(base, oa + 112));
    const F v22 = REX_VMADDFP(g2, v13, v0);
    const F v21 = REX_VMADDFP(g3, v13, v0);
    const F v20 = REX_VMADDFP(g4, v13, v0);
    const F v19 = REX_VMADDFP(g5, v13, v0);
    const F t6 = REX_VMADDFP(g0, v13, v0);
    const F t5 = REX_VMADDFP(g1, v13, v0);
    const F v18 = REX_VMADDFP(g6, v13, v0);
    const F v17 = REX_VMADDFP(g7, v13, v0);
    g0 = simde_mm_add_ps(g0, step);
    g1 = simde_mm_add_ps(g1, step);
    g2 = simde_mm_add_ps(g2, step);
    g3 = simde_mm_add_ps(g3, step);
    g4 = simde_mm_add_ps(g4, step);
    g5 = simde_mm_add_ps(g5, step);
    g6 = simde_mm_add_ps(g6, step);
    g7 = simde_mm_add_ps(g7, step);
    const F o3 = REX_VMADDFP(b3, v21, a3);
    const F o4 = REX_VMADDFP(b4, v20, a4);
    const F o5 = REX_VMADDFP(b5, v19, a5);
    const F o0 = REX_VMADDFP(b0, t6, a0);
    const F o1 = REX_VMADDFP(b1, t5, a1);
    const F o2 = REX_VMADDFP(b2, v22, a2);
    const F o6 = REX_VMADDFP(b6, v18, a6);
    const F o7 = REX_VMADDFP(b7, v17, a7);
    StV(base, od, ToI(o0));
    StV(base, od + 16, ToI(o1));
    StV(base, od + 32, ToI(o2));
    StV(base, od + 48, ToI(o3));
    StV(base, od + 64, ToI(o4));
    StV(base, od + 80, ToI(o5));
    StV(base, od + 96, ToI(o6));
    StV(base, od + 112, ToI(o7));
  }
  return true;
}
}  // namespace n_8230CBB8

// ---------------------------------------------------------------------------------------------------------------------
// sub_82244170 - an unaligned run of vectors copied into an object and a flag word or-ed in: r3 = object, r4 = slot,
// r5 = source (any alignment), r6 = number of 16-byte vectors, r7 = 64-bit mask. The vectors go to the aligned
// addresses r3 + 16 (r4 + 120) + 16 k (lvlx128 + lvrx128 pairs or-ed together = the 16 bytes at the source), then
// [r3] |= r7 (ld, or, std). Console samples (race, JQ Worker 4 33 %, 0 22 %, 2 22 %; 0.56 % of a core): two unaligned-load
// instructions, a shuffle-table lookup and an or per 16 bytes, a store card per store, in groups of four.
// The native copies the bytes with one memcpy (the byte image the pairs assemble is the source bytes), marks one card
// and does the or on the doubleword. Writes: the vector run and [r3, r3 + 8).
// Declines: more than 2^20 vectors; any range wrapping or crossing the physical-view boundary; the source overlapping
// the destination run; the object doubleword overlapping the run (the original reads it after the stores).
// Scratch not reproduced (hand.txt exact=, none): r4-r11, v60-v63.
// ---------------------------------------------------------------------------------------------------------------------
namespace n_82244170 {
template <class S>
[[gnu::always_inline]] inline bool Native(PPCContext& __restrict ctx, uint8_t* base, S& s) {
  using namespace blur_b2;
  if (!On(kN82244170)) [[unlikely]] return false;
  const uint32_t obj = ctx.r3.u32, src = ctx.r5.u32, n = ctx.r6.u32;
  if (n > (1u << 20)) [[unlikely]] return false;
  const uint32_t dst = (obj + ((ctx.r4.u32 + 120u) << 4)) & ~15u;
  const uint32_t bytes = n * 16u;
  if (!detail::Straight(obj, 8)) [[unlikely]] return false;
  if (n != 0) {
    if (!detail::Straight(dst, bytes) || !detail::Straight(src, bytes) || Hit(dst, bytes, src, bytes) ||
        Hit(dst, bytes, obj, 8)) [[unlikely]]
      return false;
  }
  s.Hand(kN82244170, bytes);
  if (n != 0) {
    if (!s.Note(dst, bytes)) [[unlikely]] return true;
  }
  if (!s.Note(obj, 8)) [[unlikely]] return true;
  if (n != 0) {
    ::banjo::hot::Card(base, dst, bytes);
    std::memcpy(::banjo::hot::Raw(base, dst), ::banjo::hot::Raw(base, src), bytes);
  }
  ::banjo::hot::Card(base, obj, 8);
  uint8_t* o = ::banjo::hot::Raw(base, obj);
  St64(o, Ld64(o) | ctx.r7.u64);
  return true;
}
}  // namespace n_82244170

// ---------------------------------------------------------------------------------------------------------------------
// sub_822E9F20 - the sum of two float buffers of 1024 bytes: r3 = destination, r4 = A, r5 = B; dst = A + B per
// 16-byte vector (vaddfp128 with A first), 4 groups of 16 vectors. When dst is neither A nor B, eight dcbz (32 zero
// bytes at (dst + 128 k) & ~31) come first. Console samples (race, JQ Worker 3 64 %, 4 36 %; 0.68 % of a core): 64
// loads and 64 stores, each byte-swapped and each store card-marked, 16 vectors live per group.
// The native marks one card for the 1024 bytes and runs the 64 adds in a tight loop on the same values.
// Writes: the zones (when dst is neither A nor B) and dst[0..1024). Flush mode on at the end, as the original.
// Declines: any range wrapping or crossing the physical-view boundary; a source that is neither dst itself nor
// disjoint from the destination and its zones.
// Scratch not reproduced (hand.txt exact=, none): r6-r11, r22-r31 (locals), v32-v63.
// ---------------------------------------------------------------------------------------------------------------------
namespace n_822E9F20 {
template <class S>
[[gnu::always_inline]] inline bool Native(PPCContext& __restrict ctx, uint8_t* base, S& s) {
  using namespace blur_b2;
  if (!On(kN822E9F20)) [[unlikely]] return false;
  const uint32_t dst = ctx.r3.u32, pa = ctx.r4.u32, pb = ctx.r5.u32;
  const bool zero = pa != dst && pb != dst;
  const uint32_t dlo = dst & ~31u, dlen = (dst - dlo) + 1024u + 32u;
  if (!detail::Straight(dst, 1024) || !detail::Straight(pa, 1024) || !detail::Straight(pb, 1024) ||
      !detail::Straight(dlo, dlen)) [[unlikely]]
    return false;
  if ((pa != dst && Hit(pa, 1024, dlo, dlen)) || (pb != dst && Hit(pb, 1024, dlo, dlen))) [[unlikely]] return false;
  s.Hand(kN822E9F20, 1024);
  if (zero) {
    for (uint32_t k = 0; k < 8; ++k)
      if (!s.Note(Zone(dst, k), 32)) [[unlikely]] return true;
  }
  if (!s.Note(dst, 1024)) [[unlikely]] return true;
  if (zero) {
    for (uint32_t k = 0; k < 8; ++k) {
      ::banjo::hot::Card(base, Zone(dst, k), 32);
      std::memset(::banjo::hot::Raw(base, Zone(dst, k)), 0, 32);
    }
  }
  ::banjo::hot::Card(base, dst, 1024);
  ctx.fpscr.enableFlushMode();
  Fence();
  for (uint32_t k = 0; k < 64; ++k) {
    const F a = ToF(LdV(base, pa + 16u * k));
    const F b = ToF(LdV(base, pb + 16u * k));
    StV(base, dst + 16u * k, ToI(simde_mm_add_ps(a, b)));  // vaddfp128 v45,v63,v47: A + B
  }
  return true;
}
}  // namespace n_822E9F20
