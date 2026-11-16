// blur-nx (2026-11-16, the 15 ms batch, N4: the resampler sub_8230EDE0; the user: "I want 7 natives for the top 7 audio
// functions"): EXACT hand-written natives of the two kernels that hold the resampler's time, checked by the guard (not
// the inexact rules of blur_hand_natives_audio.h). docs/cpu-15ms-batch-1116.md says why the kernels and not the body.
//
// The nx race profile (2026-11-13 abN5_1, reports 9-20): the resampler's copy, 2.06 ms a frame on JQ3/4, 221 samples.
// About 60 are the gathers of 822D9040 / 822D79A8 (natives already, inlined), 51 the recursive filter 822E29A8, 16 the
// stereo converter 822D7DF0's vector loop. The rest (~90) is spread thin over the body's bookkeeping and calls: a
// transcription of its 737 instructions would compile to what its copy is.
//
// The DRAFT rule as the other files: included inside `namespace banjo::hand` after banjo_hand_natives.h's own helpers;
// no #include lines.

namespace blur_rs {

// ext_k(a, b) = [a_k .. a_3, b_0 .. b_(k-1)] in the guest's lane order (vsldoi by 4k bytes).
template <int K>
[[gnu::always_inline]] inline simde__m128 Ext(simde__m128 a, simde__m128 b) {
  if constexpr (K == 0)
    return a;
  else
    return simde_mm_castsi128_ps(simde_mm_alignr_epi8(simde_mm_castps_si128(b), simde_mm_castps_si128(a), 4 * K));
}
// A guest quadword (16-byte aligned) in the guest's lane order, and back (blur_swap: each word's bytes swapped).
[[gnu::always_inline]] inline simde__m128 Ld(uint8_t* base, uint32_t a) {
  return blur_swap::LoadWords(::banjo::hot::Raw(base, a));
}
[[gnu::always_inline]] inline void St(uint8_t* base, uint32_t a, simde__m128 v) {
  blur_swap::StoreWords(::banjo::hot::Raw(base, a), v);
}
// A vector register in the generated code's layout (the 16 bytes reversed) from / to the guest's lane order.
[[gnu::always_inline]] inline simde__m128 GetV(const PPCVRegister& r) {
  return blur_swap::LaneRev(simde_mm_load_ps(r.f32));
}
[[gnu::always_inline]] inline void SetV(PPCVRegister& r, simde__m128 v) {
  simde_mm_store_ps(r.f32, blur_swap::LaneRev(v));
}
[[gnu::always_inline]] inline uint32_t LdW(uint8_t* base, uint32_t a) {
  uint32_t v;
  std::memcpy(&v, ::banjo::hot::Raw(base, a), 4);
  return __builtin_bswap32(v);
}
[[gnu::always_inline]] inline uint32_t LdH(uint8_t* base, uint32_t a) {
  uint16_t v;
  std::memcpy(&v, ::banjo::hot::Raw(base, a), 2);
  return __builtin_bswap16(v);
}
// Stack stores (the generated REX_STORE_D_U32 / _U64: no store card).
[[gnu::always_inline]] inline void StW(uint8_t* base, uint32_t a, uint32_t v) {
  v = __builtin_bswap32(v);
  std::memcpy(::banjo::hot::Raw(base, a), &v, 4);
}
[[gnu::always_inline]] inline void StD(uint8_t* base, uint32_t a, uint64_t v) {
  v = __builtin_bswap64(v);
  std::memcpy(::banjo::hot::Raw(base, a), &v, 8);
}
[[gnu::always_inline]] inline uint64_t SetLo(uint64_t r, uint32_t v) { return (r & 0xFFFFFFFF00000000ull) | v; }
// [a, a + an) and [b, b + bn) share a byte (neither wraps 4 GB: the caller has checked Straight).
[[gnu::always_inline]] inline bool Hit(uint32_t a, uint32_t an, uint32_t b, uint32_t bn) {
  return uint32_t(a - b) < bn || uint32_t(b - a) < an;
}

// n frames of big-endian int16 pairs (left, right) at in -> two rows of big-endian floats v * 2^-15 (host pointers, no
// overlap): float(v) * 2^-15 is exact for every int16, as the original's scalar (fcfid, frsp, fmuls by the 2^-15 at
// 0x8202E414) and vector (vcsxwfp128 with a scale of 15) paths both give it.
[[gnu::always_inline]] inline void StereoI16F32(const uint8_t* in, uint8_t* ol, uint8_t* orr, uint32_t n) {
  // bytes of four frames -> int16 lanes [L0 L1 L2 L3 R0 R1 R2 R3] (each halfword's bytes swapped)
  const simde__m128i deint = simde_mm_set_epi8(14, 15, 10, 11, 6, 7, 2, 3, 12, 13, 8, 9, 4, 5, 0, 1);
  const simde__m128i swap32 = simde_mm_set_epi8(12, 13, 14, 15, 8, 9, 10, 11, 4, 5, 6, 7, 0, 1, 2, 3);
  const simde__m128 k = simde_mm_set1_ps(0x1p-15f);
  uint32_t i = 0;
  for (; i + 4u <= n; i += 4u) {
    const simde__m128i v =
        simde_mm_shuffle_epi8(simde_mm_loadu_si128(reinterpret_cast<const simde__m128i*>(in + 4u * i)), deint);
    const simde__m128 l = simde_mm_mul_ps(simde_mm_cvtepi32_ps(simde_mm_cvtepi16_epi32(v)), k);
    const simde__m128 r = simde_mm_mul_ps(simde_mm_cvtepi32_ps(simde_mm_cvtepi16_epi32(simde_mm_unpackhi_epi64(v, v))), k);
    simde_mm_storeu_si128(reinterpret_cast<simde__m128i*>(ol + 4u * i),
                          simde_mm_shuffle_epi8(simde_mm_castps_si128(l), swap32));
    simde_mm_storeu_si128(reinterpret_cast<simde__m128i*>(orr + 4u * i),
                          simde_mm_shuffle_epi8(simde_mm_castps_si128(r), swap32));
  }
  for (; i < n; ++i) {
    const int16_t l = int16_t(uint16_t(in[4u * i]) << 8 | in[4u * i + 1u]);
    const int16_t r = int16_t(uint16_t(in[4u * i + 2u]) << 8 | in[4u * i + 3u]);
    const uint32_t wl = __builtin_bswap32(std::bit_cast<uint32_t>(float(l) * 0x1p-15f));
    const uint32_t wr = __builtin_bswap32(std::bit_cast<uint32_t>(float(r) * 0x1p-15f));
    std::memcpy(ol + 4u * i, &wl, 4);
    std::memcpy(orr + 4u * i, &wr, 4);
  }
}

}  // namespace blur_rs

