// blur-nx (2026-11-17, the 24-hour run; the user: "also 5 more audio natives wouldn't hurt"): five more natives of Blur's
// audio graph, chosen from the 15 ms build's console profile of the audio threads (JQ Workers 3 / 4 and the sound
// update's XThread), plus the XDK's large memcpy (#224 of docs/cpu-ideas-196-225-1116.md). The rules are
// blur_hand_natives_audio.h's: these call guest code (hand.txt calls=, and virtual calls through the generated dispatch),
// so they decline under the guard and the guard compares the copies with the originals instead.
//
// What they share: each one is a walk over guest objects whose time is cache misses on the chain object -> vtable ->
// method -> dispatch table (the console samples sit on those loads). The natives
//   - make their virtual calls through REX_CALL_INDIRECT_FUNC_AT, whose one-entry site cache (CG-08,
//     tools/indirect_calls.py) takes the dispatch-table load away;
//   - prefetch the next object of the walk while the current one's call runs;
//   - run without the copy's context traffic, the callees' frames below the frame the original allocates.
// The audio they compute is the original's: the same calls with the same arguments, in the same order.
//
// The DRAFT rule as the other files: included inside `namespace banjo::hand` after banjo_hand_natives.h's own helpers;
// no #include lines.

namespace blur_audio {

[[gnu::always_inline]] inline uint64_t LdD(uint8_t* base, uint32_t a) {
  uint64_t v;
  std::memcpy(&v, ::banjo::hot::Raw(base, a), 8);
  return __builtin_bswap64(v);
}
[[gnu::always_inline]] inline void StB(uint8_t* base, uint32_t a, uint32_t v) {
  *::banjo::hot::Raw(base, a) = uint8_t(v);
  ::banjo::hot::Card(base, a, 1);
}
[[gnu::always_inline]] inline void StF(uint8_t* base, uint32_t a, double v) {  // stfs
  StW(base, a, std::bit_cast<uint32_t>(float(v)));
}
[[gnu::always_inline]] inline double LdF(uint8_t* base, uint32_t a) {  // lfs
  return double(Bits(LdW(base, a)));
}
[[gnu::always_inline]] inline void Prefetch(uint8_t* base, uint32_t a) { __builtin_prefetch(::banjo::hot::Raw(base, a)); }

// 822EB480 / 822EB4C0's parameter (8 bytes at p: an i16 modulator index, a u16 argument, a float): index -1 is the
// float itself, else the node's modulator [[node + 72] + 76][index] answers through its vtable's +48 (r3 = the
// modulator, r4 = the node, r5 = the argument; the result in f1). A leaf tail call in the original: no frame of its
// own, so the callee runs at the caller's r1.
[[gnu::always_inline]] inline double Param(PPCContext& __restrict ctx, uint8_t* base, uint32_t node, uint32_t p) {
  const int32_t index = int16_t(LdH(base, p));
  if (index == -1) return LdF(base, p + 4);
  const uint32_t mod = LdW(base, LdW(base, LdW(base, node + 72) + 76) + (uint32_t(index) << 2));
  ctx.r3.u64 = mod;
  ctx.r4.u64 = node;
  ctx.r5.u64 = LdH(base, p + 2);
  const uint32_t fn = LdW(base, LdW(base, mod) + 48);
  BLUR_CALL_VIRTUAL(fn);
  return ctx.f1.f64;
}

}  // namespace blur_audio

// ---------------------------------------------------------------------------------------------------------------------
// 822CBAD0 - a node's children told about an update (the sound update, XThread; r3 = the node, r4 = the update): the
// children at [+172] (count at +180, both read again after every call) get their vtable's +28 with r3 = the child,
// r4 = the update; then, unless the update's half at +14 is set, the same for the list at [+188] (count +196).
// Console (15 ms build): 0.8-1.0 ms a frame on the sound update's XThread, on the child's vtable and dispatch loads.
// ---------------------------------------------------------------------------------------------------------------------
namespace n_822CBAD0 {
template <class S>
[[gnu::always_inline]] inline void Walk(PPCContext& __restrict ctx, uint8_t* base, uint32_t node, uint32_t list,
                                        uint32_t count, uint32_t update) {
  using namespace blur_audio;
  for (uint32_t i = 0; i < LdW(base, node + count); ++i) {
    const uint32_t items = LdW(base, node + list);
    const uint32_t child = LdW(base, items + (i << 2));
    if (i + 1 < LdW(base, node + count)) Prefetch(base, LdW(base, items + ((i + 1) << 2)));
    ctx.r4.u64 = update;
    ctx.r3.u64 = child;
    const uint32_t fn = LdW(base, LdW(base, child) + 28);
    BLUR_CALL_VIRTUAL(fn);
  }
}
template <class S>
inline bool Native(PPCContext& __restrict ctx, uint8_t* base, [[maybe_unused]] S& s) {
  using namespace blur_audio;
  if (!On(kN822CBAD0) || !Runs<S>()) [[unlikely]] return false;
  const uint32_t node = ctx.r3.u32, update = ctx.r4.u32, sp0 = ctx.r1.u32;
  if (!detail::Straight(node, 200) || !detail::Straight(update, 16)) [[unlikely]] return false;
  ctx.r1.u64 = sp0 - 128u;
  Walk<S>(ctx, base, node, 172, 180, update);
  if (LdH(base, update + 14) == 0) Walk<S>(ctx, base, node, 188, 196, update);
  ctx.r1.u64 = sp0;
  return true;
}
}  // namespace n_822CBAD0

