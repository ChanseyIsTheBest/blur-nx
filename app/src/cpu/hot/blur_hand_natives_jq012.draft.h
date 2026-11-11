// blur-nx (2026-11-11): hand-written natives for the hot game functions of the job workers 0-2 in the console race
// profile (2026-11-11 15:18, rebased build 46b3f6d6). DRAFT: included inside `namespace banjo::hand` of
// banjo_hand_natives.h once it has its final name (blur_hand_natives_jq012.h). Two natives are written here: the sine
// sub_82954478 and the function that calls it twice, sub_82521E68. The other eight functions of the request were
// read and not written (docs/hand-natives-jq012.md says why, one by one).
//
// No #include lines: the file is read in the middle of the namespace, after banjo_hand_natives.h's own helpers.
#pragma once

namespace blur_jq012 {

// Big-endian guest words through a host pointer (any alignment).
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

// The host's flush-mode switch is an `msr fpcr` without memory operand: the compiler may move arithmetic held in
// registers across it. Fence orders the loads after it; Pin ties a register value to the place where it stands.
// (The same two as Katamari's natives: the base-update branch of banjo_hand_natives.h.)
[[gnu::always_inline]] inline void Fence() { __asm__ __volatile__("" ::: "memory"); }
template <class T>
[[gnu::always_inline]] inline void Pin(T& x) {
#if defined(__aarch64__)
  __asm__ __volatile__("" : "+w"(x));
#elif defined(__x86_64__) || defined(_M_X64)
  __asm__ __volatile__("" : "+x"(x));
#endif
}

// The byte ranges [a, a + an) and [b, b + bn) share a byte (neither wraps 4 GB: the caller has checked Straight).
[[gnu::always_inline]] inline bool Hit(uint32_t a, uint32_t an, uint32_t b, uint32_t bn) {
  return uint32_t(a - b) < bn || uint32_t(b - a) < an;
}

// lvx128: the aligned 16 bytes at ea (rounded down), byte-reversed per word as the generated code does.
[[gnu::always_inline]] inline simde__m128i LdV(uint8_t* base, uint32_t ea) {
  return REX_BSWAP128(simde_mm_load_si128((simde__m128i*)REX_RAW_ADDR(ea & ~0xFu)));
}
// lvlx128 (the generated statement): the bytes from ea to the end of its 16-byte block, left-justified, rest zero.
[[gnu::always_inline]] inline simde__m128i LvlxV(uint8_t* base, uint32_t ea) {
  return REX_SHUFFLE_TABLE(simde_mm_load_si128((simde__m128i*)REX_RAW_ADDR(ea & ~0xFu)), &VectorMaskL[(ea & 0xFu) * 16]);
}

// ---------------------------------------------------------------------------------------------------------------------
// The game's sine, sub_82954478 (see the block of n_82954478 below): the constants and the arithmetic.
// ---------------------------------------------------------------------------------------------------------------------
struct SinK {
  double c8, c16, c40, c48, c56, c64, c72, c80, c88, c96, c104, c112;  // doubles at 0x82C40200 + offset
  double flo, fhi;                                                     // the floats at +32 and +28, widened
  double one;                                                          // 0x82005090
  double big;                                                          // 0x82C40908
};
[[gnu::always_inline]] inline SinK LoadSinK(uint8_t* base) {
  const uint8_t* t = ::banjo::hot::Raw(base, 0x82C40200u);
  SinK k;
  k.c8 = D64(Ld64(t + 8));
  k.c16 = D64(Ld64(t + 16));
  k.c40 = D64(Ld64(t + 40));
  k.c48 = D64(Ld64(t + 48));
  k.c56 = D64(Ld64(t + 56));
  k.c64 = D64(Ld64(t + 64));
  k.c72 = D64(Ld64(t + 72));
  k.c80 = D64(Ld64(t + 80));
  k.c88 = D64(Ld64(t + 88));
  k.c96 = D64(Ld64(t + 96));
  k.c104 = D64(Ld64(t + 104));
  k.c112 = D64(Ld64(t + 112));
  k.flo = double(F32(Ld32(t + 32)));
  k.fhi = double(F32(Ld32(t + 28)));
  k.one = D64(Ld64(::banjo::hot::Raw(base, 0x82005090u)));
  k.big = D64(Ld64(::banjo::hot::Raw(base, 0x82C40908u)));
  return k;
}

// The sine's arithmetic, operation for operation as the generated code has it (double precision, the fused
// multiply-adds only where the original has fmadd / fnmsub). abs_bits / trunc_bits are the two doublewords the
// original leaves on the stack at r1 - 24 and r1 - 32. Exact in either flush mode as long as no operation sees or
// makes a denormal double: the caller guarantees |x| is zero, a normal double, or a widened single (>= 2^-149).
[[gnu::always_inline]] inline double SinEval(const SinK& k, double x, uint64_t& abs_bits, uint64_t& trunc_bits) {
  abs_bits = U64(x) & 0x7FFFFFFFFFFFFFFFull;  // fabs f0,f1
  const double f0 = D64(abs_bits);
  const double scaled = k.c8 * f0;                          // fmul f13,f11,f0
  const int64_t rounded = ::rex::ppc::fctid(scaled);        // fctid f11,f13
  const double f12 = ::rex::ppc::GeZeroAnyMode(x) ? k.fhi : k.flo;  // fsel f12,f1,f13,f12
  const double n = double(rounded);                         // fcfid f13,f11
  const double r1 = -std::fma(k.c40, n, -f0);               // fnmsub f11,f10,f13,f0
  const int64_t z = ::rex::ppc::fctidz(n);                  // fctidz f10,f13 (stored at r1 - 32, loaded back as r9)
  trunc_bits = uint64_t(z);
  const bool odd = (z & 1) != 0;                            // clrldi r8,r9,63; cmpdi cr6,r8,0
  const double f9 = -std::fma(k.c48, n, -r1);               // fnmsub f9,f9,f13,f11
  const double sq = f9 * f9;                                // fmul f13,f9,f9
  double p = std::fma(k.c112, sq, k.c104);
  p = std::fma(p, sq, k.c96);
  p = std::fma(p, sq, k.c88);
  p = std::fma(p, sq, k.c80);
  p = std::fma(p, sq, k.c72);
  p = std::fma(p, sq, k.c64);
  p = std::fma(p, sq, k.c56);
  p = std::fma(p, sq, k.one);
  double v = p * f9;                                        // fmul f13,f3,f9
  if (odd) v = -v;                                          // fneg f13,f13 (a sign flip, NaN included)
  v = v * f12;                                              // fmul f12,f13,f12
  if (abs_bits == 0) return x;                              // cmpdi r10,0; beq: f1 stays as it came (+-0)
  const double diff = f0 - k.c16;                           // fsub f13,f0,f13
  return ::rex::ppc::GeZeroAnyMode(diff) ? k.big : v;       // fsel f1,f13,f0,f12
}

}  // namespace blur_jq012