// sub_822E29A8 - the resampler's recursive filter over a stack buffer, in place, blocks of 16 floats: r3 = the buffer
// (x), r4 / r5 = two quadwords of state (the last four inputs, the last four outputs), r6 = the count n, v1 / v2 = the
// filter's coefficients (lane 0 of each: b, a). Per block (z = the four inputs before it then its 16, w the same of the
// outputs): D(q) = dot([b a^3, b a^2, b a, b], [P(q) .. P(q+3)]), P(j) = z_j + z_(j+1); y = a^4 * (the output four
// back) + D, fused (REX_VMADDFP); the results over the inputs. max(1, ceil(n / 16)) blocks, the last one whole. Then
// the state quadwords from the last block at n & 15 (n & 15 stored at r1 - 16 and read back by lvlx128).
// Console race (nx, 2026-11-13 abN5_1): ~51 of the resampler copy's 221 samples, its 16 dot products a block each two
// pairwise adds and a zip (VX-14/15's merge), 16 pair-sum vectors, the address of each lvx/stvx checked. Here: the dot
// products transposed (four outputs a vector: each coefficient's products, then (c3 P3 + c2 P2) + (c1 P1 + c0 P0) with
// the operands in the generated code's order, the same bits, NaN choice included), five pair-sum vectors and
// their shifts, one range check and one card per range.
// Registers (what the generated function writes back): r7 = 16, r8 = -16, r9 = -32, r10 = 16 x the blocks; v0-v13 as
// the generated code's selection chains leave them (below); the stack: the word at r1 - 16. The flush mode: on.
// Declined: a buffer not 16-byte aligned (lvx would round it down), r1 not word-aligned, n > 4096, ranges that wrap 4 GB
// or cross the physical-view boundary. (The ranges may overlap: the loads and stores come in the original's order.)
namespace n_822E29A8 {

template <class S>
[[gnu::always_inline]] inline bool Native(PPCContext& __restrict ctx, uint8_t* base, [[maybe_unused]] S& s) {
  if (!On(kN822E29A8)) [[unlikely]] return false;
  using namespace blur_rs;
  const uint32_t x = ctx.r3.u32, a1 = ctx.r4.u32 & ~15u, a2 = ctx.r5.u32 & ~15u, n = ctx.r6.u32, sp = ctx.r1.u32;
  const uint32_t ws = sp - 16u;
  if ((x & 15u) || (sp & 3u) || n > 4096u) [[unlikely]] return false;
  const uint32_t blocks = n > 16u ? (n + 15u) >> 4 : 1u;
  const uint32_t len = 64u * blocks;
  if (!detail::Straight(x, len) || !detail::Straight(a1, 16u) || !detail::Straight(a2, 16u) ||
      !detail::Straight(ws, 4u)) [[unlikely]]
    return false;
  s.Hand(kN822E29A8, len);
  if (!s.Note(ws, 4u) || !s.Note(x, len) || !s.Note(a1, 16u) || !s.Note(a2, 16u)) [[unlikely]] return true;
  ::banjo::hot::Card(base, x, len);
  ::banjo::hot::Card(base, a1, 16u);
  ::banjo::hot::Card(base, a2, 16u);
  ctx.fpscr.enableFlushMode();
  // The coefficients, lane by lane as the generated vmulfp128s (operands in their order), then lane 0 of each.
  const simde__m128 V2 = GetV(ctx.v2), V1 = GetV(ctx.v1);
  const simde__m128 aa = simde_mm_mul_ps(V2, V2);      // v63 = v2 * v2
  const simde__m128 ba = simde_mm_mul_ps(V1, V2);      // v62 = v1 * v2
  const simde__m128 aaa = simde_mm_mul_ps(V2, aa);     // v61 = v2 * v63
  const simde__m128 baa = simde_mm_mul_ps(V1, aa);     // v60 = v1 * v63
  const simde__m128 V3 = simde_mm_mul_ps(aa, aa);      // v3 = v63 * v63 (every lane)
  const simde__m128 baaa = simde_mm_mul_ps(V1, aaa);   // v58 = v1 * v61
  const simde__m128 C0 = simde_mm_shuffle_ps(baaa, baaa, 0), C1 = simde_mm_shuffle_ps(baa, baa, 0),
                    C2 = simde_mm_shuffle_ps(ba, ba, 0), C3 = simde_mm_shuffle_ps(V1, V1, 0);
  simde__m128 P = Ld(base, a1);  // lvx128 v59,r0,r4
  StW(base, ws, n & 15u);        // stw r11,-16(r1)
  simde__m128 Sv = Ld(base, a2);  // lvx128 v11,r0,r5
  simde__m128 Pp = P, Sp = Sv, X0 = P, X1 = P, X2 = P, X3 = P, Y0 = Sv, Y1 = Sv, Y2 = Sv, Y3 = Sv;
  const auto dot4 = [&](simde__m128 q, simde__m128 q1) {
    return simde_mm_add_ps(simde_mm_add_ps(simde_mm_mul_ps(C3, Ext<3>(q, q1)), simde_mm_mul_ps(C2, Ext<2>(q, q1))),
                           simde_mm_add_ps(simde_mm_mul_ps(C1, Ext<1>(q, q1)), simde_mm_mul_ps(C0, q)));
  };
  for (uint32_t b = 0; b < blocks; ++b) {
    const uint32_t a = x + 64u * b;
    X0 = Ld(base, a);
    X1 = Ld(base, a + 16u);
    X2 = Ld(base, a + 32u);
    X3 = Ld(base, a + 48u);
    // the pair sums P(j) = z_j + z_(j+1), z_j first as in every vaddfp128 of the original
    const simde__m128 Q0 = simde_mm_add_ps(P, Ext<1>(P, X0)), Q1 = simde_mm_add_ps(X0, Ext<1>(X0, X1)),
                      Q2 = simde_mm_add_ps(X1, Ext<1>(X1, X2)), Q3 = simde_mm_add_ps(X2, Ext<1>(X2, X3)),
                      Q4 = simde_mm_add_ps(X3, Ext<1>(X3, X3));  // lanes 0-2 used (P(16) .. P(18))
    const simde__m128 D0 = dot4(Q0, Q1), D1 = dot4(Q1, Q2), D2 = dot4(Q2, Q3), D3 = dot4(Q3, Q4);
    Y0 = REX_VMADDFP(V3, Sv, D0);  // vmaddfp v0,v3,v11,v0
    Y1 = REX_VMADDFP(V3, Y0, D1);
    Y2 = REX_VMADDFP(V3, Y1, D2);
    Y3 = REX_VMADDFP(V3, Y2, D3);
    St(base, a, Y0);
    St(base, a + 16u, Y1);
    St(base, a + 32u, Y2);
    St(base, a + 48u, Y3);
    Pp = P;
    Sp = Sv;
    P = X3;
    Sv = Y3;
  }
  // The last block's z and w, and windows of four from them: win(j) = elements j .. j+3 (j = 16: the last vector).
  alignas(16) float zb[20], wb[20];
  simde_mm_store_ps(zb, Pp);
  simde_mm_store_ps(zb + 4, X0);
  simde_mm_store_ps(zb + 8, X1);
  simde_mm_store_ps(zb + 12, X2);
  simde_mm_store_ps(zb + 16, X3);
  simde_mm_store_ps(wb, Sp);
  simde_mm_store_ps(wb + 4, Y0);
  simde_mm_store_ps(wb + 8, Y1);
  simde_mm_store_ps(wb + 12, Y2);
  simde_mm_store_ps(wb + 16, Y3);
  const uint32_t m = LdW(base, ws);  // lvlx128 v35 (r1 - 16) + vspltw128: lane 0, the word at r1 - 16 (ws & 15 <= 12)
  const auto win = [](const float* b, uint32_t j) { return simde_mm_loadu_ps(b + j); };
  // The vsel chains: chain_k = m in 1..k ? win(m) : win(16).
  const auto wc = [&](uint32_t k) { return win(wb, (m >= 1u && m <= k) ? m : 16u); };
  St(base, a1, win(zb, (m >= 1u && m <= 15u) ? m : 16u));  // stvx128 v6,r0,r4
  St(base, a2, wc(15));                                    // stvx128 v12,r0,r5
  SetV(ctx.v0, win(wb, 15));
  SetV(ctx.v1, wc(3));
  SetV(ctx.v2, win(wb, 5));
  SetV(ctx.v3, wc(5));
  SetV(ctx.v4, win(wb, 7));
  SetV(ctx.v5, wc(7));
  SetV(ctx.v6, win(wb, 9));
  SetV(ctx.v7, wc(9));
  SetV(ctx.v8, win(wb, 11));
  SetV(ctx.v9, wc(11));
  SetV(ctx.v10, win(wb, 14));
  SetV(ctx.v11, wc(12));
  SetV(ctx.v12, wc(15));
  SetV(ctx.v13, wc(14));
  ctx.r7.s64 = 16;
  ctx.r8.s64 = -16;
  ctx.r9.s64 = -32;
  ctx.r10.s64 = int64_t(16u * blocks);
  return true;
}

}  // namespace n_822E29A8