// ---------------------------------------------------------------------------------------------------------------------
// 822CD2B0 - a list of 8-byte entries {object, flag A, flag B} (r3 = the list: entries at [+0], count at +16, both read
// once): for each entry, read whole before anything is called, flag A calls 822CBA00 (r3 = [object + 72], r4 = the
// object); flag B calls 822CBB80 (r3 = [object + 72] again, r4 = the object, r5 = 1 when flag A is set or the byte at
// 0x82C8DFE4 is clear, else 0).
// Console (15 ms build): 0.6-1.0 ms a frame on JQ3 / JQ4, the samples on its callees' vtable and dispatch loads (LTO
// inlines them into it). The native also prefetches the next entry's object.
// ---------------------------------------------------------------------------------------------------------------------
namespace n_822CD2B0 {
template <class C, class S>
inline bool Native(PPCContext& __restrict ctx, uint8_t* base, S& s) {
  using namespace blur_audio;
  if (!On(kN822CD2B0) || !Runs<S>()) [[unlikely]] return false;
  const uint32_t list = ctx.r3.u32, sp0 = ctx.r1.u32;
  if (!detail::Straight(list, 20)) [[unlikely]] return false;
  const uint32_t count = LdW(base, list + 16), entries = LdW(base, list + 0);
  if (count == 0) return true;
  if (!detail::Straight(entries, count * 8u)) [[unlikely]] return false;
  ctx.r1.u64 = sp0 - 144u;
  for (uint32_t k = 0; k < count; ++k) {
    const uint64_t e = LdD(base, entries + 8u * k);
    const uint32_t object = uint32_t(e >> 32);
    const uint32_t a = uint32_t(e >> 24) & 0xFFu, b = uint32_t(e >> 16) & 0xFFu;
    if (k + 1 < count) Prefetch(base, LdW(base, entries + 8u * (k + 1)) + 72u);
    if (a != 0) {
      ctx.r4.u64 = object;
      ctx.r3.u64 = LdW(base, object + 72);
      C::sub_822CBA00(ctx, base, s);
    }
    if (b != 0) {
      ctx.r5.u64 = (a != 0 || LdB(base, 0x82C8DFE4u) == 0) ? 1 : 0;
      ctx.r4.u64 = object;
      ctx.r3.u64 = LdW(base, object + 72);
      C::sub_822CBB80(ctx, base, s);
    }
  }
  ctx.r1.u64 = sp0;
  return true;
}
}  // namespace n_822CD2B0

