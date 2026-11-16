// blur-nx (2026-11-16, the 15 ms batch; the user: "I want 7 natives for the top 7 audio functions", then "Its ok for the
// audio functions to cheat a bit and not be exact as like as the game doesnt break"): hand-written natives of Blur's
// audio graph (docs/cpu-15ms-batch-1116.md, docs/cpu-threads-audio-breakdown-1116.md).
//
// The rules of these natives (they differ from the other files' in three ways):
//   - They may be INEXACT in the audio they compute (float operations fused or reordered, work on a row known to be
//     zero skipped), never in the graph's state: node stamps, gain bookkeeping, voice positions and every pointer the
//     game follows are written as the original writes them.
//   - So the guard cannot compare them: they decline while the guard runs (banjo::hot::GuardBusy, and any form but
//     the running one), and the guard compares the generated copy with the original instead.
//   - Some call guest functions (hand.txt calls=): C::sub_X(ctx, base, s), the header's struct (tools/
//     localize_native.py), with the callee's arguments in ctx and ctx.r1 at the frame the original allocates.
//
// The DRAFT rule as the other files: included inside `namespace banjo::hand` after banjo_hand_natives.h's own helpers;
// no #include lines.

namespace blur_audio {

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
[[gnu::always_inline]] inline uint32_t LdB(uint8_t* base, uint32_t a) { return *::banjo::hot::Raw(base, a); }
[[gnu::always_inline]] inline void StW(uint8_t* base, uint32_t a, uint32_t v) {
  v = __builtin_bswap32(v);
  std::memcpy(::banjo::hot::Raw(base, a), &v, 4);
  ::banjo::hot::Card(base, a, 4);
}
[[gnu::always_inline]] inline float Bits(uint32_t b) { return std::bit_cast<float>(b); }

// The running form only, outside the guard (see the file's comment).
template <class S>
[[gnu::always_inline]] inline bool Runs() {
  return std::is_same_v<S, ::banjo::hot::Fast> && !::banjo::hot::GuardBusy();
}

// One operation of the mixer's gain matrix on a destination row (the three kernels the node loop calls):
//   kAdd  822E9F20: d = d + s
//   kMad  822EA500: d = s * g + d (one rounding: REX_VMADDFP)
//   kRamp 8230CBB8: d = s * (n * step + start) + d, n the element's index 0..255 (both fused)
enum : uint8_t { kSkip = 0, kAdd, kMad, kRamp };
struct Op {
  uint8_t kind;
  uint32_t src;  // the source row (guest address)
  float g;       // kMad: the gain; kRamp: start
  float step;    // kRamp: (end - start) * the scalar at 0x82B9D114
};

// Row d (1024 bytes) through ops[0..n) in their order, element by element. fresh: the row holds the +0.0 the node loop
// zeroed it to (its first load skipped: the same values, unless another node wrote into this row meanwhile).
[[gnu::always_inline]] inline void MixRow(uint8_t* base, uint32_t d, const Op* ops, uint32_t n, bool fresh) {
  uint8_t* dp = ::banjo::hot::Raw(base, d);
  const uint8_t* sp[8];
  for (uint32_t k = 0; k < n; ++k) sp[k] = ::banjo::hot::Raw(base, ops[k].src);
  const simde__m128 zero = simde_mm_setzero_ps();
  const simde__m128 lane = simde_mm_set_ps(3.0f, 2.0f, 1.0f, 0.0f);
  for (uint32_t c = 0; c < 64; ++c) {  // 64 vectors of 4 floats, in memory order (LoadWords: each word swapped)
    simde__m128 acc = fresh ? zero : blur_swap::LoadWords(dp + 16 * c);
    for (uint32_t k = 0; k < n; ++k) {
      const simde__m128 sv = blur_swap::LoadWords(sp[k] + 16 * c);
      switch (ops[k].kind) {
        case kAdd:
          acc = simde_mm_add_ps(acc, sv);
          break;
        case kMad:
          acc = REX_VMADDFP(sv, simde_mm_set1_ps(ops[k].g), acc);
          break;
        default: {  // kRamp
          const simde__m128 idx = simde_mm_add_ps(simde_mm_set1_ps(float(4 * c)), lane);
          const simde__m128 gain = REX_VMADDFP(idx, simde_mm_set1_ps(ops[k].step), simde_mm_set1_ps(ops[k].g));
          acc = REX_VMADDFP(sv, gain, acc);
          break;
        }
      }
    }
    blur_swap::StoreWords(dp + 16 * c, acc);
  }
  ::banjo::hot::Card(base, d, 1024);
}

// One op on row d element by element, every word through its own host address (any range).
inline void MixRowSlow(uint8_t* base, uint32_t d, const Op& op) {
  for (uint32_t n = 0; n < 256; ++n) {
    const float sv = Bits(LdW(base, op.src + 4 * n));
    const float dv = Bits(LdW(base, d + 4 * n));
    float r;
    if (op.kind == kAdd) r = dv + sv;
    else if (op.kind == kMad) r = std::fma(sv, op.g, dv);
    else r = std::fma(sv, std::fma(float(n), op.step, op.g), dv);
    StW(base, d + 4 * n, std::bit_cast<uint32_t>(r));
  }
}

[[gnu::always_inline]] inline bool Apart(uint32_t a, uint32_t an, uint32_t b, uint32_t bn) {
  return !(uint32_t(a - b) < bn || uint32_t(b - a) < an);
}

}  // namespace blur_audio

