// blur-nx (2026-11-11): hand-written natives, batch 3c, for hot game functions of the console race profile (2026-11-11
// 15:18, rebased build 46b3f6d6), mostly job workers 0-2 and 4. DRAFT: included inside `namespace banjo::hand` after the
// helpers of banjo_hand_natives.h (no #include lines).
//
// KEYS (each native's On(kN<ADDRESS>) key; add them to blur_hand_keys.cpp / banjo_hand_ids.h as the others):
//   kN82432918   sub_82432918, hot path (slot already holds the key)
//   kN8251E438   sub_8251E438, whole function (ring + list push)
//   kN82526568   sub_82526568, whole function with its four callees inlined
//
// Every native returns false to decline (nothing written yet) and the machine copy runs. The registers a native leaves
// as it found them where the original changes them are scratch of the PowerPC calling convention (r3-r12, f0-f13,
// v0-v19 that the function itself never returns as a result): hand_batch3c.draft.txt lists in exact= the ones a caller
// may read.
#pragma once

namespace blur_b3c {

// Big-endian guest words through a host pointer (any alignment).
[[gnu::always_inline]] inline uint16_t Ld16(const uint8_t* p) {
  uint16_t v;
  std::memcpy(&v, p, 2);
  return __builtin_bswap16(v);
}
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
[[gnu::always_inline]] inline void St16(uint8_t* p, uint16_t v) {
  v = __builtin_bswap16(v);
  std::memcpy(p, &v, 2);
}
[[gnu::always_inline]] inline void St32(uint8_t* p, uint32_t v) {
  v = __builtin_bswap32(v);
  std::memcpy(p, &v, 4);
}
[[gnu::always_inline]] inline void St64(uint8_t* p, uint64_t v) {
  v = __builtin_bswap64(v);
  std::memcpy(p, &v, 8);
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

// The flush-mode switch is an `msr fpcr` without memory operand: the compiler may move arithmetic held in registers
// across it. Fence orders the loads after it; Pin ties a register value to the place where it stands.
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

// Guest word / byte at a guest address (the same host address as the generated code's accesses).
[[gnu::always_inline]] inline uint32_t R32(uint8_t* base, uint32_t a) { return Ld32(::banjo::hot::Raw(base, a)); }
[[gnu::always_inline]] inline uint16_t R16(uint8_t* base, uint32_t a) { return Ld16(::banjo::hot::Raw(base, a)); }

// Vector helpers (host lane order as the generated code keeps guest vectors: a guest word is a host 32-bit lane
// holding the value, so a splat of a word is the same in both).
using F4 = simde__m128;
using I4 = simde__m128i;
[[gnu::always_inline]] inline I4 Ci(F4 x) { return simde_mm_castps_si128(x); }
[[gnu::always_inline]] inline F4 Cf(I4 x) { return simde_mm_castsi128_ps(x); }
// vspltw128 x,0: the first guest word (host lane 3) in every lane.
[[gnu::always_inline]] inline I4 SplatW0(I4 x) { return simde_mm_shuffle_epi32(x, 0xFF); }
[[gnu::always_inline]] inline I4 Splat(uint32_t w) { return simde_mm_set1_epi32(int(w)); }
// The value of a vector whose four lanes are equal (a splat, or an operation on splats): the word stvewx128 stores.
[[gnu::always_inline]] inline uint32_t Lane0(F4 x) { return uint32_t(simde_mm_cvtsi128_si32(simde_mm_castps_si128(x))); }
// lvx128: the aligned 16 bytes at ea (rounded down), byte-reversed per word as the generated code does.
[[gnu::always_inline]] inline I4 LdV(uint8_t* base, uint32_t ea) {
  return REX_BSWAP128(simde_mm_load_si128((simde__m128i*)REX_RAW_ADDR(ea & ~0xFu)));
}
// lvlx128 (the generated statement): the bytes from ea to the end of its 16-byte block, left-justified, rest zero.
[[gnu::always_inline]] inline I4 LvlxV(uint8_t* base, uint32_t ea) {
  return REX_SHUFFLE_TABLE(simde_mm_load_si128((simde__m128i*)REX_RAW_ADDR(ea & ~0xFu)), &VectorMaskL[(ea & 0xFu) * 16]);
}
template <class... T>
[[gnu::always_inline]] inline void PinAll(T&... x) {
  (Pin(x), ...);
}

}  // namespace blur_b3c

// Guest stores with their logging (Note first; early `return true` when the guard's logged form's run is over) in
// the order of the original. `s` and `base` are the native's. W32: stw / stfs / stvewx128 with the card the original
// marks; W32N: without a card (stack stores); WV: stvx128 (16-byte aligned, card marked).
#define B3C_W32(a, v)                                                     \
  do {                                                                    \
    const uint32_t b3c_a = (a);                                           \
    if (!s.Note(b3c_a, 4)) [[unlikely]] return true;                      \
    blur_b3c::St32(::banjo::hot::Raw(base, b3c_a), (v));                  \
    ::banjo::hot::Card(base, b3c_a, 4);                                   \
  } while (0)
#define B3C_W32N(a, v)                                                    \
  do {                                                                    \
    const uint32_t b3c_a = (a);                                           \
    if (!s.Note(b3c_a, 4)) [[unlikely]] return true;                      \
    blur_b3c::St32(::banjo::hot::Raw(base, b3c_a), (v));                  \
  } while (0)
#define B3C_WV(a, v)                                                                      \
  do {                                                                                    \
    const uint32_t b3c_a = (a);                                                           \
    if (!s.Note(b3c_a, 16)) [[unlikely]] return true;                                     \
    simde_mm_store_si128((simde__m128i*)::banjo::hot::Raw(base, b3c_a), REX_BSWAP128(v)); \
    ::banjo::hot::Card(base, b3c_a, 16);                                                  \
  } while (0)

// sub_82432918 - HOT PATH: "is this key already in slot h of the object?" r3 = the object, r4 = a key (h16 at +0, h16 at
// +2 = the slot number h, words at +4, +8, +12). A slot is 16 bytes at obj + 16 h + 68 (three words: the key's first
// halfword sign-extended, its words +4 and +8), and the word at obj + 16 (h + 4) is compared with the key's word at +12
// (the next key of the list; the original then goes on to that one). Hot paths covered (the ones that end in "return 1"
// with no call): (a) h == -1 (nothing to do), and (b) slot h holds the key's three values and the following word
// equals the key's word at +12. Every other path calls (sub_8232CC40 or sub_82432300 after a store into the slot):
// declined with nothing written.
// Console race (15:18, rebased build 46b3f6d6): job workers, 0.56 % of a core, 8 samples on the entry (the stwu frame
// store and the key loads) and the slot compares. The machine copy writes back r8-r10 (the compare operands) and keeps
// r11, r29-r31 in locals; what the native removes is those write-backs and the flag-register chain (the compares
// are done on the loaded words directly). The gain is small: a read-only path.
// Memory: only the back chain, 4 bytes at r1 - 112 holding r1 (unless REX_BACK_CHAIN_ELIDE), which the original stores
// before it reads; the native stores it last and so declines when any word it reads overlaps those 4 bytes.
// Declines: key / slot / next-word ranges crossing 4 GB or the physical-view boundary; reads overlapping the frame
// store; slot or next word not equal (the call paths).
// Registers: r3 = 1 (the result); r1 as it came. Scratch not reproduced: r8, r9, r10 (and the locals r11, r12,
// r29-r31, cr6, xer).
namespace n_82432918 {
template <class S>
[[gnu::always_inline]] inline bool Native(PPCContext& __restrict ctx, uint8_t* base, [[maybe_unused]] S& s) {
  using namespace blur_b3c;
  if (!On(kN82432918)) [[unlikely]] return false;
  const uint32_t obj = ctx.r3.u32, key = ctx.r4.u32, sp = ctx.r1.u32;
  if (!detail::Straight(key, 16)) [[unlikely]] return false;
  const uint8_t* K = ::banjo::hot::Raw(base, key);
  const int32_t h = int16_t(Ld16(K + 2));
  [[maybe_unused]] uint32_t slot = 0, nxt = 0;
  if (h != -1) {
    slot = obj + (uint32_t(h) << 4) + 68u;
    nxt = obj + ((uint32_t(h) + 4u) << 4);
    if (!detail::Straight(slot, 12) || !detail::Straight(nxt, 4)) [[unlikely]] return false;
    const uint8_t* P = ::banjo::hot::Raw(base, slot);
    if (Ld32(P) != uint32_t(int32_t(int16_t(Ld16(K)))) || Ld32(P + 4) != Ld32(K + 4) || Ld32(P + 8) != Ld32(K + 8))
      return false;
    if (Ld32(::banjo::hot::Raw(base, nxt)) != Ld32(K + 12)) return false;
  }
#if !defined(REX_BACK_CHAIN_ELIDE)
  if (!detail::Straight(sp - 112u, 4) || Hit(sp - 112u, 4, key, 16) ||
      (h != -1 && (Hit(sp - 112u, 4, slot, 12) || Hit(sp - 112u, 4, nxt, 4)))) [[unlikely]]
    return false;
#endif
  s.Hand(kN82432918, 0);
#if !defined(REX_BACK_CHAIN_ELIDE)
  B3C_W32N(sp - 112u, sp);  // stwu r1,-112(r1): the old stack pointer
#endif
  ctx.r3.s64 = 1;
  return true;
}
}  // namespace n_82432918

// sub_8251E438 - pushes a debug-line / sample record into the object's ring and two lists (WHOLE function, a leaf):
// r3 = the object, f1 = a float, r4 = a vector (16 bytes at r4 & ~15), r6 = a slot number or -1 (then r6, r7 are the
// words at obj + 20 and obj + 24). The count at obj + 2704 is incremented; a 64-byte record a = obj + 912 + 64 count - 64
// gets f1 (as single, a + 32), the vector (a & ~15), r7 and r6 (a + 40, a + 36), the byte 1 (a + 56) and the constants
// 0xFE800000 (a + 48) and 0x7E800000 (a + 52). Then twice (slot r6, and, when the word at obj + 912 + 64 count - 24 is
// not negative, slot = the word at a + 40) one entry is pushed into the slot (obj + 144 slot): an index (count - 1)
// onto the list of 4-byte words at slot + 44 whose length is the word at slot + 108, and a float (+1.0 the first time,
// -1.0 the second) onto the list at slot + 112 whose length is the word at slot + 176. Every list length is read, used
// as an index, read again and incremented, as the original does.
// Console race (15:18, rebased build 46b3f6d6): job workers, 0.80 % of a core, 80 instructions; samples on the loads
// that follow the card marks (+0x140 4, +0x158 3) and the closing stores (+0x300 4): a card mark after every one of the
// 16 stores, the vector byte-swapped on its load and swapped back on its store, r3-r10 written back.
// What the native removes against the machine copy: the vector's two byte swaps (the 16 bytes are copied as they lie),
// the write-backs of r3-r10 and the constants' address arithmetic. Every other access is kept in the order and at the
// size of the original (the stores can alias the loads that follow them: the list lengths, the count, the vector), so
// it is memory-exact for any inputs and nothing is declined. The gain is small.
// The flush mode is OFF when it ends, as the original leaves it (stfs f1 disables it).
// Declines: none (the guard may end the logged run in the middle: nothing more is written then).
// Scratch not reproduced (exact= nothing): r3-r10 (the original leaves values of its own); f0, v63, r11 are locals.
namespace n_8251E438 {
template <class S>
[[gnu::always_inline]] inline bool Native(PPCContext& __restrict ctx, uint8_t* base, [[maybe_unused]] S& s) {
  using namespace blur_b3c;
  if (!On(kN8251E438)) [[unlikely]] return false;
  s.Hand(kN8251E438, 0);
  const uint32_t obj = ctx.r3.u32;
  const uint32_t cnt = R32(base, obj + 2704u) + 1u;
  const bool noslot = ctx.r6.s32 == -1;  // cmpwi cr6,r6,-1
  B3C_W32(obj + 2704u, cnt);
  uint8_t vec[16];
  std::memcpy(vec, ::banjo::hot::Raw(base, ctx.r4.u32 & ~15u), 16);  // lvx128 + stvx128 below: byte-swapped twice
  const uint32_t a = obj + 912u + (cnt << 6) - 64u;  // r10; the record; r11 = a + 64
  ctx.fpscr.disableFlushMode();
  Fence();
  float f1s = float(ctx.f1.f64);
  Pin(f1s);
  B3C_W32(a + 32u, U32(f1s));
  {
    const uint32_t va = a & ~15u;
    if (!s.Note(va, 16)) [[unlikely]] return true;
    std::memcpy(::banjo::hot::Raw(base, va), vec, 16);
    ::banjo::hot::Card(base, va, 16);
  }
  uint32_t r6v = ctx.r6.u32, r7v = ctx.r7.u32;
  if (noslot) {
    r6v = R32(base, obj + 20u);
    r7v = R32(base, obj + 24u);
  }
  B3C_W32(a + 40u, r7v);
  B3C_W32(a + 36u, r6v);
  const uint32_t k1 = r6v * 144u;  // r6 * 9 * 16
  {
    if (!s.Note(a + 56u, 1)) [[unlikely]] return true;
    *::banjo::hot::Raw(base, a + 56u) = 1;  // stb r5,56(r10)
    ::banjo::hot::Card(base, a + 56u, 1);
  }
  B3C_W32(a + 48u, ::rex::ppc::QuietSingleBits(REX_RO_CFG_U32(0x82002F6C, 0xFE800000u, REX_LOAD_U32(0x82002F6Cu))));
  B3C_W32(a + 52u, ::rex::ppc::QuietSingleBits(REX_RO_CFG_U32(0x82002F70, 0x7E800000u, REX_LOAD_U32(0x82002F70u))));
  // The first push: slot obj + k1, index count - 1 (the count is read again), the float +1.0.
  {
    const uint32_t c2 = R32(base, obj + 2704u);
    const uint32_t q = obj + k1;
    const uint32_t one = ::rex::ppc::QuietSingleBits(REX_RO_CFG_U32(0x82000BA4, 0x3F800000u, REX_LOAD_U32(0x82000BA4u)));
    B3C_W32((R32(base, q + 108u) << 2) + q + 44u, c2 - 1u);
    B3C_W32(q + 108u, R32(base, q + 108u) + 1u);
    B3C_W32((R32(base, q + 176u) << 2) + q + 112u, one);
    B3C_W32(q + 176u, R32(base, q + 176u) + 1u);
  }
  // The second push, when the word below the next record (obj + 912 + 64 count - 24, count read again) is >= 0.
  const uint32_t c3 = R32(base, obj + 2704u);
  if (int32_t(R32(base, obj + 912u + (c3 << 6) - 24u)) < 0) return true;
  {
    const uint32_t slot2 = R32(base, a + 40u);
    const uint32_t c4 = R32(base, obj + 2704u);
    const uint32_t q = obj + slot2 * 144u;
    const uint32_t neg = ::rex::ppc::QuietSingleBits(REX_RO_CFG_U32(0x820024C0, 0xBF800000u, REX_LOAD_U32(0x820024C0u)));
    B3C_W32((R32(base, q + 108u) << 2) + q + 44u, c4 - 1u);
    B3C_W32(q + 108u, R32(base, q + 108u) + 1u);
    B3C_W32((R32(base, q + 176u) << 2) + q + 112u, neg);
    B3C_W32(q + 176u, R32(base, q + 176u) + 1u);
  }
  return true;
}
}  // namespace n_8251E438

// sub_82526568 - WHOLE function, with its four callees inlined (sub_82521DC0 and its leaf sub_82520E70, sub_82521228,
// sub_82521148; the machine copy inlines them too but hands 8 to 20 registers through the context around each). r3 = a
// camera/view object, r4 and r5 = two output vectors. It reads a basis from the object (two lvx and a vmsum3fp dot
// product), forms a scale from two reciprocals / a reciprocal square root (Newton steps), calls sub_82521DC0 (when the
// first scale is not positive) or multiplies directly (when it is), a smoothstep (sub_82521228) of the result, and
// (when the word at obj + 880 is below 2) a second one (sub_82521148), and stores the two output vectors (r4: always;
// r5: a vector, or zero when the word at obj + 880 is 2 or more).
// Console race (15:18, rebased build 46b3f6d6): job workers, 0.8 % of a core, 276 instructions plus 4 callee copies.
// What the native removes against the machine copy: (1) the register hand-over around each of the four inlined
// callees (stores to and loads from the context of 8 to 20 registers each time), (2) the stack round trips through the
// frame: stvewx128 + lfs, and stfs + lvlx128 + vspltw128 about ten times (a 4-byte store followed by a 16-byte load of
// the same line: failed store forwarding on the A57), (3) the context copies of f1-f13, v0-v13 and r3-r10 at the return.
// Memory is written exactly as the original writes it, in its order and with its cards: the back chain (stwu, unless
// REX_BACK_CHAIN_ELIDE), the frame words at r1 - 240 + 80 / 84 / 88 and the callees' words below it (r1 - 240 - 8: the
// link register's low word, unless REX_LR_MOVES_ELIDE; - 16, - 32, - 48, - 128), the vector at + 96, the two outputs;
// every read from the object, the tables and the constants is made from guest memory where the original makes it.
// Only the 16-byte lvlx128 / lvx128 reads of stack words the function has just stored are taken from the registers
// that stored them (the 4-byte lfs reads of a just-stored word are made from memory: that forwards fine); that is
// exact because no other store of the function can hit the frame (declined below).
// The flush mode is switched exactly where the original switches it (the arithmetic around each switch is pinned) and
// ends ON as the original leaves it. ctx.lr ends as the original leaves it (the low word of lr, as sub_82521DC0
// reloads it from its save slot, unless REX_LR_MOVES_ELIDE).
// Declines: r1 not 16-aligned; the frame (r1 - 368 ... r1 - 128) or either output vector wrapping 4 GB or crossing the
// physical-view boundary; either output vector (r4, r5 rounded down to 16) overlapping the frame (the original would
// read its own stack words back after the store).
// Scratch not reproduced (exact= nothing): r3-r12, f0-f13, v0-v13 (and the locals r24-r31, f26-f31, v32-v63, cr6).
namespace n_82526568 {
template <class S>
[[gnu::always_inline]] inline bool Native(PPCContext& __restrict ctx, uint8_t* base, S& s) {
  using namespace blur_b3c;
  using ::rex::ppc::SingleToDoublePure;
  using ::rex::ppc::GeZeroAnyMode;
  if (!On(kN82526568)) [[unlikely]] return false;
  const uint32_t B = ctx.r3.u32, sp = ctx.r1.u32, o26 = ctx.r4.u32, o24 = ctx.r5.u32;
  if ((sp & 15u) != 0) [[unlikely]] return false;
  const uint32_t R = sp - 240u;  // r1 after stwu r1,-240(r1); every stack word below is R + offset
  const uint32_t a26 = o26 & ~15u, a24 = o24 & ~15u;
  if (!detail::Straight(R - 128u, 240u) || !detail::Straight(a26, 16u) || !detail::Straight(a24, 16u) ||
      Hit(R - 128u, 240u, a26, 16u) || Hit(R - 128u, 240u, a24, 16u)) [[unlikely]]
    return false;
  s.Hand(kN82526568, 0);
#if !defined(REX_BACK_CHAIN_ELIDE)
  B3C_W32N(R, sp);  // stwu r1,-240(r1)
#endif
  const uint32_t Q = R32(base, B + 892u);                  // r11
  const int32_t m = int32_t(R32(base, B + 880u));          // r31
  const uint32_t P = R32(base, Q + 7044u) + 344u;          // r30
  const uint32_t b = m < 2 ? P : P + 108u;                 // r10 / r8 / r9 (the record)
  const I4 zero = simde_mm_setzero_si128();                // v46
  const I4 k7f = simde_mm_set1_epi32(0x7FFFFFFF);          // v45: vsrw128 of -1 by 1
  const I4 vsign = simde_mm_set1_epi32(int(0x80000000u));  // vslw128 of 1 by -1
  const I4 v62i = SplatW0(LvlxV(base, b + 36u));
  const I4 v60i = SplatW0(LvlxV(base, B + 152u));
  const I4 v59i = REX_RO_V128(0x82000B80, 0x0080000000800000ull, 0x0080000000800000ull);
  ctx.fpscr.enableFlushMode();
  Fence();
  F4 v10 = Cf(Splat(0x3F800000u));  // vupkd3d128 v10,v46,0 (of zero)
  F4 v62 = Cf(v62i), v60 = Cf(v60i), v59 = Cf(v59i);
  PinAll(v62, v60, v59, v10);
  F4 v0 = REX_VREFP(v62);
  F4 v8 = simde_mm_cmpgt_ps(Cf(simde_mm_and_si128(Ci(v62), k7f)), v59);
  F4 v9 = REX_VNMSUBFP(v0, v62, v10);
  v0 = REX_VMADDFP(v0, v9, v0);
  F4 v13 = Cf(REX_VSEL(Ci(v8), zero, Ci(v0)));
  F4 v55 = simde_mm_mul_ps(v60, v13);
  Pin(v55);
  const uint32_t w80 = Lane0(v55);
  B3C_W32(R + 80u, w80);  // stvewx128 v55,r0,r7
  F4 v54 = Cf(LdV(base, B + 32u)), v53 = Cf(LdV(base, B + 448u));
  F4 v52 = simde_mm_dp_ps(v53, v54, 0xEF);
  Pin(v52);
  const uint32_t w84 = Lane0(v52);
  B3C_W32(R + 84u, w84);  // stvewx128 v52,r0,r4
  double f26 = SingleToDoublePure(w84), f27 = SingleToDoublePure(w80);  // lfs f26,84(r1); lfs f27,80(r1)
  double f29 = SingleToDoublePure(REX_RO_CFG_U32(0x82000BA0, 0x0u, REX_LOAD_U32(0x82000BA0u)));
  PPCCRRegister cr6{};
  ::rex::ppc::CompareAnyMode(cr6, f27, f29);
  const uint32_t w88 = ::rex::ppc::QuietSingleBits(w84);  // stfs f26,88(r1)
  B3C_W32N(R + 88u, w88);

  F4 vec96;  // the vector at R + 96
  I4 v62n;   // vspltw128 v62 of the word at obj + 712
  if (cr6.gt) {
    // ---- f27 > 0: the scale is computed here
    const uint32_t Q2 = R32(base, B + 892u);
    ctx.fpscr.disableFlushMode();
    Fence();
    double f0 = double(F32(R32(base, b + 28u)));
    double f13 = double(F32(R32(base, b + 4u)));
    double f12 = double(F32(R32(base, Q2 + 5996u)));
    v62n = SplatW0(LvlxV(base, B + 712u));
    double f11 = double(F32(R32(base, (uint32_t(m) << 5) + Q2 + 6036u)));
    double f10 = REX_FMULS(f11, f12);
    double f9 = REX_FMULS(f10, f0);
    double f8 = REX_FMULS(f9, f13);
    float f8s = float(f8);
    Pin(f8s);
    const uint32_t w8 = U32(f8s);
    B3C_W32N(R + 84u, w8);  // stfs f8,84(r1)
    ctx.fpscr.enableFlushModeUnconditional();
    Fence();
    F4 v49 = Cf(Splat(w8)), v62f = Cf(v62n);  // lvlx128 v50 of the word just stored, vspltw128 v49
    PinAll(v49, v62f);
    vec96 = simde_mm_mul_ps(v49, v62f);
    Pin(vec96);
    B3C_WV(R + 96u, Ci(vec96));
  } else {
    // ---- sub_82521DC0(r3 = R + 96, r4 = obj), inlined; its frame is R - 128
    const uint32_t Fr = R - 128u;
#if !defined(REX_LR_MOVES_ELIDE)
    const uint32_t lrw = uint32_t(ctx.lr);
    B3C_W32N(R - 8u, lrw);  // stw r12,-8(r1)
#endif
#if !defined(REX_BACK_CHAIN_ELIDE)
    B3C_W32N(Fr, R);  // stwu r1,-128(r1)
#endif
    const uint32_t Qa = R32(base, B + 892u);
    const int32_t ma = int32_t(R32(base, B + 880u));
    const uint32_t Pa = R32(base, Qa + 7044u) + 344u;
    const uint32_t ba = ma < 2 ? Pa : Pa + 108u;
    const uint32_t Qb = R32(base, B + 892u);
    ctx.fpscr.disableFlushMode();
    Fence();
    double f0 = double(F32(R32(base, ba + 24u)));
    double f13 = double(F32(R32(base, ba + 4u)));
    double f12 = double(F32(R32(base, Qb + 5988u)));
    const uint32_t r5a = R32(base, Qb + 10900u);
    double f11 = double(F32(R32(base, (uint32_t(ma) << 5) + Qb + 6036u)));
    double f10 = REX_FMULS(f11, f12);
    double f9 = REX_FMULS(f10, f0);
    double f8 = REX_FMULS(f9, f13);
    float f8s = float(f8);
    Pin(f8s);
    const uint32_t w8 = U32(f8s);
    B3C_W32N(Fr + 80u, w8);  // stfs f8,80(r1)
    // sub_82520E70(r3 = Fr + 96, r4 = obj, r5 = r5a): a splat of a table entry or of 1.0, stored at Fr + 96
    I4 splat;
    bool inrange = false;
    if (int32_t(r5a) >= 1) {
      const uint32_t Qc = R32(base, B + 892u);
      const int32_t mc = int32_t(R32(base, B + 880u));
      const uint32_t Pc = R32(base, Qc + 7044u) + 344u;
      const uint32_t bc = mc < 2 ? Pc : Pc + 108u;
      if (int32_t(R32(base, bc + 104u)) >= int32_t(r5a)) {
        const uint32_t tab = R32(base, bc + 100u);
        splat = SplatW0(LvlxV(base, tab + (r5a << 2) - 4u));
        inrange = true;
      }
    }
    if (!inrange) splat = SplatW0(LvlxV(base, 0x82000BA4u));
    B3C_WV(Fr + 96u, splat);
    // back in sub_82521DC0: v60 = splat(w8) * the vector it stored, stored at r7 = R + 96
    ctx.fpscr.enableFlushMode();
    Fence();
    F4 va = Cf(Splat(w8)), vb = Cf(splat);
    PinAll(va, vb);
    F4 v60c = simde_mm_mul_ps(va, vb);
    Pin(v60c);
    B3C_WV(R + 96u, Ci(v60c));
#if !defined(REX_LR_MOVES_ELIDE)
    ctx.lr = lrw;  // lwz r12,-8(r1); mtlr r12
#endif
    // back in sub_82526568: v43 = (the vector at R + 96) * splat(word at obj + 712), stored at R + 96
    v62n = SplatW0(LvlxV(base, B + 712u));
    ctx.fpscr.enableFlushMode();
    Fence();
    F4 vc = Cf(v62n);
    PinAll(vc, v60c);
    vec96 = simde_mm_mul_ps(v60c, vc);
    Pin(vec96);
    B3C_WV(R + 96u, Ci(vec96));
  }

  // ---- loc_825266E8: f0 = word 0 of the vector at R + 96, written through R + 84 and R + 80
  const uint32_t wv = Lane0(vec96);
  B3C_W32(R + 84u, wv);                                   // stvewx128 v42,r0,r10
  const uint32_t w80b = ::rex::ppc::QuietSingleBits(wv);  // lfs f0,84(r1); stfs f0,80(r1)
  B3C_W32N(R + 80u, w80b);
  I4 v40i = SplatW0(LvlxV(base, b + 32u));
  double f0m = ::rex::ppc::MulSingleAnyMode(f27, f27);  // fmuls f0,f27,f27
  I4 v37i = SplatW0(LvlxV(base, B + 148u));
  REX_FLUSH_KNOWN_ON();
  Fence();
  F4 v40 = Cf(v40i), v37 = Cf(v37i);
  PinAll(v40, v37, v10, v59);
  F4 w0 = REX_VREFP(v40);
  F4 w8m = simde_mm_cmpgt_ps(Cf(simde_mm_and_si128(Ci(v40), k7f)), v59);
  F4 w9 = REX_VNMSUBFP(w0, v40, v10);
  w0 = REX_VMADDFP(w0, w9, w0);
  F4 w13 = Cf(REX_VSEL(Ci(w8m), zero, Ci(w0)));
  F4 v36 = simde_mm_mul_ps(v37, w13);
  Pin(v36);
  const uint32_t w36 = Lane0(v36);
  B3C_W32(R + 84u, w36);                  // stvewx128 v36,r0,r10
  double f28 = SingleToDoublePure(w36);  // lfs f28,84(r1)
  ctx.fpscr.disableFlushModeUnconditional();
  Fence();
  PinAll(f27, f28, f0m);
  double g13 = REX_FMADDS(f28, f28, f0m);  // fmadds f13,f28,f28,f0
  double g12 = double(float(1.0 / std::sqrt(g13)));     // frsqrte f12,f13
  const double half = double(F32(REX_RO_U32(0x82001BD0, 0x3F000000)));
  double f31 = double(F32(REX_RO_U32(0x82000BA4, 0x3F800000)));
  double g11 = -g13;                                    // fneg f11,f13
  double g10 = double(float(g12));                      // frsp f10,f12
  double g9 = REX_FMULS(g10, g10);
  double g8 = (-REX_FMADDS(g9, g13, -f31));  // fnmsubs f8,f9,f13,f31
  double g7 = REX_FMULS(g8, g10);
  double g6 = REX_FMADDS(g7, half, g10);  // fmadds f6,f7,f0,f10
  double g5 = REX_FMULS(g6, g6);
  double g4 = (-REX_FMADDS(g5, g13, -f31));  // fnmsubs f4,f5,f13,f31
  double g3 = REX_FMULS(g4, g6);
  double g2 = REX_FMADDS(g3, half, g6);  // fmadds f2,f3,f0,f6
  double g1 = GeZeroAnyMode(g2) ? g2 : f29;           // fsel f1,f2,f2,f29
  double g0 = double(float(g1 * g13));                 // fmuls f0,f1,f13
  double f30 = GeZeroAnyMode(g11) ? f29 : g0;         // fsel f30,f11,f29,f0
  // ---- sub_82521228(f1 = f30), inlined (a smoothstep of f30)
  double s1;
  {
    const uint32_t Q4 = R32(base, B + 892u);
    const int32_t m4 = int32_t(R32(base, B + 880u));
    const uint32_t P4 = R32(base, Q4 + 7044u) + 344u;
    const uint32_t b4 = m4 < 2 ? P4 : P4 + 108u;
    const uint32_t p1 = R32(base, b4 + 8u);
    ctx.fpscr.disableFlushMode();
    Fence();
    PinAll(f30, f29);
    double c13 = double(F32(R32(base, p1 + 20u)));
    REX_FLUSH_KNOWN_OFF();
    double c11 = double(float(f30 * f30));
    const uint32_t p2 = R32(base, b4 + 8u);
    double c0 = double(F32(REX_RO_U32(0x82000BA4, 0x3F800000)));
    double c10 = double(float(f30 - c0));
    double c9 = double(F32(R32(base, p2 + 16u)));
    double c8 = REX_FSUBS(c0, c13);
    double c12 = double(F32(REX_RO_U32(0x82002528, 0x3FC00000)));
    double c7 = REX_FSUBS(c9, c0);
    double cz = double(F32(REX_RO_U32(0x82000BA0, 0x00000000)));
    double c6 = double(float(c11 * f30));
    double c5 = double(float(c11 + f30));
    double c4 = REX_FMULS(c10, c10);
    double c3 = REX_FMADDS(c6, f30, f30);
    double c2 = REX_FMULS(c5, c12);
    double c1 = REX_FMULS(c8, c4);
    double d12 = REX_FADDS(c7, c4);
    double d11 = REX_FADDS(c3, c0);
    double d9 = REX_FDIVS(c1, d12);
    double d8 = REX_FDIVS(c2, d11);
    double d7 = REX_FSUBS(c0, d9);
    double d6 = GeZeroAnyMode(c10) ? cz : d8;
    double d5 = GeZeroAnyMode(c10) ? d7 : cz;
    s1 = REX_FADDS(d5, d6);
    Pin(s1);
  }
  // ---- back: the second scale and the first output vector
  ctx.fpscr.disableFlushMode();
  Fence();
  PinAll(f30, f31, f27, f28, s1);
  double h13 = REX_FDIVS(f31, f30);
  double h12 = -std::fabs(f30);  // fnabs f12,f30 (and f11: the same value)
  I4 v33i = Splat(w80b);         // lvlx128 v35 of R + 80, vspltw128 v33
  I4 v34i = LdV(base, B + 64u);
  double h10 = double(float(h13 * f27));
  double h9 = double(float(h13 * f28));
  PinAll(h10, h9);
  ctx.fpscr.enableFlushModeUnconditional();
  Fence();
  F4 v33 = Cf(v33i), v34 = Cf(v34i);
  PinAll(v33, v34);
  F4 v32 = simde_mm_mul_ps(v34, v33);
  Pin(v32);
  double h8 = ::rex::ppc::MulSingleAnyMode(h10, s1);
  double h7 = ::rex::ppc::MulSingleAnyMode(h9, s1);
  double h6 = GeZeroAnyMode(h12) ? f29 : h8;
  double h5 = GeZeroAnyMode(h12) ? f29 : h7;
  const uint32_t w4 = U32(::rex::ppc::DoubleToSingleAnyMode(-h6));  // fneg f4,f6; stfs f4,84(r1)
  B3C_W32N(R + 84u, w4);
  F4 v61 = Cf(Splat(w4));  // lvlx128 v63 of R + 84, vspltw128 v61
  const uint32_t w3 = U32(::rex::ppc::DoubleToSingleAnyMode(-h5));  // fneg f3,f5; stfs f3,84(r1)
  B3C_W32N(R + 84u, w3);
  F4 v63 = simde_mm_mul_ps(v32, v61);
  Pin(v63);
  const uint32_t Q5 = R32(base, B + 892u);
  ctx.fpscr.disableFlushMode();
  Fence();
  double k0 = double(F32(R32(base, b + 20u)));
  double k13 = double(F32(R32(base, b + 4u)));
  double k12 = double(F32(R32(base, Q5 + 5992u)));
  double k11 = double(F32(R32(base, (uint32_t(m) << 5) + Q5 + 6036u)));
  double k10 = REX_FMULS(k11, k12);
  I4 v58i = simde_mm_xor_si128(LdV(base, B + 48u), vsign);  // lvx128 v59,r6,r4; vxor128 v58,v59,v60
  double k9 = REX_FMULS(k10, k0);
  double k8 = REX_FMULS(k9, k13);
  float k8s = float(k8);
  Pin(k8s);
  const uint32_t wb = U32(k8s);
  B3C_W32N(R + 80u, wb);  // stfs f8,80(r1)
  ctx.fpscr.enableFlushModeUnconditional();
  Fence();
  F4 v58 = Cf(v58i), v52b = Cf(Splat(wb)), v62f = Cf(v62n), v55b = Cf(Splat(w3)), v54b = Cf(Splat(w88)), v48 = v63;
  PinAll(v58, v52b, v62f, v55b, v54b, v48);
  F4 v51 = simde_mm_mul_ps(v52b, v62f);
  F4 v50 = simde_mm_mul_ps(v58, v51);
  v48 = REX_VMADDFP(v50, v55b, v48);
  F4 v47 = simde_mm_mul_ps(v48, v54b);
  Pin(v47);
  B3C_WV(a26, Ci(v47));  // stvx128 v47,r0,r26
  if (int32_t(R32(base, B + 880u)) >= 2) {
    B3C_WV(a24, zero);  // stvx128 v46,r0,r24
    return true;
  }
  // ---- the word at obj + 880 is below 2: the second smoothstep (sub_82521148) and the second output vector
  const uint32_t V = R32(base, B + 896u);
  ctx.fpscr.disableFlushModeUnconditional();
  Fence();
  PinAll(f30, f31, f28);
  double e13 = double(float(f30 - f31));  // fsubs f13,f30,f31
  I4 v44i = LdV(base, B + 64u);
  const double e0 = double(F32(REX_RO_U32(0x820024C0, 0xBF800000)));
  double e12 = double(float(f28 - e0));
  I4 v43i = LdV(base, V + 16u), v42i = LdV(base, V + 32u), v41i = LdV(base, V);
  I4 v40j = simde_mm_unpackhi_epi32(zero, v43i);  // vmrghw128 v40,v43,v46
  I4 v39i = simde_mm_unpackhi_epi32(v42i, v41i);  // vmrghw128 v39,v41,v42
  double e11 = GeZeroAnyMode(e13) ? f30 : f31;
  const uint32_t x88 = U32(float(e11));
  B3C_W32N(R + 88u, x88);                         // stfs f11,88(r1)
  I4 v38i = simde_mm_unpackhi_epi32(v40j, v39i);  // vmrghw128 v38,v39,v40
  double e10 = GeZeroAnyMode(e12) ? f28 : e0;
  double e9 = double(float(e10 - f31));
  double e8 = GeZeroAnyMode(e9) ? f31 : e10;
  const uint32_t x80 = U32(float(e8));
  B3C_W32N(R + 80u, x80);  // stfs f8,80(r1)
  // sub_82521148(r3 = R + 96, r4 = obj), inlined: its frame is R (it writes R - 16)
  F4 v47c;
  {
    const I4 c63 = SplatW0(LvlxV(base, B + 144u));
    const uint32_t wc = Lane0(Cf(simde_mm_and_si128(c63, k7f)));  // vsrw128 v60 = 0x7FFFFFFF; vand128 v59
    B3C_W32N(R - 16u, wc);                                         // stvewx128 v59,r0,r10 (no card)
    const uint32_t C9 = R32(base, B + 892u);
    const int32_t C8 = int32_t(R32(base, B + 880u));
    const uint32_t CP = R32(base, C9 + 7044u) + 344u;
    const uint32_t cb = C8 < 2 ? CP : CP + 108u;
    ctx.fpscr.disableFlushMode();
    Fence();
    double y0 = double(F32(R32(base, cb + 60u)));
    double y13 = double(F32(R32(base, cb + 68u)));
    double y12 = double(F32(R32(base, R - 16u)));  // lfs f12,-16(r1): the word just stored
    double y11 = REX_FSUBS(y13, y0);
    double y10 = REX_FSUBS(y12, y0);
    double y9 = double(F32(R32(base, cb + 72u)));
    double y8 = double(F32(R32(base, cb + 64u)));
    double yone = double(F32(REX_RO_U32(0x82000BA4, 0x3F800000)));
    double yzero = double(F32(REX_RO_U32(0x82000BA0, 0x00000000)));
    double y7 = REX_FDIVS(y10, y11);
    double y6 = REX_FSUBS(yone, y7);
    double y5 = GeZeroAnyMode(y6) ? y7 : yone;
    double y4 = -y5;
    double y3 = GeZeroAnyMode(y4) ? yzero : y5;
    double y2 = REX_FSUBS(yone, y3);
    double y1 = REX_FMULS(y9, y3);
    double yr = REX_FMADDS(y2, y8, y1);  // fmadds f0,f2,f8,f1
    float yrs = float(yr);
    Pin(yrs);
    const uint32_t wr = U32(yrs);
    B3C_W32N(R - 16u, wr);  // stfs f0,-16(r1)
    const I4 c58 = SplatW0(LvlxV(base, 0x82000BA4u));
    const I4 c57 = SplatW0(LvlxV(base, 0x82000BA0u));
    const I4 c54 = SplatW0(LvlxV(base, 0x82003000u));
    ctx.fpscr.enableFlushModeUnconditional();
    Fence();
    F4 z63 = Cf(c63), z54 = Cf(c54), z58 = Cf(c58), z57 = Cf(c57), z56 = Cf(Splat(wr));
    PinAll(z63, z54, z58, z57, z56);
    F4 z53 = simde_mm_mul_ps(z63, z54);
    F4 z49 = simde_mm_min_ps(z58, z53);
    F4 z48 = simde_mm_max_ps(z57, z49);
    v47c = simde_mm_mul_ps(z48, z56);
    Pin(v47c);
    B3C_WV(R + 96u, Ci(v47c));
  }
  // back in sub_82526568
  const I4 vp63 = LdV(base, 0x82000B80u);
  const I4 s37 = simde_mm_shuffle_epi32(v38i, 0x78), s12 = simde_mm_shuffle_epi32(v38i, 0x9C);
  const I4 s36 = simde_mm_shuffle_epi32(v44i, 0x9C), s13 = simde_mm_shuffle_epi32(v44i, 0x78);
  const I4 v32i = Splat(x88), v62i2 = Splat(x80);  // lvlx128 of R + 88 and R + 80, vspltw128
  ctx.fpscr.enableFlushMode();
  Fence();
  F4 q37 = Cf(s37), q36 = Cf(s36), q12 = Cf(s12), q13 = Cf(s13), q32 = Cf(v32i), q62 = Cf(v62i2), q63 = Cf(vp63);
  PinAll(q37, q36, q12, q13, q32, q62, q63, v47c, v10);
  F4 r9 = simde_mm_mul_ps(q36, q37);
  F4 r0 = REX_VREFP(q32);
  F4 r61 = Cf(simde_mm_and_si128(Ci(q32), k7f));
  F4 r60 = simde_mm_mul_ps(q62, v47c);
  F4 r8 = simde_mm_cmpgt_ps(r61, q63);
  r9 = REX_VNMSUBFP(q13, q12, r9);
  F4 r12 = REX_VNMSUBFP(r0, q32, v10);
  r0 = REX_VMADDFP(r0, r12, r0);
  F4 r13 = Cf(REX_VSEL(Ci(r8), zero, Ci(r0)));
  F4 r59 = simde_mm_mul_ps(r60, r13);
  Pin(r59);
  Pin(r9);
  const uint32_t w59 = Lane0(r59);
  B3C_W32(R + 84u, w59);  // stvewx128 v59,r0,r6
  double r7 = SingleToDoublePure(w59);
  double r6 = ::rex::ppc::MulSingleAnyMode(r7, f26);
  const uint32_t w5 = U32(::rex::ppc::DoubleToSingleAnyMode(-r6));  // fneg f5,f6; stfs f5,88(r1)
  B3C_W32N(R + 88u, w5);
  F4 r57 = Cf(Splat(w5));
  F4 r56 = simde_mm_mul_ps(r9, r57);
  Pin(r56);
  B3C_WV(a24, Ci(r56));  // stvx128 v56,r0,r24
  return true;
}
}  // namespace n_82526568