// ---------------------------------------------------------------------------------------------------------------------
// 8230B5E8 - a node's four sends (r3 = the node, r4 = the graph; reached through the tail calls 823016D0 / 823040B8):
// when the node's byte +31 is set, each send s = [+44 + 4i] with s+40 clear and s+41 odd hands each of the node's rows
// (count +22, read again after each) to 822EEF78: r3 = the queue at 0x82C9E220, r4 = r5 = the send's row
// ([s + 36] + 1024 k), r6 = the node's row ((+60 << 2) + [graph + 32] + 1024 k), f1 = the float at 0x82000BA0; then
// [+44 + 4i]+31 = 1. Then the node's +65 = whether any send is live (the same test).
// 822EEF78 inlined, as it reads: when the byte at 0x82C9E232 differs from the one at 0x82C9E350 the queue's count
// [+4] is cleared, and the first byte takes the second's value; with it clear the rows are added at once (822E9F20:
// r3 = r4 = the source, r5 = the destination); else {r3, r4, r6, f1} is appended to the queue's 16-byte entries
// ([+0], count +4, capacity +8) and a full queue is flushed (822EE948, r3 = the queue) and its count cleared.
// Console (15 ms build): ~0.4 ms a frame (the sound update's XThread and JQ3 / JQ4 through the tail calls).
// ---------------------------------------------------------------------------------------------------------------------
namespace n_8230B5E8 {
template <class C, class S>
inline bool Native(PPCContext& __restrict ctx, uint8_t* base, S& s) {
  using namespace blur_audio;
  if (!On(kN8230B5E8) || !Runs<S>()) [[unlikely]] return false;
  const uint32_t node = ctx.r3.u32, graph = ctx.r4.u32, sp0 = ctx.r1.u32;
  if (!detail::Straight(node, 68) || !detail::Straight(graph, 36)) [[unlikely]] return false;
  const auto live = [&](uint32_t send) {
    return send != 0 && LdB(base, send + 40) == 0 && (LdB(base, send + 41) & 1u) != 0;
  };
  if (LdB(base, node + 31) != 0) {
    const uint32_t mine = (LdW(base, node + 60) << 2) + LdW(base, graph + 32);
    const double gain = LdF(base, 0x82000BA0u);
    constexpr uint32_t kQueue = 0x82C9E220u;
    for (uint32_t i = 0; i < 4; ++i) {
      const uint32_t send = LdW(base, node + 44 + 4 * i);
      if (!live(send)) continue;
      const uint32_t rows = send ? LdW(base, send + 36) : 0;
      for (uint32_t k = 0; k < LdH(base, node + 22); ++k) {
        const uint32_t src = rows + (k << 10), dst = mine + (k << 10);
        // 822EEF78 (its frame below this one's)
        const uint32_t flag = LdB(base, 0x82C9E350u);
        if (LdB(base, 0x82C9E232u) != flag) {
          StW(base, kQueue + 4, 0);
        }
        const uint32_t now = LdB(base, 0x82C9E350u);
        StB(base, 0x82C9E232u, now);
        ctx.r1.u64 = sp0 - 176u - 112u;
        if (now == 0) {
          ctx.r3.u64 = src;
          ctx.r4.u64 = src;
          ctx.r5.u64 = dst;
          C::sub_822E9F20(ctx, base, s);
        } else {
          const uint32_t entry = LdW(base, kQueue + 0) + (LdW(base, kQueue + 4) << 4);
          StW(base, entry + 0, src);
          StW(base, entry + 4, src);
          StW(base, entry + 8, dst);
          StF(base, entry + 12, gain);
          const uint32_t n = LdW(base, kQueue + 4) + 1;
          StW(base, kQueue + 4, n);
          if (n == LdW(base, kQueue + 8)) {
            ctx.r3.u64 = kQueue;
            C::sub_822EE948(ctx, base, s);
            StW(base, kQueue + 4, 0);
          }
        }
      }
      StB(base, LdW(base, node + 44 + 4 * i) + 31, 1);
    }
  }
  StB(base, node + 65, 0);
  for (uint32_t i = 0; i < 4; ++i)
    if (live(LdW(base, node + 44 + 4 * i))) StB(base, node + 65, 1);
  ctx.r1.u64 = sp0;
  return true;
}
}  // namespace n_8230B5E8

// ---------------------------------------------------------------------------------------------------------------------
// 82302950 - a product of parameters, kept for the frame (r3 = the object, r4 = the node, r5 = a flag; result in f1):
// with r5 set, the float at 0x82000BA0. Else the state at [node + 84] + [object + 20] holds {product, stamp}: when its
// stamp is the frame's ([[0x82C8DE90] + 0x27BD8]) the product is returned; else the stamp is stored and the product
// recomputed over the parameters at [[object + 12] + 16] + [object + 24] (8 bytes each, their count the half at
// [[object + 12] + 16] + [object + 16] + 4, read again each time; at least two), each through Param (822EB480), the
// running product stored after each multiplication (fmuls), as the original does.
// Console (15 ms build): ~0.45 ms a frame on JQ3 / JQ4 and the sound update.
// ---------------------------------------------------------------------------------------------------------------------
namespace n_82302950 {
template <class S>
inline bool Native(PPCContext& __restrict ctx, uint8_t* base, [[maybe_unused]] S& s) {
  using namespace blur_audio;
  if (!On(kN82302950) || !Runs<S>()) [[unlikely]] return false;
  const uint32_t object = ctx.r3.u32, node = ctx.r4.u32, sp0 = ctx.r1.u32;
  if (!detail::Straight(object, 28) || !detail::Straight(node, 88)) [[unlikely]] return false;
  if (ctx.r5.u32 != 0) {
    ctx.f1.f64 = LdF(base, 0x82000BA0u);
    return true;
  }
  const uint32_t table = LdW(base, LdW(base, object + 12) + 16);
  const uint32_t state = LdW(base, node + 84) + LdW(base, object + 20);
  const uint32_t desc = LdW(base, object + 16) + table;
  uint32_t p = LdW(base, object + 24) + table;
  const uint32_t stamp = LdW(base, LdW(base, 0x82C8DE90u) + 0x27BD8u);
  if (LdW(base, state + 4) != stamp) {
    StW(base, state + 4, stamp);
    ctx.r1.u64 = sp0 - 144u;
    StF(base, state, Param(ctx, base, node, p));
    double f1 = Param(ctx, base, node, p + 8);
    StF(base, state, REX_FMULS(f1, LdF(base, state)));
    p += 16;
    for (uint32_t k = 2; k < LdH(base, desc + 4); ++k, p += 8) {
      f1 = Param(ctx, base, node, p);
      StF(base, state, REX_FMULS(f1, LdF(base, state)));
    }
    ctx.r1.u64 = sp0;
  }
  ctx.f1.f64 = LdF(base, state);
  return true;
}
}  // namespace n_82302950

