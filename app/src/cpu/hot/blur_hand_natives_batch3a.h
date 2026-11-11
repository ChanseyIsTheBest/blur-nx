// blur-nx (2026-11-11): hand-written natives, batch 3a (callers of the sine and leaves; hand_batch3a.draft.txt and
// docs/hand-natives-batch3a.md say which and why). DRAFT: included inside `namespace banjo::hand`; no #include lines.
// Helpers are in blur_b3a (nothing is taken from the other natives' namespaces).
//
// Keys the main session has to add (enum values and keys), one per native below:
//   kN82500AC0
#pragma once

namespace blur_b3a {

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
// value to its place (as Katamari's natives do).
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
// vspltw128 v,v,i for the host lane order: element 0 is host lane 3 (imm 0xFF), 1 is 0xAA, 2 is 0x55.
[[gnu::always_inline]] inline V Sp0(V v) { return simde_mm_shuffle_epi32(v, 0xFF); }
[[gnu::always_inline]] inline V Sp1(V v) { return simde_mm_shuffle_epi32(v, 0xAA); }
[[gnu::always_inline]] inline V Sp2(V v) { return simde_mm_shuffle_epi32(v, 0x55); }

// ---------------------------------------------------------------------------------------------------------------------
// The game's sine (sub_82954478), operation for operation as the generated code has it (the same code as the jq012
// natives): double precision, fused multiply-adds only where the original has fmadd / fnmsub. abs_bits / trunc_bits are
// the two doublewords the original leaves on the stack at its r1 - 24 and r1 - 32. Exact in either flush mode as long
// as no operation sees or makes a denormal double: the callers guarantee |x| is zero, a widened single or inf/NaN.
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
[[gnu::always_inline]] inline double SinEval(const SinK& k, double x, uint64_t& abs_bits, uint64_t& trunc_bits) {
  abs_bits = U64(x) & 0x7FFFFFFFFFFFFFFFull;
  const double f0 = D64(abs_bits);
  const double scaled = k.c8 * f0;
  const int64_t rounded = ::rex::ppc::fctid(scaled);
  const double f12 = ::rex::ppc::GeZeroAnyMode(x) ? k.fhi : k.flo;
  const double n = double(rounded);
  const double r1 = -std::fma(k.c40, n, -f0);
  const int64_t z = ::rex::ppc::fctidz(n);
  trunc_bits = uint64_t(z);
  const bool odd = (z & 1) != 0;
  const double f9 = -std::fma(k.c48, n, -r1);
  const double sq = f9 * f9;
  double p = std::fma(k.c112, sq, k.c104);
  p = std::fma(p, sq, k.c96);
  p = std::fma(p, sq, k.c88);
  p = std::fma(p, sq, k.c80);
  p = std::fma(p, sq, k.c72);
  p = std::fma(p, sq, k.c64);
  p = std::fma(p, sq, k.c56);
  p = std::fma(p, sq, k.one);
  double v = p * f9;
  if (odd) v = -v;
  v = v * f12;
  if (abs_bits == 0) return x;
  const double diff = f0 - k.c16;
  return ::rex::ppc::GeZeroAnyMode(diff) ? k.big : v;
}

}  // namespace blur_b3a