// sub_82954478 - the game's sine, f1 = sin(f1) (32 % of the job workers' sine calls come from sub_82521E68 and
// sub_8250A7E8, which call it two and four times). Range reduction by pi (fctid of |x| / pi through the stack:
// stfd f10,-32(r1); ld r9,-32(r1)), a degree-17 polynomial in x^2 by eight fused multiply-adds, the sign from the
// parity of the reduction, the result forced to a constant for |x| beyond a limit (fsel). 56 guest instructions, no
// call. Console samples (race, job workers 4, 0, 1; 2.3 % of one core): spread over the context traffic of the
// function's own scratch registers (stp d..,[x0,#360...] of f2-f13 and the integer r8-r10: +0x160-+0x198 is 14 of 45
// samples) and the entry load of f1 that the caller has just stored (+0x000: 6). What the native removes: the 12 scratch
// float registers and r8-r10 written to the context (the original leaves them, the native does not), the stack
// store/reload pair through memory (the two doublewords are still written, the reloads are not), and, because the
// native is inlined in the copies that call this function, the call itself and the f1 round trip through the context.
// Writes: the doubleword of |x| at r1 - 24, the truncated reduction at r1 - 32, and (unless REX_NV_SAVES_ELIDE) the
// two zero save slots at r1 - 16 and r1 - 8, as the original; marks their store cards. Ends with the flush mode OFF
// as the original does (disableFlushMode at its first lfs), f1 = the result (unchanged for +-0).
// Declines: a stack pointer whose 32 bytes below it wrap 4 GB or cross the physical-view boundary, or that touch
// the image's read-only data (0x82000000-0x83000000, where the constants are read), so the reloads of the original
// cannot differ from the values used here. Signalling NaN: f1 is an ordinary double here (no single widening), the
// same operations run in the same order, so the same bits come out; nothing to decline.
// Scratch not reproduced (hand.txt exact=f1): f2-f13, r8, r9, r10 (r10 = the bits of |x| in the original).
namespace n_82954478 {
template <class S>
[[gnu::always_inline]] inline bool Native(PPCContext& __restrict ctx, uint8_t* base, [[maybe_unused]] S& s) {
  if (!On(kN82954478)) [[unlikely]] return false;
  const uint32_t sp = ctx.r1.u32;
  if (!detail::Straight(sp - 32u, 32) || uint32_t(sp - 0x82000000u) < 0x01000020u) [[unlikely]] return false;
  s.Hand(kN82954478, 0);
  const double x = ctx.f1.f64;
  ctx.fpscr.disableFlushMode();  // the original's lfs f12 switch: the arithmetic below runs with the mode off
  blur_jq012::Fence();
  const blur_jq012::SinK k = blur_jq012::LoadSinK(base);
  uint64_t abs_bits, trunc_bits;
  double result = blur_jq012::SinEval(k, x, abs_bits, trunc_bits);
  blur_jq012::Pin(result);  // finished before any later flush-mode switch
#if defined(REX_NV_SAVES_ELIDE)
  constexpr uint32_t kBytes = 16;
#else
  constexpr uint32_t kBytes = 32;
#endif
  if (!s.Note(sp - 32u, kBytes)) [[unlikely]] return true;  // the guard's logged form's run is over
  ::banjo::hot::Card(base, sp - 32u, kBytes);
  uint8_t* d = ::banjo::hot::Raw(base, sp - 32u);
  blur_jq012::St64(d, trunc_bits);
  blur_jq012::St64(d + 8, abs_bits);
#if !defined(REX_NV_SAVES_ELIDE)
  blur_jq012::St64(d + 16, 0);  // stfd f30,-16(r1) / stfd f31,-8(r1): the copy's locals f30 and f31 are zero
  blur_jq012::St64(d + 24, 0);
#endif
  ctx.f1.f64 = result;
  return true;
}
}  // namespace n_82954478