// ---------------------------------------------------------------------------------------------------------------------
// 822EB500 - a gain from two parameters (r4 = the node, r5 / r6 the parameters; result in f1): a = Param(r5),
// b = Param(r6); b equal to the float at 0x82000BA0 gives a, else a * frsp(822954F28's result for
// (the float at 0x8202E408 * b) * the float at 0x8200251C) (fmuls each; 82954F28 is the CRT's exp, itself a native).
// Console (15 ms build): ~0.45 ms a frame on JQ3 / JQ4.
// ---------------------------------------------------------------------------------------------------------------------
namespace n_822EB500 {
template <class C, class S>
inline bool Native(PPCContext& __restrict ctx, uint8_t* base, S& s) {
  using namespace blur_audio;
  if (!On(kN822EB500) || !Runs<S>()) [[unlikely]] return false;
  const uint32_t node = ctx.r4.u32, pa = ctx.r5.u32, pb = ctx.r6.u32, sp0 = ctx.r1.u32;
  if (!detail::Straight(node, 76) || !detail::Straight(pa, 8) || !detail::Straight(pb, 8)) [[unlikely]] return false;
  ctx.r1.u64 = sp0 - 112u;
  const double a = Param(ctx, base, node, pa);
  const double b = Param(ctx, base, node, pb);
  if (b == LdF(base, 0x82000BA0u)) {
    ctx.f1.f64 = a;
  } else {
    const double t = REX_FMULS(LdF(base, 0x8202E408u), b);
    ctx.f1.f64 = REX_FMULS(t, LdF(base, 0x8200251Cu));
    C::sub_82954F28(ctx, base, s);
    ctx.f1.f64 = REX_FMULS(double(float(ctx.f1.f64)), a);
  }
  ctx.r1.u64 = sp0;
  return true;
}
}  // namespace n_822EB500

// ---------------------------------------------------------------------------------------------------------------------
// 82953D90 - the XDK's memcpy (#224; r3 = destination, r4 = source, r5 = bytes; r3 returned): it first stores r3 at
// r1 - 8 (std, its red zone) and copies forwards, in bytes, words, doublewords or 128-byte blocks by the alignment.
// Where the two ranges do not overlap (and the copy stays off the red zone) every path writes the same bytes: the
// native stores the red-zone word and copies with the host's memcpy. Overlapping copies decline (the original's
// forward order would show). Exact in memory and r3 (exact=r3); the scratch registers it leaves are the guard's skip set.
// Console (15 ms build): 0.36 ms a frame on JQ3 alone (the busiest C-runtime function of the profiles).
// ---------------------------------------------------------------------------------------------------------------------
namespace n_82953D90 {
template <class S>
[[gnu::always_inline]] inline bool Native(PPCContext& __restrict ctx, uint8_t* base, S& s) {
  if (!On(kN82953D90)) [[unlikely]] return false;
  const uint64_t n64 = ctx.r5.u64;
  if (n64 >= 0x80000000ull) [[unlikely]] return false;
  const uint32_t d = ctx.r3.u32, src = ctx.r4.u32, n = uint32_t(n64), red = ctx.r1.u32 - 8u;
  if (n != 0) {
    if (!detail::Straight(d, n) || !detail::Straight(src, n)) [[unlikely]] return false;
    if (uint64_t(d) < uint64_t(src) + n && uint64_t(src) < uint64_t(d) + n) [[unlikely]] return false;
    if (uint64_t(d) < uint64_t(red) + 8 && uint64_t(red) < uint64_t(d) + n) [[unlikely]] return false;
  }
  s.Hand(kN82953D90, n);
  if (!s.Note(red, 8)) [[unlikely]] return true;
  const uint64_t r3 = __builtin_bswap64(ctx.r3.u64);
  std::memcpy(::banjo::hot::Raw(base, red), &r3, 8);
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
      std::memcpy(dp, sp, n);
  }
  return true;
}
}  // namespace n_82953D90
