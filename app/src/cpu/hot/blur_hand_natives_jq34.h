// blur-nx (2026-11-11): hand-written natives of Blur's hottest game functions on job workers 3 and 4 (and an XThread),
// from the console race profile of the rebased build (C:\blur-nx-test\console_runs\2026-11-11_1518_rebased: handheld,
// 13 fps, all three cores full). Included by banjo_hand_natives.h inside namespace banjo::hand; each in front of the
// machine copy of its function (hot/hand.txt), with its key in blur_hand_keys.cpp. A native returns false to decline a
// call (nothing written yet) and the copy runs; the guard of every hook that contains it compares it with the original.
//
// Guest function addresses may be named in this folder only (tools/localize_native.py reads every other source's as
// hooked functions).

#pragma once

// sub_822E4698 - zero r4 kilobytes from r3: for i < r4, eight dcbzl (128-byte cache-line zero) at r3 + 1024 i +
// 128 k, k < 8, each address rounded down to 128. Adding multiples of 128 leaves the low seven bits alone, so that is
// exactly the range [r3 & ~127, + 1024 r4). The generated code makes each dcbzl an inline 128-byte memset (stp q
// pairs: every line read into the cache before it is overwritten). Console race (2026-11-11 15:18, rebased build):
// job workers 3 and 4, 4.4 % of a core, 91 % of its samples on those stores. Here: one memset of the whole range,
// which devkitA64's newlib does with dc zva (a line zeroed without reading it) when dczid_el0 allows; on the PC, the
// C library's memset. Cards marked for the range once.
// Registers: r10 = r4 (the loop counter's end; 0 when r4 is 0) and r5-r9 = 640, 512, 384, 896, 768 as the last pass
// leaves them (r11 is a local of the generated function; cr6 too). r4 == 0 writes r10 only.
// Declined: a range that wraps 4 GB or crosses the physical-view boundary (detail::Straight).
namespace n_822E4698 {

template <class S>
[[gnu::always_inline]] inline bool Native(PPCContext& __restrict ctx, uint8_t* base, [[maybe_unused]] S& s) {
  if (!On(kN822E4698)) [[unlikely]] return false;
  const uint32_t count = ctx.r4.u32;
  if (count == 0) {
    ctx.r10.s64 = 0;
    return true;
  }
  if (count >= (1u << 22)) [[unlikely]] return false;  // 4 GB or more: it wraps
  const uint32_t start = ctx.r3.u32 & ~127u, len = count << 10;
  if (!detail::Straight(start, len)) [[unlikely]] return false;
  s.Hand(kN822E4698, len);
  if (!s.Note(start, len)) [[unlikely]] return true;
  ::banjo::hot::Card(base, start, len);
  std::memset(::banjo::hot::Raw(base, start), 0, len);
  ctx.r5.s64 = 640;
  ctx.r6.s64 = 512;
  ctx.r7.s64 = 384;
  ctx.r8.s64 = 896;
  ctx.r9.s64 = 768;
  ctx.r10.s64 = count;
  return true;
}

}  // namespace n_822E4698