// ---------------------------------------------------------------------------------------------------------------------
// sub_82500AC0 - a rotation matrix from an axis and an angle (Rodrigues): r3 = the three row vectors (aligned, 48
// bytes, read for the lane that is kept and written), r4 = the axis (x, y, z, w), v1 = the angle in element 0. It
// saves v126 and v127 (stvx128 at r1 - 64 and r1 - 48), makes a frame of 160, takes the sine of the angle and of
// (constant - angle) (sub_82954478 twice; the constant is the vector at 0x82027BF0), rounds each to single, forms
// c = cos, s = sin, t = 1 - c and the nine entries (x*x*t + c, x*y*t +- s*z, ...) as vmulfp128 / vmaddfp128 chains
// and lane insertions (vrlimi128) into the three rows in three rounds (one lane group per round), storing the three
// rows after each round, then restores v126 and v127.
// Console samples: not among the profile's 80 hottest by name, but it calls the sine twice per call and the sine
// (sub_82954478, 2.3 % of a core) is reached mostly through callers like this one and sub_82521E68.
// What the native removes against the machine copy: two calls of the sine with their hand-over of f1, f13 and the
// context (the sine is computed inline from the shared constants, once loaded), the stvewx128 + lfs round trips
// (the word is taken from the register), the intermediate stores of the three rows (only the third round's stores
// remain: nothing reads the rows in between, which the overlap checks guarantee), the intermediate stack words, the
// context round trips of every vector temporary, and three of the four flush-mode switches (off for sine one, on for
// vsubfp128, off for sine two, on for the vector products) as in sub_82521E68's native: the sine's inputs are
// single-precision values widened to double, so no denormal double arises and flush on and off agree.
// Stores made, the bytes the original leaves (all Note'd first, one Card each for the frame and the rows): the
// second sine's red zone at r1 - 192 (|x| bits, the reduction, and unless REX_NV_SAVES_ELIDE two zero words), the back
// chain at r1 - 160 (unless REX_BACK_CHAIN_ELIDE), the words at r1 - 80 (the subtraction's lane 3) and r1 - 76
// (the second sine rounded to single), v126 and v127 at r1 - 64 and r1 - 48 (the entry context values; they are not
// written back to the context: the restore at the end gives the same bytes), the dead saves at r1 - 24, r1 - 16 (zero)
// and r1 - 8 (the low word of LR; unless REX_LR_MOVES_ELIDE), and the three rows at r3 (the lanes the three rounds
// replace; lane 0 keeps its memory value). ctx.lr ends as the original leaves it.
// Declines (nothing written): r1 not 16-aligned; the frame (224 bytes under r1), the three rows, the axis block or the
// constant block wrapping 4 GB or crossing the physical-view boundary; the rows overlapping the axis block, the frame
// or the constant block; the axis block or the constant block overlapping the frame (the original reads them after
// writing the frame, the rows after the saves).
// Scratch not reproduced (hand.txt exact=r1): r5-r12, f0, f1, f13, v0, v7-v13 and the temporaries; v126 and v127
// are restored by the original, so they end as they began.
// ---------------------------------------------------------------------------------------------------------------------
namespace n_82500AC0 {
template <class S>
[[gnu::always_inline]] inline bool Native(PPCContext& __restrict ctx, uint8_t* base, S& s) {
  using namespace blur_b3a;
  if (!On(kN82500AC0)) [[unlikely]] return false;
  const uint32_t sp0 = ctx.r1.u32, rows = ctx.r3.u32, axis = ctx.r4.u32;
  if ((sp0 & 15u) != 0) [[unlikely]] return false;
  const uint32_t frame = sp0 - 192u;  // the lowest byte written: the second sine's red zone
  const uint32_t rlo = rows & ~15u;
  constexpr uint32_t kConst = 0x82027BF0u;  // lvlx128 v63,r9,r8: 0x82027BE0 + 16
  if (!detail::Straight(frame, 192) || !detail::Straight(rlo, 48 + 16) || !Blk(axis) || !Blk(kConst)) [[unlikely]]
    return false;
  const uint32_t alo = axis & ~15u;
  if (Hit(rlo, 64, alo, 16) || Hit(rlo, 64, frame, 192) || Hit(rlo, 64, kConst, 16) || Hit(alo, 16, frame, 192) ||
      Hit(kConst, 16, frame, 192)) [[unlikely]]
    return false;
  s.Hand(kN82500AC0, 0);

  const SinK k = LoadSinK(base);
  uint64_t ab1, tr1, ab2, tr2;
  (void)ab1;
  (void)tr1;
  const V v1 = simde_mm_load_si128((const simde__m128i*)ctx.v1.u8);
  const uint32_t w80a = uint32_t(simde_mm_extract_epi32(v1, 3));  // stvewx128 v126 (= v1) at r1 - 80: lane 3
  const double s1 = SinEval(k, ::rex::ppc::SingleToDoubleAnyMode(w80a), ab1, tr1);  // bl sub_82954478
  const double f0 = ::rex::ppc::RoundToSingleAnyMode(s1);                              // frsp f0,f1
  const uint32_t w84a = U32(::rex::ppc::DoubleToSingleAnyMode(f0));                   // stfs f0,84(r1)
  const V v63 = LvlxV(base, kConst);
  const V v61 = Sp0(v63);
  const V v127n = Sp0(simde_mm_set1_epi32(int(w84a)));  // lvlx128 of the word at r1 - 76 + vspltw128: sin(angle)
  ctx.fpscr.enableFlushMode();
  Fence();
  F v61p = ToF(v61), v126p = ToF(v1);
  Pin(v61p);
  Pin(v126p);
  const F v60 = simde_mm_sub_ps(v61p, v126p);  // vsubfp128 v60,v61,v126
  const uint32_t w80b = uint32_t(simde_mm_extract_epi32(ToI(v60), 3));  // stvewx128 v60 at r1 - 80: lane 3
  const double s2 = SinEval(k, ::rex::ppc::SingleToDoublePure(w80b), ab2, tr2);        // bl sub_82954478
  const double f13 = ::rex::ppc::RoundToSingleAnyMode(s2);                             // frsp f13,f1
  const uint32_t w84b = U32(::rex::ppc::DoubleToSingleAnyMode(f13));                   // stfs f13,84(r1)
  const F cosv = ToF(simde_mm_set1_epi32(int(w84b)));                                  // v13 = splat of the word at r1 - 76
  const F sinv = ToF(v127n);                                                           // v127
  const F zero = ToF(simde_mm_setzero_si128());                                        // v59
  const F one = ToF(simde_mm_set1_epi32(0x3F800000));                                  // vupkd3d128 v54,v59,0
  const V sign = simde_mm_set1_epi32(int(0x80000000u));                                // vslw128 of 1 by -1
  const F nsin = ToF(simde_mm_xor_si128(ToI(sinv), sign));                             // v12
  const F v0 = simde_mm_sub_ps(one, cosv);                                             // vsubfp128 v0,v54,v13
  V r52 = LdV(base, rows), r48 = LdV(base, rows + 16u), r47 = LdV(base, rows + 32u);   // lvx128 v52, v48, v47
  const V ax = LdV(base, axis);                                                        // lvx128 v58 / v45 / v40
  const F xs = ToF(Sp0(ax)), ys = ToF(Sp1(ax)), zs = ToF(Sp2(ax));                     // vspltw128 element 0, 1, 2
  // round 1: the column of x
  {
    const F v9 = simde_mm_mul_ps(xs, xs);                  // vmulfp128 v9,v56,v56
    const F v51 = simde_mm_mul_ps(ys, xs);                 // vmulfp128 v51,v11,v56
    const F v49 = simde_mm_mul_ps(zs, xs);                 // vmulfp128 v49,v10,v56
    F v8 = simde_mm_mul_ps(v51, v0);                       // vmulfp128 v8,v51,v0
    F v7 = simde_mm_mul_ps(v49, v0);                       // vmulfp128 v7,v49,v0
    F v55 = REX_VMADDFP(v9, v0, cosv);                     // vmaddfp128 v55,v9,v0,v55
    const F v10 = REX_VMADDFP(sinv, zs, v8);               // vmaddcfp128 v10,v127,v10,v8
    v7 = REX_VMADDFP(nsin, ys, v7);                        // vmaddfp128 v7,v12,v11,v7
    v55 = simde_mm_blend_ps(v55, simde_mm_permute_ps(v10, 228), 4);   // vrlimi128 v55,v10,4,0
    v7 = simde_mm_blend_ps(v7, simde_mm_permute_ps(zero, 228), 1);    // vrlimi128 v7,v59,1,0
    v55 = simde_mm_blend_ps(v55, simde_mm_permute_ps(v7, 228), 3);    // vrlimi128 v55,v7,3,0
    r52 = ToI(simde_mm_blend_ps(ToF(r52), simde_mm_permute_ps(v55, 228), 8));  // vrlimi128 v52,v55,8,0
    r48 = ToI(simde_mm_blend_ps(ToF(r48), simde_mm_permute_ps(v55, 147), 8));  // vrlimi128 v48,v55,8,1
    r47 = ToI(simde_mm_blend_ps(ToF(r47), simde_mm_permute_ps(v55, 78), 8));   // vrlimi128 v47,v55,8,2
    (void)v8;
  }
  // round 2: the column of y
  {
    F v9 = simde_mm_mul_ps(ys, ys);                        // vmulfp128 v9,v44,v44
    const F v43 = simde_mm_mul_ps(xs, ys);                 // vmulfp128 v43,v11,v44
    const F v42 = simde_mm_mul_ps(zs, ys);                 // vmulfp128 v42,v10,v44
    v9 = REX_VMADDFP(v9, v0, cosv);                        // vmaddfp v9,v9,v0,v13
    F v8 = simde_mm_mul_ps(v43, v0);                       // vmulfp128 v8,v43,v0
    F v7 = simde_mm_mul_ps(v42, v0);                       // vmulfp128 v7,v42,v0
    v8 = REX_VMADDFP(nsin, zs, v8);                        // vmaddfp128 v8,v12,v10,v8
    v7 = REX_VMADDFP(sinv, xs, v7);                        // vmaddfp128 v7,v127,v11,v7
    v8 = simde_mm_blend_ps(v8, simde_mm_permute_ps(v9, 228), 4);      // vrlimi128 v8,v9,4,0
    v7 = simde_mm_blend_ps(v7, simde_mm_permute_ps(zero, 228), 1);    // vrlimi128 v7,v59,1,0
    v8 = simde_mm_blend_ps(v8, simde_mm_permute_ps(v7, 228), 3);      // vrlimi128 v8,v7,3,0
    r52 = ToI(simde_mm_blend_ps(ToF(r52), simde_mm_permute_ps(v8, 57), 4));    // vrlimi128 v52,v8,4,3
    r48 = ToI(simde_mm_blend_ps(ToF(r48), simde_mm_permute_ps(v8, 228), 4));   // vrlimi128 v48,v8,4,0
    r47 = ToI(simde_mm_blend_ps(ToF(r47), simde_mm_permute_ps(v8, 147), 4));   // vrlimi128 v47,v8,4,1
  }
  // round 3: the column of z
  {
    F v9 = simde_mm_mul_ps(zs, zs);                        // vmulfp128 v9,v39,v39
    const F v38 = simde_mm_mul_ps(xs, zs);                 // vmulfp128 v38,v10,v39
    const F v37 = simde_mm_mul_ps(ys, zs);                 // vmulfp128 v37,v11,v39
    F v41 = REX_VMADDFP(v9, v0, cosv);                     // vmaddfp128 v41,v9,v0,v41 (v41 = cos)
    v9 = simde_mm_mul_ps(v38, v0);                         // vmulfp128 v9,v38,v0
    const F v13 = simde_mm_mul_ps(v37, v0);                // vmulfp128 v13,v37,v0
    v41 = simde_mm_blend_ps(v41, simde_mm_permute_ps(zero, 228), 1);  // vrlimi128 v41,v59,1,0
    v9 = REX_VMADDFP(sinv, ys, v9);                        // vmaddfp128 v9,v127,v11,v9
    const F v0n = REX_VMADDFP(nsin, xs, v13);              // vmaddfp v0,v12,v10,v13
    v9 = simde_mm_blend_ps(v9, simde_mm_permute_ps(v0n, 228), 4);     // vrlimi128 v9,v0,4,0
    v9 = simde_mm_blend_ps(v9, simde_mm_permute_ps(v41, 228), 3);     // vrlimi128 v9,v41,3,0
    r52 = ToI(simde_mm_blend_ps(ToF(r52), simde_mm_permute_ps(v9, 78), 2));    // vrlimi128 v52,v9,2,2
    r48 = ToI(simde_mm_blend_ps(ToF(r48), simde_mm_permute_ps(v9, 57), 2));    // vrlimi128 v48,v9,2,3
    r47 = ToI(simde_mm_blend_ps(ToF(r47), simde_mm_permute_ps(v9, 228), 2));   // vrlimi128 v47,v9,2,0
  }
  Fence();

  // ---- the stores (Note first; the guard's logged form may end the run here)
#if defined(REX_NV_SAVES_ELIDE)
  constexpr uint32_t kRed = 16;
#else
  constexpr uint32_t kRed = 32;
#endif
  if (!s.Note(frame, kRed)) [[unlikely]] return true;
#if !defined(REX_BACK_CHAIN_ELIDE)
  if (!s.Note(sp0 - 160u, 4)) [[unlikely]] return true;
#endif
  if (!s.Note(sp0 - 80u, 8) || !s.Note(sp0 - 64u, 32)) [[unlikely]] return true;
#if !defined(REX_NV_SAVES_ELIDE)
  if (!s.Note(sp0 - 24u, 16)) [[unlikely]] return true;
#endif
#if !defined(REX_LR_MOVES_ELIDE)
  if (!s.Note(sp0 - 8u, 4)) [[unlikely]] return true;
#endif
  if (!s.Note(rlo, 48)) [[unlikely]] return true;
  ::banjo::hot::Card(base, frame, 192);
  ::banjo::hot::Card(base, rlo, 48);
  {
    uint8_t* d = ::banjo::hot::Raw(base, frame);
    St64(d, tr2);
    St64(d + 8, ab2);
#if !defined(REX_NV_SAVES_ELIDE)
    St64(d + 16, 0);
    St64(d + 24, 0);
#endif
  }
#if !defined(REX_BACK_CHAIN_ELIDE)
  St32(::banjo::hot::Raw(base, sp0 - 160u), sp0);  // stwu r1,-160(r1)
#endif
  {
    uint8_t* d = ::banjo::hot::Raw(base, sp0 - 80u);
    St32(d, w80b);
    St32(d + 4, w84b);
  }
  StV(base, sp0 - 64u, simde_mm_load_si128((const simde__m128i*)ctx.v126.u8));  // stvx128 v126,r1,r12
  StV(base, sp0 - 48u, simde_mm_load_si128((const simde__m128i*)ctx.v127.u8));  // stvx128 v127,r1,r12
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
  StV(base, rows, r52);
  StV(base, rows + 16u, r48);
  StV(base, rows + 32u, r47);
  return true;
}
}  // namespace n_82500AC0
