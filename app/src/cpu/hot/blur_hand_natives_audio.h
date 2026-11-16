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

// ---------------------------------------------------------------------------------------------------------------------
// sub_822DA210 - the audio graph's compressor effect (one of the effect stage 822BEDD0's processors; r3 = its state,
// r4 = the rows: u16 +8 of them, 256 floats each, processed in place). The hottest code inside 822BEDD0's copy (its
// multiply-add chain at +0x32dc of the nx race's copy). Read from its generated statements:
//   - nothing (r3 = 1) when its +10 is 0 or the byte at 0x82C8E081 is set;
//   - every row gets a gain ramp: y[n] = x[n] * (n * slope + start), start = +76 * +84, slope = (+80 * +76 - start) *
//     the scalar at 0x82B9D114;
//   - with +88 == 0 (the detector on) each row also runs its level: acc = x[n]^2 * +16 + +20 * acc from the row's state
//     at +28 + 4 * row (written back), the rows' sum divided by the row count and its square root (frsqrte and two
//     Newton steps) stored at +52;
//   - then the attack / release of the gain (+80, the previous one kept at +84) toward the compression the level asks
//     (+68 threshold, +72 ratio, +60 attack, +64 release, 1.0 at most); r3 = 1.
// The native: the ramp four elements at a time; the level of up to four rows at once, one row a lane, with each lane's
// operations those of the original (fmuls, then fmadds fused in single; the flush mode as the original's), the sum
// and the tail as the original computes them: the same results. Flagged inexact anyway (the audio rules of this file):
// not written, the original's stack word at -160 (its int-to-float conversion) and its scratch float registers.
// ---------------------------------------------------------------------------------------------------------------------
namespace n_822DA210 {
template <class S>
[[gnu::always_inline]] inline bool Native(PPCContext& __restrict ctx, uint8_t* base, S& s) {
  using namespace blur_audio;
  (void)s;
  if (!On(kN822DA210) || !Runs<S>()) [[unlikely]] return false;
  const uint32_t p = ctx.r3.u32, buf = ctx.r4.u32;
  if (!detail::Straight(p, 92)) [[unlikely]] return false;
  const uint32_t rows = LdH(base, p + 8);
  if (rows && (!detail::Straight(buf, rows * 1024u) || !detail::Straight(p + 28, 4u * rows) ||
               !Apart(buf, rows * 1024u, p, 92) || !Apart(buf, rows * 1024u, p + 28, 4u * rows))) [[unlikely]]
    return false;
  ctx.r3.u64 = 1;
  if (LdB(base, p + 10) == 0 || LdB(base, 0x82C8E081u) != 0) return true;
  const float k = Bits(LdW(base, 0x82B9D114u));
  const float c76 = Bits(LdW(base, p + 76)), c84 = Bits(LdW(base, p + 84)), c80 = Bits(LdW(base, p + 80));
  const float start = c76 * c84;                       // fmuls
  const float slope = std::fma(c80, c76, -start) * k;  // fmsubs (fused, single), fmuls
  const simde__m128 vstart = simde_mm_set1_ps(start), vslope = simde_mm_set1_ps(slope);
  const simde__m128 lane = simde_mm_set_ps(3.0f, 2.0f, 1.0f, 0.0f);
  uint8_t* const rp = rows ? ::banjo::hot::Raw(base, buf) : nullptr;
  const bool meter = LdB(base, p + 88) == 0;
  double f6 = 0.0;
  if (!meter) {
    for (uint32_t r = 0; r < rows; ++r)
      for (uint32_t c = 0; c < 64; ++c) {
        uint8_t* q = rp + r * 1024u + 16u * c;
        const simde__m128 n = simde_mm_add_ps(simde_mm_set1_ps(float(4 * c)), lane);
        blur_swap::StoreWords(q, simde_mm_mul_ps(REX_VMADDFP(n, vslope, vstart), blur_swap::LoadWords(q)));
      }
    if (rows) ::banjo::hot::Card(base, buf, rows * 1024u);
  } else {
    const float b = Bits(LdW(base, p + 16)), a = Bits(LdW(base, p + 20));
    const simde__m128 vb = simde_mm_set1_ps(b), va = simde_mm_set1_ps(a);
    for (uint32_t r0 = 0; r0 < rows; r0 += 4) {
      const uint32_t nr = rows - r0 < 4 ? rows - r0 : 4;
      alignas(16) float st[4] = {0, 0, 0, 0};
      for (uint32_t l = 0; l < nr; ++l) st[l] = Bits(LdW(base, p + 28 + 4 * (r0 + l)));
      simde__m128 acc = simde_mm_load_ps(st);
      for (uint32_t c = 0; c < 64; ++c) {
        // The four rows' chunk c (elements 4c..4c+3), ramped in place, and transposed for the level (lane = row).
        simde__m128 x[4];
        const simde__m128 n = simde_mm_add_ps(simde_mm_set1_ps(float(4 * c)), lane);
        const simde__m128 gain = REX_VMADDFP(n, vslope, vstart);
        for (uint32_t l = 0; l < 4; ++l) {
          if (l < nr) {
            uint8_t* q = rp + (r0 + l) * 1024u + 16u * c;
            x[l] = blur_swap::LoadWords(q);
            blur_swap::StoreWords(q, simde_mm_mul_ps(x[l], gain));
          } else {
            x[l] = simde_mm_setzero_ps();
          }
        }
        // the transpose: e_k holds element 4c+k of each row
        const simde__m128 t0 = simde_mm_unpacklo_ps(x[0], x[1]), t1 = simde_mm_unpackhi_ps(x[0], x[1]);
        const simde__m128 t2 = simde_mm_unpacklo_ps(x[2], x[3]), t3 = simde_mm_unpackhi_ps(x[2], x[3]);
        const simde__m128 e0 = simde_mm_movelh_ps(t0, t2), e1 = simde_mm_movehl_ps(t2, t0);
        const simde__m128 e2 = simde_mm_movelh_ps(t1, t3), e3 = simde_mm_movehl_ps(t3, t1);
        acc = REX_VMADDFP(simde_mm_mul_ps(e0, e0), vb, simde_mm_mul_ps(va, acc));
        acc = REX_VMADDFP(simde_mm_mul_ps(e1, e1), vb, simde_mm_mul_ps(va, acc));
        acc = REX_VMADDFP(simde_mm_mul_ps(e2, e2), vb, simde_mm_mul_ps(va, acc));
        acc = REX_VMADDFP(simde_mm_mul_ps(e3, e3), vb, simde_mm_mul_ps(va, acc));
      }
      simde_mm_store_ps(st, acc);
      for (uint32_t l = 0; l < nr; ++l) {
        StW(base, p + 28 + 4 * (r0 + l), std::bit_cast<uint32_t>(st[l]));
        f6 = double(float(double(st[l]) + f6));  // fadds f6,f8,f6, row by row
      }
    }
    if (rows) ::banjo::hot::Card(base, buf, rows * 1024u);
    // loc_822DA5A0: the level, as the original computes it.
    const double f0 = 1.0, f7 = 0.0, half = double(Bits(0x3F000000u));
    const double f10 = double(float(double(int64_t(int32_t(rows)))));
    const double f9 = REX_FDIVS(f6, f10);
    const double f8 = double(float(1.0 / sqrt(f9)));
    const double f6n = -f9;
    const double f5 = double(float(f8));
    const double f4 = REX_FMULS(f5, f5);
    const double f3 = -REX_FMADDS(f4, f9, -f0);
    const double f2 = REX_FMULS(f3, f5);
    const double f1 = REX_FMADDS(f2, half, f5);
    const double f12 = REX_FMULS(f1, f1);
    const double f11 = -REX_FMADDS(f12, f9, -f0);
    const double f10b = REX_FMULS(f11, f1);
    const double f8b = REX_FMADDS(f10b, half, f1);
    const double f5b = rex::ppc::GeZeroAnyMode(f8b) ? f8b : f7;
    const double f4b = double(float(f5b * f9));
    const double f3b = rex::ppc::GeZeroAnyMode(f6n) ? f7 : f4b;
    StW(base, p + 52, std::bit_cast<uint32_t>(float(f3b)));
  }
  // loc_822DA600: the gain toward the compression the level asks for.
  {
    const double f0 = 1.0;
    const double f13 = double(Bits(LdW(base, p + 52)));
    const double f11 = double(Bits(LdW(base, p + 68)));
    const double f10 = double(float(1.0 / f13));
    const double f9 = double(float(f13 - f11));
    const double f8 = double(Bits(LdW(base, p + 72)));
    const uint32_t fb12 = LdW(base, p + 80);
    const double f12 = rex::ppc::SingleToDoublePure(fb12);
    StW(base, p + 84, rex::ppc::QuietSingleBits(fb12));
    const double f7 = REX_FMADDS(f8, f9, f11);
    const double f6 = double(float(f10 * f7));
    const double target = rex::ppc::GeZeroAnyMode(f9) ? f6 : f0;
    if (target < f12) {
      const double att = double(Bits(LdW(base, p + 60)));
      const double g = -REX_FMADDS(att, target, -f12);
      StW(base, p + 80, std::bit_cast<uint32_t>(float(g)));
      const double d = double(float(g - target));
      StW(base, p + 80, std::bit_cast<uint32_t>(float(rex::ppc::GeZeroAnyMode(d) ? g : target)));
    } else if (target > f12) {
      const double rel = double(Bits(LdW(base, p + 64)));
      const double g = REX_FMADDS(rel, target, f12);
      StW(base, p + 80, std::bit_cast<uint32_t>(float(g)));
      const double d = double(float(f0 - g));
      StW(base, p + 80, std::bit_cast<uint32_t>(float(rex::ppc::GeZeroAnyMode(d) ? g : f0)));
    }
  }
  return true;
}
}  // namespace n_822DA210