// sub_822EA3C0 - a 1 KB vector block copy (hand-written PowerPC: dcbt of the source's lines, dcbz of eight 32-byte
// pieces of the destination at (r3 + 128 k) & ~31, then four 256-byte blocks of sixteen lvx128 / stvx128, each block's
// loads before its stores). lvx / stvx round every address down to 16, and adding multiples of 16 keeps the low four
// bits, so the loads read [r4 & ~15, +1024) and the stores write [r3 & ~15, +1024). Of the zeroed pieces only one
// reaches outside the stores: with bit 4 of r3 set, [(r3 & ~15) - 16, r3 & ~15) stays zero. Console race (2026-11-11
// 15:18, rebased build): job workers 3 and 4, 5.5 % of a core; 44 % of its samples on the loads, 28 % on the stores, 17
// % on store-card marks (one after every 16-byte store) and the rest on address arithmetic, with every vector byte-
// swapped on its load and swapped back on its store. Here: one 1 KB copy in guest byte order, the 16 bytes zeroed when
// they are, the cards marked once.
// Registers (r11, r24-r31, ctr and v48-v63 are locals of the generated function): r3-r9 = 96, 112, 144, 160, 176,
// 192, 208 as the last block leaves them; r10 = r3 + 1056 (64-bit, from r3 as it came).
// Declined: a source that overlaps what is written ([(r3 & ~15) - 16, (r3 & ~15) + 1024): the original then reads
// what an earlier block or the dcbz wrote), ranges that wrap 4 GB or cross the physical-view boundary.
namespace n_822EA3C0 {

template <class S>
[[gnu::always_inline]] inline bool Native(PPCContext& __restrict ctx, uint8_t* base, [[maybe_unused]] S& s) {
  if (!On(kN822EA3C0)) [[unlikely]] return false;
  const uint32_t dst = ctx.r3.u32 & ~15u, src = ctx.r4.u32 & ~15u;
  const uint32_t lead = ctx.r3.u32 & 16u;  // 16: the zeroed piece reaching 16 bytes below dst
  const uint32_t w0 = dst - lead, wlen = 1024u + lead;
  if (uint32_t(src - w0) < wlen || uint32_t(w0 - src) < 1024u) [[unlikely]] return false;
  if (!detail::Straight(w0, wlen) || !detail::Straight(src, 1024u)) [[unlikely]] return false;
  s.Hand(kN822EA3C0, 1024);
  if (!s.Note(w0, wlen)) [[unlikely]] return true;
  ::banjo::hot::Card(base, w0, wlen);
  std::memcpy(::banjo::hot::Raw(base, dst), ::banjo::hot::Raw(base, src), 1024);
  if (lead) std::memset(::banjo::hot::Raw(base, w0), 0, 16);
  const int64_t r3 = ctx.r3.s64;
  ctx.r3.s64 = 96;
  ctx.r4.s64 = 112;
  ctx.r5.s64 = 144;
  ctx.r6.s64 = 160;
  ctx.r7.s64 = 176;
  ctx.r8.s64 = 192;
  ctx.r9.s64 = 208;
  ctx.r10.s64 = r3 + 1056;
  return true;
}

}  // namespace n_822EA3C0