// ---------------------------------------------------------------------------------------------------------------------
// sub_8230AD50 - the audio graph's mixer node (r3 = the node, r4 = the graph's frame context). Read from its generated
// statements after tools/stereo_mix.py and tools/blur_mix_nx.py (the stereo fold, the nx tolerances):
//   - returns at once when the node's stamp (+8) is the frame's (+0 of r4) or its buffer index (+4) is -1;
//   - zeroes its output rows (u16 +22 of them, 1 KB each at r4[+28] + index * 4: 8 dcbzl each);
//   - for each child (u16 +36; 8-byte entries {type, offset} at node + u16 +26 + u16 +38): processes it (type 0
//     8230A4D8, 2 822E17C8, 3 itself, 5 822BD548; the node r4[+24] + offset), then, when the node's +31 is set, mixes
//     each of its input rows (u16 +40, fewer in stereo) into each output row through the gain matrix: current gains at
//     node + u16 +26 + u16 +46, targets at + u16 +44, index ((inputs * child + j) * stride + i) * 4 (stride u16 +42).
//     The graph context's +44 / +45 zero the current / target gain first. Equal gains: 1.0 adds, 0 skips, any other
//     multiplies and adds; else a ramp from cur + r4[+16] * (tgt - cur) to cur + (r4[+16] + r4[+20]) * (tgt - cur);
//   - with +31 set, the node's effect stage 822BEDD0; on the frame's last sub-frame (r4 u16 +6 == u16 +4 - 1) the
//     targets copied over the current gains (82953D90).
// Console profile (the nx race): 537 samples, 5.0 ms a frame on JQ Worker 3 / 4; flat: node fields that miss the cache,
// context traffic around its calls, the inlined kernels.
// The native: the node's fields read once, the gain bookkeeping in the original's order, then each child's input rows
// in one pass over each output row (the kernels' own operations in the same order), the first pass over a zeroed row
// without loading it, one card per row written. Not written: the original's stack words (the gains it passes to its
// kernels by pointer: dead once it returns), the volatile registers (its callers are void call sites).
// Declines (the generated copy runs): the guard's forms, more than 8 input or output rows, a node or output range that
// wraps or crosses the physical-view boundary. Everything that could decline is decided before any work.
// ---------------------------------------------------------------------------------------------------------------------
namespace n_8230AD50 {
constexpr uint32_t kMaxRows = 8;  // inputs and outputs a node may have for the native (5.1 is 6)
template <class C, class S>
inline bool Native(PPCContext& __restrict ctx, uint8_t* base, S& s) {
  using namespace blur_audio;
  if (!On(kN8230AD50) || !Runs<S>()) [[unlikely]] return false;
  const uint32_t node = ctx.r3.u32, g = ctx.r4.u32, sp0 = ctx.r1.u32;
  if (!detail::Straight(node, 48) || !detail::Straight(g, 48)) [[unlikely]] return false;
  const uint32_t r11 = node + LdH(base, node + 26);
  const uint32_t kids = r11 + LdH(base, node + 38);
  const uint32_t cur = r11 + LdH(base, node + 46);
  const uint32_t tgt = r11 + LdH(base, node + 44);
  if (LdW(base, node + 8) == LdW(base, g + 0)) return true;
  const uint32_t index = LdW(base, node + 4);
  if (index == 0xFFFFFFFFu) return true;
  const uint32_t rows = (index << 2) + LdW(base, g + 28);
  const uint32_t outputs = LdH(base, node + 22);
  const uint32_t nkids = LdH(base, node + 36);
  const uint32_t inputs_field = LdH(base, node + 40);
  const uint32_t stride = LdH(base, node + 42);
  // Everything that could make the native decline is decided here, before any work (a child processed cannot be
  // taken back).
  if (outputs > kMaxRows || inputs_field > kMaxRows) [[unlikely]] return false;
  if (outputs && !detail::Straight(rows & ~127u, outputs * 1024u + 128u)) [[unlikely]] return false;
  // The zeroed rows: eight 128-byte blocks from each row's address rounded down to 128 (the original's dcbzl).
  for (uint32_t i = 0; i < outputs; ++i) {
    const uint32_t z = (rows + (i << 10)) & ~127u;
    ::banjo::hot::Card(base, z, 1024);
    std::memset(::banjo::hot::Raw(base, z), 0, 1024);
  }
  bool fresh[kMaxRows];
  for (uint32_t i = 0; i < kMaxRows; ++i) fresh[i] = (rows & 127u) == 0;
  const bool mixes = LdB(base, node + 31) != 0;
  const uint32_t pool = LdW(base, g + 24);
  const float one = Bits(0x3F800000u), nought = Bits(0x00000000u);  // the image's 1.0f / 0.0f (0x82000BA4 / BA0)
  const uint32_t frame = sp0 - 208u;
  for (uint32_t k = 0; k < nkids; ++k) {
    const uint32_t entry = kids + 8u * k;
    const uint32_t type = LdW(base, entry);
    const uint32_t child = pool + LdW(base, entry + 4);
    const bool processed = type == 0 || type == 2 || type == 3 || type == 5;
    if (processed) {
      ctx.r3.u64 = child;
      ctx.r4.u64 = g;
      ctx.r1.u64 = frame;
      if (type == 0) C::sub_8230A4D8(ctx, base, s);
      else if (type == 2) C::sub_822E17C8(ctx, base, s);
      else if (type == 3) C::sub_8230AD50(ctx, base, s);
      else C::sub_822BD548(ctx, base, s);
      ctx.r1.u64 = sp0;
    }
    if (!mixes) continue;
    // The child's rows: r4[+28] for types 0, 2, 3, r4[+32] for 5, address 0 for the others (as the original).
    uint32_t src = 0;
    if (processed) src = (LdW(base, child + 4) << 2) + (type == 5 ? LdW(base, g + 32) : LdW(base, g + 28));
    const uint32_t inputs = REX_BLUR_STEREO_INPUTS(inputs_field, child);  // at most inputs_field
    if (!inputs || !outputs) continue;
    const bool stereo = REX_BLUR_STEREO_ON(node);
    Op ops[kMaxRows][kMaxRows];
    // The gain bookkeeping in the original's order (j, then i): the resets come before the reads of that pair, and the
    // stereo fold reads the whole gain row, which the next pair's resets change.
    for (uint32_t j = 0; j < inputs; ++j) {
      for (uint32_t i = 0; i < outputs; ++i) {
        const uint32_t w = ((inputs_field * k + j) * stride + i) << 2;
        if (LdB(base, g + 44)) {
          StW(base, w + cur, 0);
          if (stereo)
            for (uint32_t kk = 2; kk < stride; ++kk) StW(base, w - (i << 2) + (kk << 2) + cur, 0);
        }
        if (LdB(base, g + 45)) {
          StW(base, w + tgt, 0);
          if (stereo)
            for (uint32_t kk = 2; kk < stride; ++kk) StW(base, w - (i << 2) + (kk << 2) + tgt, 0);
        }
        const uint32_t tb = stereo ? uint32_t(REX_BLUR_STEREO_FOLD(w + tgt, i, stride)) : LdW(base, w + tgt);
        const double f0 = rex::ppc::SingleToDoublePure(tb);
        const double f13 = double(stereo ? std::bit_cast<float>(uint32_t(REX_BLUR_STEREO_FOLD(w + cur, i, stride)))
                                         : Bits(LdW(base, w + cur)));
        Op& op = ops[j][i];
        op.src = src + (j << 10);
        if ((REX_BLUR_NX_RAMP_SAME(f13, f0) ? f0 : f13) == f0) {
          if ((REX_BLUR_NX_UNITY(f0, double(one)) ? double(one) : f0) == double(one)) {
            op.kind = kAdd;
          } else if ((REX_BLUR_NX_SILENT(f0) ? double(nought) : f0) == double(nought)) {
            op.kind = kSkip;
          } else {
            op.kind = kMad;
            op.g = Bits(rex::ppc::QuietSingleBits(tb));
          }
        } else {
          const double f12 = double(Bits(LdW(base, g + 16)));
          const double f10 = double(Bits(LdW(base, g + 20)));
          const double f11 = REX_FSUBS(f0, f13);
          const double f9 = REX_FADDS(f10, f12);
          const float start = float(REX_FMADDS_KNOWN(f12, f11, f13));
          const float end = float(REX_FMADDS_KNOWN(f9, f11, f13));
          op.kind = kRamp;
          op.g = start;
          op.step = (end - start) * Bits(LdW(base, 0x82B9D114u));  // vsubfp128, vmulfp128 (8230CBB8)
        }
      }
    }
    // The mixing. Normally one pass per output row with this child's input rows in order (j); a source that is also a
    // destination row, or a source range the host cannot address in one stretch, goes op by op in the original's order.
    bool plain = detail::Straight(src, inputs << 10);
    for (uint32_t i = 0; i < outputs && plain; ++i) plain = Apart(rows + (i << 10), 1024, src, inputs << 10);
    if (plain) {
      for (uint32_t i = 0; i < outputs; ++i) {
        Op list[kMaxRows];
        uint32_t n = 0;
        for (uint32_t j = 0; j < inputs; ++j)
          if (ops[j][i].kind != kSkip) list[n++] = ops[j][i];
        if (!n) continue;
        MixRow(base, rows + (i << 10), list, n, fresh[i]);
        fresh[i] = false;
      }
    } else {
      for (uint32_t j = 0; j < inputs; ++j)
        for (uint32_t i = 0; i < outputs; ++i)
          if (ops[j][i].kind != kSkip) {
            MixRowSlow(base, rows + (i << 10), ops[j][i]);
            fresh[i] = false;
          }
    }
  }
  if (mixes) {
    ctx.r3.u64 = node;
    ctx.r4.u64 = g;
    ctx.r1.u64 = frame;
    C::sub_822BEDD0(ctx, base, s);
    ctx.r1.u64 = sp0;
  }
  if (int32_t(LdH(base, g + 6)) == int32_t(LdH(base, g + 4) - 1u)) {
    const uint32_t bytes = (LdH(base, node + 42) * LdH(base, node + 40) * LdH(base, node + 36)) << 2;
    if (bytes && detail::Straight(cur, bytes) && detail::Straight(tgt, bytes)) {  // 82953D90: the bytes as they are
      ::banjo::hot::Card(base, cur, bytes);
      std::memmove(::banjo::hot::Raw(base, cur), ::banjo::hot::Raw(base, tgt), bytes);
    } else {
      for (uint32_t b = 0; b < bytes; b += 4) StW(base, cur + b, LdW(base, tgt + b));
    }
  }
  return true;
}
}  // namespace n_8230AD50