// sub_822D7DF0 - the resampler's stereo converter: r3 = n frames, r4 = interleaved big-endian int16 (left, right), r5 /
// r6 = the left / right rows of floats, each v * 2^-15. The original: a dcbt pass over the input, frames one at a time
// (each value through std / lfd at r1 - 160 / - 152 to fcfid) until r5 is 16-byte aligned, vectors of four frames
// (lvlx128 + lvrx128, vupk, vcsxwfp128 with a scale of 15, two vperm128, stvx128 to r5 and r6 rounded down), then the
// last one to three frames one at a time (std / lfd at r1 - 104 / - 112). (Its loop of four frames at a time, between
// the two, never runs: fewer than four remain after the vectors.)
// Console race (nx, 2026-11-13 abN5_1): ~16 of the resampler copy's 221 samples in the vector loop (a range check per
// stvx128, the swap table loaded every pass) and the scalar frames' round trips. Here: one pass over all n frames
// (blur_rs::StereoI16F32), one card per row.
// Registers (what the generated function writes back): r3 / r4 = the last single frame's right / left value
// sign-extended (else as they came), r7 / r8 / r9 / r10 and f6-f13 as the last stretch that ran leaves them (the
// formulas follow the generated statements: a pointer updated by lhzu keeps its upper word), v0 / v7 = the two vperm
// controls when the vectors ran; f1-f5 as they came (only the dead loop writes them). The stack: the doublewords at
// r1 - 160 / - 152 (the head's last frame) and r1 - 104 / - 112 (the tail's). The flush mode: off.
// Declined: n > 4096, r5 not word-aligned or r5 and r6 not alike modulo 16 (the vector stores would round down), the
// input, the rows and the stack slots (r1 - 160 .. r1 - 96) overlapping each other, ranges that wrap 4 GB or cross the
// physical-view boundary.
namespace n_822D7DF0 {

template <class S>
[[gnu::always_inline]] inline bool Native(PPCContext& __restrict ctx, uint8_t* base, [[maybe_unused]] S& s) {
  if (!On(kN822D7DF0)) [[unlikely]] return false;
  using namespace blur_rs;
  const uint64_t n64 = ctx.r3.u64, in64 = ctx.r4.u64, ol64 = ctx.r5.u64, or64 = ctx.r6.u64;
  const uint32_t n = uint32_t(n64), in = uint32_t(in64), ol = uint32_t(ol64), orr = uint32_t(or64), sp = ctx.r1.u32;
  if (n > 4096u || (ol & 3u) || ((ol ^ orr) & 15u)) [[unlikely]] return false;
  const uint32_t fr = sp - 160u;  // the stack slots: r1 - 160 .. r1 - 96
  if (!detail::Straight(fr, 64u)) [[unlikely]] return false;
  if (n != 0) {
    const uint32_t b = 4u * n;
    if (!detail::Straight(in, b) || !detail::Straight(ol, b) || !detail::Straight(orr, b)) [[unlikely]] return false;
    if (Hit(in, b, ol, b) || Hit(in, b, orr, b) || Hit(ol, b, orr, b) || Hit(in, b, fr, 64u) || Hit(ol, b, fr, 64u) ||
        Hit(orr, b, fr, 64u)) [[unlikely]]
      return false;
  }
  const double f0 = double(std::bit_cast<float>(uint32_t(REX_RO_U32(0x8202E414u, 0x38000000u))));
  // (1) the dcbt pass: r9 / r10
  uint64_t g9, g10;
  {
    const uint64_t lo = in & 0x7Fu;
    const uint32_t lines = uint32_t(lo + uint64_t(uint32_t(n << 2)) + 127u) >> 7;
    g9 = lines != 0 ? 128ull * lines : lo;
    g10 = uint64_t(int64_t(-2113732608));
  }
  uint64_t g3 = n64, g4 = in64, g7 = ol & 0xFu, g8 = 0;
  uint32_t done = 0;  // frames converted (r11)
  PPCRegister F6 = ctx.f6, F7 = ctx.f7, F8 = ctx.f8, F9 = ctx.f9, F10 = ctx.f10, F11 = ctx.f11, F12 = ctx.f12,
              F13 = ctx.f13;
  bool hs = false, ts = false, vec = false;
  uint64_t hl = 0, hr = 0, tl = 0, tr = 0;
  const auto half = [&](uint32_t a) -> int32_t { return int16_t(uint16_t(LdH(base, a))); };
  // (2) the head: one frame at a time while r7 (r5 & 15, + 4 a frame) < 16
  if (g7 != 0) {
    const uint32_t hmax = (16u - uint32_t(g7) + 3u) / 4u;
    const uint32_t h = n < hmax ? n : hmax;
    const uint64_t r9 = in64 - 2u;
    if (h != 0) {
      const int32_t l = half(in + 4u * (h - 1u)), r = half(in + 4u * (h - 1u) + 2u);
      F13.u64 = uint64_t(int64_t(l));
      F12.u64 = uint64_t(int64_t(r));
      F11.f64 = double(int64_t(l));
      F10.f64 = double(int64_t(r));
      F9.f64 = double(float(F11.f64));
      F8.f64 = double(float(F10.f64));
      F7.f64 = REX_FMULS(F9.f64, f0);
      F6.f64 = REX_FMULS(F8.f64, f0);
      hs = true;
      hl = uint64_t(int64_t(l));
      hr = uint64_t(int64_t(r));
    }
    g7 += 4ull * h;
    g9 = SetLo(r9, uint32_t(r9) + 4u * h);
    g10 = or64 + 4ull * h;
    g8 = 2ull * h;
    done = h;
  }
  // (3) the vectors of four frames
  {
    const uint32_t k = (n - done) >> 2;
    if (k != 0) {
      const uint64_t r8 = g8;
      g9 = uint64_t(uint32_t(done << 2)) + or64 + 16ull * k;
      g7 = uint64_t(uint32_t(uint32_t(r8) << 1)) + 16ull * k;
      g10 = g7;
      g8 = uint64_t(uint32_t(k << 3)) + r8;
      done += 4u * k;
      vec = true;
    }
  }
  // (4) the last frames one at a time
  if (done < n) {
    const uint32_t rem = n - done;
    const uint64_t b10 = uint64_t(uint32_t(uint32_t(g8) << 1)) + in64 - 2u;
    g10 = SetLo(b10, uint32_t(b10) + 4u * rem);
    g9 = ol64 - or64;
    const int32_t l = half(in + 4u * (n - 1u)), r = half(in + 4u * (n - 1u) + 2u);
    g7 = uint16_t(l);
    g8 = uint16_t(r);
    g4 = uint64_t(int64_t(l));
    g3 = uint64_t(int64_t(r));
    F13.u64 = uint64_t(int64_t(l));
    F12.f64 = double(int64_t(l));
    F11.u64 = uint64_t(int64_t(r));
    F10.f64 = double(float(F12.f64));
    F9.f64 = double(int64_t(r));
    F8.f64 = REX_FMULS(F10.f64, f0);
    F7.f64 = double(float(F9.f64));
    F6.f64 = REX_FMULS(F7.f64, f0);
    ts = true;
    tl = uint64_t(int64_t(l));
    tr = uint64_t(int64_t(r));
    done = n;
  }
  if (done != n) [[unlikely]] return false;  // the stretches did not add up: not understood, nothing written yet
  s.Hand(kN822D7DF0, 8u * n);
  if (hs && (!s.Note(sp - 160u, 8u) || !s.Note(sp - 152u, 8u))) [[unlikely]] return true;
  if (ts && (!s.Note(sp - 104u, 8u) || !s.Note(sp - 112u, 8u))) [[unlikely]] return true;
  if (n != 0 && (!s.Note(ol, 4u * n) || !s.Note(orr, 4u * n))) [[unlikely]] return true;
  if (n != 0) {
    ::banjo::hot::Card(base, ol, 4u * n);
    ::banjo::hot::Card(base, orr, 4u * n);
    StereoI16F32(::banjo::hot::Raw(base, in), ::banjo::hot::Raw(base, ol), ::banjo::hot::Raw(base, orr), n);
  }
  if (hs) {
    StD(base, sp - 160u, hl);
    StD(base, sp - 152u, hr);
  }
  if (ts) {
    StD(base, sp - 104u, tl);
    StD(base, sp - 112u, tr);
  }
  ctx.fpscr.disableFlushMode();
  ctx.r3.u64 = g3;
  ctx.r4.u64 = g4;
  ctx.r7.u64 = g7;
  ctx.r8.u64 = g8;
  ctx.r9.u64 = g9;
  ctx.r10.u64 = g10;
  ctx.f6 = F6;
  ctx.f7 = F7;
  ctx.f8 = F8;
  ctx.f9 = F9;
  ctx.f10 = F10;
  ctx.f11 = F11;
  ctx.f12 = F12;
  ctx.f13 = F13;
  if (vec) {
    simde_mm_store_si128((simde__m128i*)ctx.v7.u8, REX_RO_V128(0x8202E200, 0x040506070C0D0E0Full, 0x141516171C1D1E1Full));
    simde_mm_store_si128((simde__m128i*)ctx.v0.u8, REX_RO_V128(0x8202E1F0, 0x0001020308090A0Bull, 0x1011121318191A1Bull));
  }
  return true;
}

}  // namespace n_822D7DF0