// sub_82521E68 - builds a vector from a 3-vector basis and an angle: r3 = destination (16-aligned vector), r4 = an
// object (two words at +880 and +892 pick a constants record), r5 = three vectors (+0, +16, +32). It saves v124-v127
// (__savevmx_124, four stvx128 at r1 - 96 ... r1 - 48), takes the first word of a record found through the object
// (lvlx128 at that address), forms the angle vector and its sign-flipped partner, calls the sine twice (sub_82954478:
// on the word, and on 1.0 minus that word), rounds each to single, and combines with vmsum3fp128 / vmulfp128 /
// vnmsubfp / vmaddfp128 into one vector, stored at r3; then __restvmx_124 puts v124-v127 back.
// Console samples (race, job workers 0, 1, 4; 2.5 % of one core), 50 samples: +0x480-+0x4cc (the hand-over of 29
// vector registers around the __restvmx_124 call: a q-register store to the context and an x-register pair load per
// register) 14 samples, the two samples after each bl of the sine (+0x1c8, +0x2d0: the reload of f1, f13 that the
// callee has just stored) 8, the lane reload of the stvewx128 (a vector stored to the host stack and one lane loaded
// back with a variable index: +0x28c, +0x15c) 5, the rest 23 spread over short vector steps.
// What the native removes: the four calls (two save/restore, two sine) and the hand-over of 29 vector registers, the
// context round trip of f1 and f13 and of every v35-v63 temporary (all in registers here), the two stvewx128 + lfs
// pairs (the stored word is used from the register; only its final value reaches memory) and, of the four flush-mode
// switches of the original (off for the first sine, on for vsubfp128, off for the second sine, on for vmsum3fp128),
// all but one: the sines run in whatever mode the host is in. That is exact because their inputs are single-precision
// values widened to double (the word read by lfs, or the single result of vsubfp128): no double operation of the
// sine meets or makes a denormal for those inputs (|x| >= 2^-149 or zero, every term of the polynomial >= 2^-300), and
// without denormals flush on and flush off give the same results. The mode is ON when this returns, as in the original.
// Stores made, the bytes the original leaves (everything else it writes to memory is overwritten before it ends):
// the sine's red zone below the frame (r1 - 224: |x| and the reduction of the second sine, then, unless
// REX_NV_SAVES_ELIDE, two zero words), the back chain at r1 - 192 (unless REX_BACK_CHAIN_ELIDE), the two result words
// at r1 - 112 (second sine, rounded) and r1 - 108 (first sine, rounded), v124-v127 at r1 - 96 ... r1 - 48, the dead
// saves at r1 - 24, r1 - 16 (zero) and r1 - 8 (the link register's low word; unless REX_LR_MOVES_ELIDE / NV_SAVES), and
// the result vector at r3 last. One Card call marks the whole 224 bytes under r1 (they are all stack) and one the
// result. ctx.lr ends as the original leaves it (its low word, or untouched under REX_LR_MOVES_ELIDE).
// Declines (nothing written): r1 not 16-aligned (the original's lane-by-address stores and its lvlx128 of the frame
// would differ); any of the frame, the sources (the object's two words, the pointer word and the record block read
// through it, the three vectors at r5, the constant block at 0x82027BF0) wrapping 4 GB or crossing the physical-view
// boundary; or any source overlapping the 224 bytes under r1 (the original reads them after it has written the frame).
// The result vector at r3 may overlap its sources: it is written last, as in the original.
// Scratch not reproduced (hand.txt exact=r1,r3): r5-r10, f1, f13, v0, v12, v13 (and the context copies of
// v35-v63, v124-v127, r11, r12, f0 which the original restores or never changes at its end).
namespace n_82521E68 {
template <class S>
[[gnu::always_inline]] inline bool Native(PPCContext& __restrict ctx, uint8_t* base, S& s) {
  using namespace blur_jq012;
  if (!On(kN82521E68)) [[unlikely]] return false;
  const uint32_t sp0 = ctx.r1.u32, dst = ctx.r3.u32, obj = ctx.r4.u32, src = ctx.r5.u32;
  if ((sp0 & 15u) != 0) [[unlikely]] return false;
  const uint32_t frame = sp0 - 224u;  // the lowest byte written: the second sine's red zone, r1 - 192 - 32
  const uint32_t a59 = src & ~15u, a57 = (src + 16u) & ~15u, a56 = (src + 32u) & ~15u, adst = dst & ~15u;
  constexpr uint32_t kConst = 0x82027BF0u;  // lvlx128 v52,r8,r30: 0x82027BE0 + 16
  if (!detail::Straight(frame, 224) || !detail::Straight(obj + 880u, 16) || !detail::Straight(a59, 16) ||
      !detail::Straight(a57, 16) || !detail::Straight(a56, 16) || !detail::Straight(adst, 16)) [[unlikely]]
    return false;
  // The record found through the object (reads only).
  const uint32_t w892 = Ld32(::banjo::hot::Raw(base, obj + 892u));
  const uint32_t w880 = Ld32(::banjo::hot::Raw(base, obj + 880u));
  const uint32_t ptr = w892 + 7044u;
  if (!detail::Straight(ptr, 4)) [[unlikely]] return false;
  uint32_t rec = Ld32(::banjo::hot::Raw(base, ptr)) + 76u;  // lwz r11,7044(r11); addi r11,r11,76
  if (int32_t(w880) >= 2) rec += 40u;                       // cmpwi r10,2; blt ...; addi r11,r11,40
  const uint32_t arec = rec & ~15u;
  if (!detail::Straight(arec, 16)) [[unlikely]] return false;
  if (Hit(frame, 224, obj + 880u, 16) || Hit(frame, 224, ptr, 4) || Hit(frame, 224, arec, 16) ||
      Hit(frame, 224, a59, 16) || Hit(frame, 224, a57, 16) || Hit(frame, 224, a56, 16) ||
      Hit(frame, 224, kConst, 16)) [[unlikely]]
    return false;
  s.Hand(kN82521E68, 0);

  // ---- the arithmetic, in registers (nothing is written to guest memory until the end)
  const simde__m128i zero = simde_mm_setzero_si128();
  const simde__m128i sign = simde_mm_set1_epi32(int(0x80000000u));  // vslw128 of 1 by -1: 1 << 31 in every lane
  const simde__m128i v62 = LvlxV(base, rec);
  const simde__m128i v60 = simde_mm_shuffle_epi32(v62, 0xFF);  // vspltw128 v60,v62,0
  const simde__m128i v59 = LdV(base, src);
  const simde__m128i v56 = LdV(base, src + 32u);
  const simde__m128i v57 = LdV(base, src + 16u);
  const simde__m128i v54 = simde_mm_unpackhi_epi32(v56, v59);  // vmrghw128 v54,v59,v56
  const simde__m128i v126 = simde_mm_xor_si128(v60, sign);     // vxor128 v126,v60,v58
  const simde__m128i v55 = simde_mm_unpackhi_epi32(zero, v57);  // vmrghw128 v55,v57,v127
  const simde__m128i v53 = simde_mm_unpackhi_epi32(v55, v54);   // vmrghw128 v53,v54,v55
  const simde__m128i v125 = simde_mm_unpacklo_epi32(v55, v54);  // vmrglw128 v125,v54,v55
  const simde__m128i v124 = simde_mm_xor_si128(v53, sign);          // vxor128 v124,v53,v58
  // stvewx128 v126 at r1 - 112 and lfs f1: the word of lane 3 (r1 - 112 is 16-aligned); v126 has one value in all lanes.
  const uint32_t w80a = uint32_t(simde_mm_cvtsi128_si32(v126));
  const SinK k = LoadSinK(base);
  uint64_t ab1, tr1, ab2, tr2;  // the first sine's stack words are overwritten by the second's
  const double s1 = SinEval(k, ::rex::ppc::SingleToDoubleAnyMode(w80a), ab1, tr1);  // bl sub_82954478
  (void)ab1;
  (void)tr1;
  const double f0 = ::rex::ppc::RoundToSingleAnyMode(s1);                              // frsp f0,f1
  const uint32_t w84 = U32(::rex::ppc::DoubleToSingleAnyMode(f0));                    // stfs f0,84(r1)
  const simde__m128i v52 = LvlxV(base, kConst);
  const simde__m128i v51 = simde_mm_shuffle_epi32(v52, 0xFF);  // vspltw128 v51,v52,0
  ctx.fpscr.enableFlushMode();                                 // vsubfp128 / vmsum3fp128 ... run with flush on
  Fence();
  simde__m128 v51p = simde_mm_castsi128_ps(v51), v126p = simde_mm_castsi128_ps(v126);
  simde__m128 v124p = simde_mm_castsi128_ps(v124), v125p = simde_mm_castsi128_ps(v125);
  Pin(v51p);
  Pin(v126p);
  Pin(v124p);
  Pin(v125p);
  const simde__m128 v50 = simde_mm_sub_ps(v51p, v126p);  // vsubfp128 v50,v51,v126
  // stvewx128 v50 at r1 - 112, lfs f1 (SingleToDoublePure): lane 3 of v50, which has one value in all lanes.
  const uint32_t w80b = uint32_t(simde_mm_cvtsi128_si32(simde_mm_castps_si128(v50)));
  const double s2 = SinEval(k, ::rex::ppc::SingleToDoublePure(w80b), ab2, tr2);  // bl sub_82954478
  const double f13 = ::rex::ppc::RoundToSingleAnyMode(s2);                         // frsp f13,f1
  const uint32_t w80c = U32(::rex::ppc::DoubleToSingleAnyMode(f13));              // stfs f13,80(r1)
  const simde__m128 v48 = simde_mm_castsi128_ps(simde_mm_set1_epi32(int(w80c)));  // lvlx128 + vspltw128 v48,v49,0
  const simde__m128 v40 = simde_mm_castsi128_ps(simde_mm_set1_epi32(int(w84)));   // lvlx128 (r1 + 84) + vspltw128
  const simde__m128 v47 = simde_mm_castsi128_ps(simde_mm_set1_epi32(0x3F800000));  // vupkd3d128 of the zero v127
  const simde__m128 v46 = simde_mm_dp_ps(v124p, v125p, 0xEF);                       // vmsum3fp128 v46,v124,v125
  const simde__m128 v45 = simde_mm_castsi128_ps(simde_mm_shuffle_epi32(simde_mm_castps_si128(v124p), 0x78));
  const simde__m128 v44 = simde_mm_castsi128_ps(simde_mm_shuffle_epi32(simde_mm_castps_si128(v125p), 0x9C));
  const simde__m128 v13 = simde_mm_castsi128_ps(simde_mm_shuffle_epi32(simde_mm_castps_si128(v124p), 0x9C));
  const simde__m128 v42 = simde_mm_sub_ps(v47, v48);  // vsubfp128 v42,v47,v48
  const simde__m128 v0 = simde_mm_castsi128_ps(simde_mm_shuffle_epi32(simde_mm_castps_si128(v125p), 0x78));
  simde__m128 v12 = simde_mm_mul_ps(v44, v45);        // vmulfp128 v12,v44,v45
  const simde__m128 v39 = simde_mm_mul_ps(v46, v42);  // vmulfp128 v39,v46,v42
  v12 = REX_VNMSUBFP(v0, v13, v12);                   // vnmsubfp v12,v0,v13,v12
  simde__m128 v35 = simde_mm_mul_ps(v124p, v39);      // vmulfp128 v36,v124,v39
  v35 = REX_VMADDFP(v125p, v48, v35);                 // vmaddfp128 v36,v125,v48,v36
  v35 = REX_VMADDFP(v12, v40, v35);                   // vmaddfp128 v35,v12,v40,v35
  Pin(v35);
  const simde__m128i result = REX_BSWAP128(simde_mm_castps_si128(v35));

  // ---- the stores (Note first; the guard's logged form may end the run here)
#if defined(REX_NV_SAVES_ELIDE)
  constexpr uint32_t kRed = 16;
#else
  constexpr uint32_t kRed = 32;
#endif
  if (!s.Note(frame, kRed)) [[unlikely]] return true;
#if !defined(REX_BACK_CHAIN_ELIDE)
  if (!s.Note(sp0 - 192u, 4)) [[unlikely]] return true;
#endif
  if (!s.Note(sp0 - 112u, 8) || !s.Note(sp0 - 96u, 64)) [[unlikely]] return true;
#if !defined(REX_NV_SAVES_ELIDE)
  if (!s.Note(sp0 - 24u, 16)) [[unlikely]] return true;
#endif
#if !defined(REX_LR_MOVES_ELIDE)
  if (!s.Note(sp0 - 8u, 4)) [[unlikely]] return true;
#endif
  if (!s.Note(adst, 16)) [[unlikely]] return true;
  ::banjo::hot::Card(base, frame, 224);
  ::banjo::hot::Card(base, adst, 16);
  {
    uint8_t* d = ::banjo::hot::Raw(base, frame);
    St64(d, tr2);  // the second sine's stack words are the ones that stay
    St64(d + 8, ab2);
#if !defined(REX_NV_SAVES_ELIDE)
    St64(d + 16, 0);
    St64(d + 24, 0);
#endif
  }
#if !defined(REX_BACK_CHAIN_ELIDE)
  St32(::banjo::hot::Raw(base, sp0 - 192u), sp0);  // stwu r1,-192(r1): the old stack pointer
#endif
  {
    uint8_t* d = ::banjo::hot::Raw(base, sp0 - 112u);
    St32(d, w80c);
    St32(d + 4, w84);
  }
  {
    // __savevmx_124: v124-v127 as they came in (the context is not touched: __restvmx_124 puts them back).
    uint8_t* d = ::banjo::hot::Raw(base, sp0 - 96u);
    simde_mm_storeu_si128((simde__m128i*)d, REX_BSWAP128(simde_mm_load_si128((simde__m128i*)ctx.v124.u8)));
    simde_mm_storeu_si128((simde__m128i*)(d + 16), REX_BSWAP128(simde_mm_load_si128((simde__m128i*)ctx.v125.u8)));
    simde_mm_storeu_si128((simde__m128i*)(d + 32), REX_BSWAP128(simde_mm_load_si128((simde__m128i*)ctx.v126.u8)));
    simde_mm_storeu_si128((simde__m128i*)(d + 48), REX_BSWAP128(simde_mm_load_si128((simde__m128i*)ctx.v127.u8)));
  }
#if !defined(REX_NV_SAVES_ELIDE)
  {
    uint8_t* d = ::banjo::hot::Raw(base, sp0 - 24u);
    St64(d, 0);  // std r30,-24(r1); std r31,-16(r1): the copy's locals r30 and r31 are zero
    St64(d + 8, 0);
  }
#endif
#if !defined(REX_LR_MOVES_ELIDE)
  St32(::banjo::hot::Raw(base, sp0 - 8u), uint32_t(ctx.lr));  // stw r12,-8(r1)
  ctx.lr = uint32_t(ctx.lr);                                    // lwz r12,-8(r1); mtlr r12
#endif
  simde_mm_store_si128((simde__m128i*)::banjo::hot::Raw(base, adst), result);  // stvx128 v35,r0,r31 (last)
  return true;
}
}  // namespace n_82521E68