// sub_822EA500 - dst = s * b + a over 1 KB (64 vectors): r3 = dst, r4 = a, r5 = b, r6 = the address of the scalar s
// (hand-written PowerPC: dcbt of both sources' lines; when dst is neither source, dcbz of eight 32-byte pieces of dst
// at (r3 + 128 k) & ~31; lvlx128 + vspltw of the word at r6; then four 256-byte blocks of 16 lvx128 of a, 16 of b, 16
// vmaddfp and 16 stvx128, each block's loads before its stores). As in sub_822EA3C0 the vectors are [r4 & ~15, +1024),
// [r5 & ~15, +1024) and [r3 & ~15, +1024), and of the zeroed pieces only [(r3 & ~15) - 16, r3 & ~15) outlives the
// stores (bit 4 of r3 set). Console race (2026-11-11 15:18, rebased build): job workers 3 and 4, 7.6 % of a core,
// the hottest game function; 53 % of its samples on the loads, 26 % on the stores, 5 % on store-card marks (one after
// every store), the rest on address arithmetic. Here: the same byte swaps and the same REX_VMADDFP (the fused form on
// the Switch, port 72; the exact one on the PC) with the flush mode on as the original sets it, straight through the
// three ranges, the zeroing only where it outlives the stores, the cards marked once.
// Registers, as the last block leaves them (the generated function writes r3-r10 and v0-v13 back): r3-r8 = 112, 144,
// 160, 176, 192, 208; r9 = r5 + 1056, r10 = r3 + 1056 (64-bit, as they came); v0 = s in all four lanes; v1-v4 = a[12],
// a[11], a[10], a[9]; v8 = b[11]; v5, v6, v7, v9, v10, v11, v12, v13 = results 8, 9, 10, 11, 12, 13, 14, 15 (of the
// last block). The flush mode is left on, as the original leaves it.
// Declined: s straddling its 16-byte line (lvlx128 then fills the word with zeros), a source overlapping what is
// written unless it is the destination itself (r4 or r5 == r3: no dcbz, element by element in place, as the original),
// ranges that wrap 4 GB or cross the physical-view boundary.
namespace n_822EA500 {

template <class S>
[[gnu::always_inline]] inline bool Native(PPCContext& __restrict ctx, uint8_t* base, [[maybe_unused]] S& s) {
  if (!On(kN822EA500)) [[unlikely]] return false;
  const uint32_t sp = ctx.r6.u32;
  if ((sp & 15u) > 12u) [[unlikely]] return false;
  const uint64_t r3 = ctx.r3.u64, r4 = ctx.r4.u64, r5 = ctx.r5.u64;
  const bool zero = (r4 - r3) != 0 && (r5 - r3) != 0;  // the original's subf / addic / subfe / and on 64 bits
  const uint32_t d = uint32_t(r3) & ~15u, pa = uint32_t(r4) & ~15u, pb = uint32_t(r5) & ~15u;
  const uint32_t lead = zero ? (uint32_t(r3) & 16u) : 0u;
  const uint32_t w0 = d - lead, wlen = 1024u + lead;
  const auto apart = [w0, wlen](uint32_t p) { return !(uint32_t(p - w0) < wlen || uint32_t(w0 - p) < 1024u); };
  if ((r4 != r3 && !apart(pa)) || (r5 != r3 && !apart(pb))) [[unlikely]] return false;
  if (!detail::Straight(w0, wlen) || !detail::Straight(pa, 1024u) || !detail::Straight(pb, 1024u)) [[unlikely]]
    return false;
  const uint8_t* q = ::banjo::hot::Raw(base, sp);
  const uint32_t sbits = uint32_t(q[0]) << 24 | uint32_t(q[1]) << 16 | uint32_t(q[2]) << 8 | uint32_t(q[3]);
  s.Hand(kN822EA500, 1024);
  if (!s.Note(w0, wlen)) [[unlikely]] return true;
  ::banjo::hot::Card(base, w0, wlen);
  if (lead) std::memset(::banjo::hot::Raw(base, w0), 0, 16);
  ctx.fpscr.enableFlushMode();
  const simde__m128 sv = simde_mm_castsi128_ps(simde_mm_set1_epi32(int32_t(sbits)));
  const uint8_t* ap = ::banjo::hot::Raw(base, pa);
  const uint8_t* bp = ::banjo::hot::Raw(base, pb);
  uint8_t* dp = ::banjo::hot::Raw(base, d);
  simde__m128 av[16], bv[16], rv[16];
  for (uint32_t blk = 0; blk < 4; ++blk) {
    const uint32_t o = blk * 256u;
    for (uint32_t j = 0; j < 16; ++j) {
      av[j] = simde_mm_castsi128_ps(REX_BSWAP128(simde_mm_load_si128((const simde__m128i*)(ap + o + 16 * j))));
      bv[j] = simde_mm_castsi128_ps(REX_BSWAP128(simde_mm_load_si128((const simde__m128i*)(bp + o + 16 * j))));
    }
    for (uint32_t j = 0; j < 16; ++j) rv[j] = REX_VMADDFP(sv, bv[j], av[j]);
    for (uint32_t j = 0; j < 16; ++j)
      simde_mm_store_si128((simde__m128i*)(dp + o + 16 * j), REX_BSWAP128(simde_mm_castps_si128(rv[j])));
  }
  simde_mm_store_ps(ctx.v0.f32, sv);
  simde_mm_store_ps(ctx.v1.f32, av[12]);
  simde_mm_store_ps(ctx.v2.f32, av[11]);
  simde_mm_store_ps(ctx.v3.f32, av[10]);
  simde_mm_store_ps(ctx.v4.f32, av[9]);
  simde_mm_store_ps(ctx.v5.f32, rv[8]);
  simde_mm_store_ps(ctx.v6.f32, rv[9]);
  simde_mm_store_ps(ctx.v7.f32, rv[10]);
  simde_mm_store_ps(ctx.v8.f32, bv[11]);
  simde_mm_store_ps(ctx.v9.f32, rv[11]);
  simde_mm_store_ps(ctx.v10.f32, rv[12]);
  simde_mm_store_ps(ctx.v11.f32, rv[13]);
  simde_mm_store_ps(ctx.v12.f32, rv[14]);
  simde_mm_store_ps(ctx.v13.f32, rv[15]);
  ctx.r3.s64 = 112;
  ctx.r4.s64 = 144;
  ctx.r5.s64 = 160;
  ctx.r6.s64 = 176;
  ctx.r7.s64 = 192;
  ctx.r8.s64 = 208;
  ctx.r9.s64 = int64_t(r5) + 1056;
  ctx.r10.s64 = int64_t(r3) + 1056;
  return true;
}

}  // namespace n_822EA500

// sub_822EE628 - a job of 1 KB vector adds: r3 = the job, [r3 + 16] = a table of 16-byte entries (dst, a, b), [r3 +
// 20] = their number, read again after every entry. Each entry as sub_822EA500 with vaddfp: dcbt of a and b; dcbz of
// eight 32-byte pieces of dst at (dst + 128 k) & ~31 unless dst is a or b; then four 256-byte blocks of 16 lvx128 of a,
// 16 of b, 16 vaddfp (a + b, in that order) and 16 stvx128, each block's loads before its stores. Console race
// (2026-11-11 15:18, rebased build): an XThread, 4.8 % of a core; 58 % of its samples on the loads, 21 % on address
// arithmetic, 15 % on the stores, 6 % on store-card marks. Here: the same steps in the same order (so exact whatever
// overlaps: the entries are read when the original reads them, the zeroing comes first, each block loads before it
// stores), each 16-byte vector through its own host pointer, the same byte swaps and simde_mm_add_ps with the flush mode
// on as the original sets it, the cards marked once an entry.
// Registers (the generated function writes r4-r10 back; the rest are its locals): with entries, r4-r8 = 160, 176,
// 192, 208, 128 and r9 = b + 1056, r10 = dst + 1056 of the last entry (its words zero-extended, as lwz leaves them);
// with none, nothing. The flush mode is left on after an entry, as the original leaves it.
namespace n_822EE628 {

template <class S>
[[gnu::always_inline]] inline bool Native(PPCContext& __restrict ctx, uint8_t* base, [[maybe_unused]] S& s) {
  if (!On(kN822EE628)) [[unlikely]] return false;
  const auto word = [base](uint32_t a) {
    const uint8_t* p = ::banjo::hot::Raw(base, a);
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | uint32_t(p[3]);
  };
  const uint32_t job = ctx.r3.u32;
  uint32_t n = word(job + 20u);
  if (n == 0) return true;
  const uint32_t table = word(job + 16u);
  uint32_t dst = 0, b = 0;
  s.Hand(kN822EE628, 0);
  for (uint32_t i = 0; i < n; ++i) {
    const uint32_t e = table + 16u * i;
    dst = word(e);
    const uint32_t a = word(e + 4u);
    b = word(e + 8u);
    const uint32_t d16 = dst & ~15u, a16 = a & ~15u, b16 = b & ~15u;
    const bool zero = b != dst && a != dst;
    const uint32_t lead = zero ? (dst & 16u) : 0u;
    if (!s.Note(d16 - lead, 1024u + lead)) [[unlikely]] return true;
    ::banjo::hot::Card(base, d16 - lead, 1024u + lead);
    if (zero)
      for (uint32_t k = 0; k < 8; ++k) std::memset(::banjo::hot::Raw(base, (dst + 128u * k) & ~31u), 0, 32);
    ctx.fpscr.enableFlushMode();
    for (uint32_t blk = 0; blk < 4; ++blk) {
      const uint32_t o = blk * 256u;
      simde__m128 av[16], bv[16];
      for (uint32_t j = 0; j < 16; ++j) {
        av[j] = simde_mm_castsi128_ps(
            REX_BSWAP128(simde_mm_load_si128((const simde__m128i*)::banjo::hot::Raw(base, a16 + o + 16u * j))));
        bv[j] = simde_mm_castsi128_ps(
            REX_BSWAP128(simde_mm_load_si128((const simde__m128i*)::banjo::hot::Raw(base, b16 + o + 16u * j))));
      }
      for (uint32_t j = 0; j < 16; ++j)
        simde_mm_store_si128((simde__m128i*)::banjo::hot::Raw(base, d16 + o + 16u * j),
                             REX_BSWAP128(simde_mm_castps_si128(simde_mm_add_ps(av[j], bv[j]))));
    }
    n = word(job + 20u);  // the original reads the count again after every entry
  }
  ctx.r4.s64 = 160;
  ctx.r5.s64 = 176;
  ctx.r6.s64 = 192;
  ctx.r7.s64 = 208;
  ctx.r8.s64 = 128;
  ctx.r9.u64 = uint64_t(b) + 1056u;
  ctx.r10.u64 = uint64_t(dst) + 1056u;
  return true;
}

}  // namespace n_822EE628

namespace blur_jq34 {
// A big-endian guest float / word / doubleword through its own host pointer (as the generated loads and stores).
[[gnu::always_inline]] inline uint32_t LoadW(uint8_t* base, uint32_t ea) {
  const uint8_t* p = ::banjo::hot::Raw(base, ea);
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | uint32_t(p[3]);
}
[[gnu::always_inline]] inline float LoadF(uint8_t* base, uint32_t ea) {
  const uint32_t w = LoadW(base, ea);
  float f;
  std::memcpy(&f, &w, 4);
  return f;
}
[[gnu::always_inline]] inline void StoreW(uint8_t* base, uint32_t ea, uint32_t w) {
  uint8_t* p = ::banjo::hot::Raw(base, ea);
  p[0] = uint8_t(w >> 24);
  p[1] = uint8_t(w >> 16);
  p[2] = uint8_t(w >> 8);
  p[3] = uint8_t(w);
}
[[gnu::always_inline]] inline void StoreD(uint8_t* base, uint32_t ea, uint64_t d) {
  StoreW(base, ea, uint32_t(d >> 32));
  StoreW(base, ea + 4u, uint32_t(d));
}
}  // namespace blur_jq34

// sub_822E4CE0 - an audio resampler's mix (linear interpolation): r4 = out (r5 floats, read and written), r7 = the
// source table, r8 / r5 the rate, f1 / f2 the two gains, f3 the sample before the table. For i < r5: x = i * (r8 / r5)
// in single precision, k = trunc(x), t = x - k; s0 = k ? src[k - 1] : f3, s1 = src[k]; out[i] = s0 * f2 + ((s1 - s0) *
// t + out[i] * f1), each step rounded to single as the original's fmuls / fsubs / fmadds. PowerPC converts through the
// stack: the counter goes in with std / lfd at -16(r1) and k comes out with fctiwz / stfd at -8(r1) / lwz of its low word
// at -4(r1). Console race (2026-11-11 15:18, rebased build): job workers 4 and 3, 3.0 % of a core; 65 % of its samples
// on that lwz (a 32-bit load from the second half of the doubleword just stored: no store forwarding) and most of the
// rest on the table loads. Here: the generated statements as they are, the two conversions in registers instead of
// through the stack, the stack slots written once at the end with what the original leaves there (-16: the last value
// stored, -8: the last fctiwz result), the output range noted and carded once.
// Registers, as the generated function writes them back (f3-f13, r6, r8, r9, r10; f1, f2, r4, r5, r7 unchanged):
// computed by the same statements. The flush mode: the generated code's own calls (off for this scalar work).
// Declined: r8 == r5 (the rate 1 path, its own unrolled loop: the copy runs it), an output range that wraps 4 GB or
// crosses the physical-view boundary.
namespace n_822E4CE0 {

template <class S>
[[gnu::always_inline]] inline bool Native(PPCContext& __restrict ctx, uint8_t* base, [[maybe_unused]] S& s) {
  if (!On(kN822E4CE0)) [[unlikely]] return false;
  if (ctx.r8.u32 == ctx.r5.u32) return false;
  const uint32_t n = ctx.r5.u32, out = ctx.r4.u32, sp = ctx.r1.u32;
  if (n != 0 && (n >= (1u << 30) || !detail::Straight(out, 4u * n))) [[unlikely]] return false;
  PPCRegister l_f1 = ctx.f1, l_f2 = ctx.f2, l_f3 = ctx.f3, l_f6 = ctx.f6, l_f7 = ctx.f7, l_f8 = ctx.f8,
              l_f9 = ctx.f9, l_f10 = ctx.f10, l_f11 = ctx.f11, l_f12 = ctx.f12, l_f13 = ctx.f13;
  PPCRegister l_r4 = ctx.r4, l_r5 = ctx.r5, l_r7 = ctx.r7, l_r8 = ctx.r8, l_r9 = ctx.r9, l_r10 = ctx.r10;
  PPCRegister r11{}, f0{};
  s.Hand(kN822E4CE0, 4u * n);
  if (!s.Note(sp - 16u, 16u)) [[unlikely]] return true;
  if (n != 0) {
    if (!s.Note(out, 4u * n)) [[unlikely]] return true;
    ::banjo::hot::Card(base, out, 4u * n);
  }
  // Setup: r5 and r8 through the stack as doublewords into f0 / f13, fcfid, frsp, fdivs.
  r11.u64 = l_r5.u64 & 0xFFFFFFFF;
  l_r10.u64 = l_r8.u64 & 0xFFFFFFFF;
  f0.u64 = r11.u64;
  l_f13.u64 = l_r10.u64;
  uint64_t slot16 = l_r10.u64;  // the last doubleword stored at -16(r1)
  l_f11.f64 = double(f0.s64);
  l_r10.s64 = 0;
  l_f12.f64 = double(l_f13.s64);
  ctx.fpscr.disableFlushMode();
  l_f9.f64 = double(float(l_f11.f64));
  l_f10.f64 = double(float(l_f12.f64));
  l_f11.f64 = REX_FDIVS(l_f10.f64, l_f9.f64);
  bool slot8 = false;
  uint64_t slot8v = 0;  // the last doubleword stored at -8(r1): fctiwz's result
  if (n != 0) {
    l_r9.s64 = l_r4.s64 + -4;
    uint32_t left = n;
    do {
      r11.u64 = l_r10.u64 & 0xFFFFFFFF;
      slot16 = r11.u64;
      f0.u64 = r11.u64;
      l_f13.f64 = double(f0.s64);
      REX_FLUSH_KNOWN_OFF();
      l_f12.f64 = double(float(l_f13.f64));
      l_f10.f64 = REX_FMULS(l_f12.f64, l_f11.f64);
      l_f9.s64 = rex::ppc::fctidz(l_f10.f64);
      l_f8.f64 = double(l_f9.s64);
      l_f7.f64 = double(float(l_f8.f64));
      l_f6.s64 = rex::ppc::fctiwz(l_f7.f64);
      slot8 = true;
      slot8v = l_f6.u64;
      r11.u64 = uint32_t(l_f6.u64);  // lwz -4(r1): the doubleword's low word
      l_f12.f64 = REX_FSUBS(l_f10.f64, l_f7.f64);
      if (r11.s32 == 0) {
        l_f13.f64 = double(blur_jq34::LoadF(base, l_r7.u32));
        f0.f64 = l_f3.f64;
      } else {
        r11.u64 = uint32_t(r11.u32 << 2);
        r11.u64 = r11.u64 + l_r7.u64;
        REX_FLUSH_KNOWN_OFF();
        f0.f64 = double(blur_jq34::LoadF(base, r11.u32 - 4u));
        l_f13.f64 = double(blur_jq34::LoadF(base, r11.u32));
      }
      REX_FLUSH_KNOWN_OFF();
      l_f10.f64 = double(blur_jq34::LoadF(base, l_r9.u32 + 4u));
      l_f9.f64 = double(float(l_f13.f64 - f0.f64));
      l_f8.f64 = double(float(l_f10.f64 * l_f1.f64));
      l_r10.s64 = l_r10.s64 + 1;
      l_f7.f64 = double(float(std::fma(l_f9.f64, l_f12.f64, l_f8.f64)));
      l_f6.f64 = double(float(std::fma(f0.f64, l_f2.f64, l_f7.f64)));
      const uint32_t ea = 4u + l_r9.u32;
      const float r = float(l_f6.f64);
      uint32_t rw;
      std::memcpy(&rw, &r, 4);
      blur_jq34::StoreW(base, ea, rw);
      l_r9.u32 = ea;
    } while (--left != 0);
  }
  // The two stack slots as the original leaves them (its stack stores are plain: no card).
  blur_jq34::StoreD(base, sp - 16u, slot16);
  if (slot8) blur_jq34::StoreD(base, sp - 8u, slot8v);
  ctx.f6 = l_f6;
  ctx.f7 = l_f7;
  ctx.f8 = l_f8;
  ctx.f9 = l_f9;
  ctx.f10 = l_f10;
  ctx.f11 = l_f11;
  ctx.f12 = l_f12;
  ctx.f13 = l_f13;
  ctx.r9 = l_r9;
  ctx.r10 = l_r10;
  return true;
}

}  // namespace n_822E4CE0
