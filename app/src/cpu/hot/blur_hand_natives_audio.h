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

// blur-nx (2026-11-17): a virtual call from a native: on the Switch through the site-cached dispatch (CG-08; the site arguments are not
// read there), elsewhere the plain dispatch (so the PC's indirect-call census never sees a site no bctrl has).
#if defined(__SWITCH__) && !defined(REX_SITE_CACHE_OFF)
#define BLUR_CALL_VIRTUAL(fn) REX_CALL_INDIRECT_FUNC_AT(0, 0, fn)
#else
#define BLUR_CALL_VIRTUAL(fn) REX_CALL_INDIRECT_FUNC(fn)
#endif

extern "C" float blur_nx_voice_floor;  // blur-nx (2026-11-17): app/src/blur_nx_mix.cpp, audio_nx_voice_floor

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

// ---------------------------------------------------------------------------------------------------------------------
// 8230B778 - the audio graph's output node (r3 = the node, r4 = the graph's frame context). Read from its generated
// statements: unless its child's type (+36) is -1, it processes the child as the mixer does (the node r4[+24] + +40;
// type 0 8230A4D8, 2 822E17C8, 3 8230AD50, 5 822BD548, else nothing), then copies the child's rows (r4[+28] or, for
// type 5, r4[+32] + the child's index * 4; address 0 for types 1 and 4) into its own (r4[+32] + +60 * 4), u16 +22 rows
// of 1 KB, each with 822EA3C0 (a 1 KB copy whose eight dcbz pieces at (dst + 128 k) & ~31 the copy overwrites).
// Console profile (the nx race): 103 samples, 1.0 ms a frame on JQ Worker 3 / 4: its own code is the dispatch and the
// copy loop's calls; the child's processing is the mixer's (its native).
// The native: the child through the header's calls (C), then one copy of all the rows when both ranges are 16-aligned,
// apart and plain (else each row through 822EA3C0, as the original), one card for them. The registers it leaves are
// the original's: the copy native's r3-r10 after the last row (r3 96 ... r9 208, r10 the last row + 1056), or with no
// rows the dispatch's r8-r10 (its LiveOut has them: it is reached through virtual calls).
// ---------------------------------------------------------------------------------------------------------------------
namespace n_8230B778 {
constexpr uint32_t kMaxRows = 64;
template <class C, class S>
inline bool Native(PPCContext& __restrict ctx, uint8_t* base, S& s) {
  using namespace blur_audio;
  if (!On(kN8230B778) || !Runs<S>()) [[unlikely]] return false;
  const uint32_t node = ctx.r3.u32, g = ctx.r4.u32, sp0 = ctx.r1.u32;
  if (!detail::Straight(node, 64) || !detail::Straight(g, 48)) [[unlikely]] return false;
  const uint32_t type = LdW(base, node + 36);
  if (type == 0xFFFFFFFFu) return true;
  if (LdH(base, node + 22) > kMaxRows) [[unlikely]] return false;
  const uint32_t child = LdW(base, g + 24) + LdW(base, node + 40);
  if (type == 0 || type == 2 || type == 3 || type == 5) {
    ctx.r3.u64 = child;
    ctx.r4.u64 = g;
    ctx.r1.u64 = sp0 - 128u;
    if (type == 0) C::sub_8230A4D8(ctx, base, s);
    else if (type == 2) C::sub_822E17C8(ctx, base, s);
    else if (type == 3) C::sub_8230AD50(ctx, base, s);
    else C::sub_822BD548(ctx, base, s);
    ctx.r1.u64 = sp0;
  }
  const uint32_t t2 = LdW(base, node + 36);
  uint32_t src = 0;
  if (t2 == 0 || t2 == 2 || t2 == 3 || t2 == 5) {
    const uint32_t at = t2 == 5 ? LdW(base, g + 32) : LdW(base, g + 28);
    src = (LdW(base, LdW(base, g + 24) + LdW(base, node + 40) + 4) << 2) + at;
  }
  const uint32_t r10 = LdW(base, node + 60) << 2;
  const uint32_t r9 = LdW(base, g + 32);
  const uint32_t dst = r10 + r9;
  const uint32_t rows = LdH(base, node + 22);
  if (!rows) {
    ctx.r8.u64 = 0;
    ctx.r9.u64 = r9;
    ctx.r10.u64 = r10;
    return true;
  }
  const uint32_t bytes = rows << 10;
  const bool plain = (dst & 15u) == 0 && (src & 15u) == 0 && detail::Straight(dst, bytes) &&
                     detail::Straight(src, bytes) && Apart(dst, bytes, src, bytes);
  if (plain) {
    ::banjo::hot::Card(base, dst, bytes);
    std::memmove(::banjo::hot::Raw(base, dst), ::banjo::hot::Raw(base, src), bytes);
    ctx.r3.s64 = 96;
    ctx.r4.s64 = 112;
    ctx.r5.s64 = 144;
    ctx.r6.s64 = 160;
    ctx.r7.s64 = 176;
    ctx.r8.s64 = 192;
    ctx.r9.s64 = 208;
    ctx.r10.s64 = int64_t(uint64_t(dst + ((rows - 1) << 10))) + 1056;
    return true;
  }
  ctx.r1.u64 = sp0 - 128u;
  for (uint32_t k = 0; k < rows; ++k) {
    ctx.r3.u64 = dst + (k << 10);
    ctx.r4.u64 = src + (k << 10);
    C::sub_822EA3C0(ctx, base, s);
  }
  ctx.r1.u64 = sp0;
  return true;
}
}  // namespace n_8230B778

// ---------------------------------------------------------------------------------------------------------------------
// 822E17C8 - the audio graph's voice node (r3 = the node, r4 = the graph's frame context). Read from its generated
// statements:
//   - returns at once when the node's stamp (+8) is the frame's;
//   - its gain for this frame: with +59 set, start = (r4 u16 +6 == 0 ? +40 : +44), end = +44; else start = +36 and
//     end = +40 + (r4[+20] + r4[+16]) * (+44 - +40) (fadds, fsubs, fmadds);
//   - with its rows (index +4 not -1; r4[+28] + index * 4) and its source object (r4[+24] + +52, when +52 and +48 are
//     not -1) and +57 (playing) set: when both gains are under the image's 0x8202E3FC (inaudible) or +31 is clear, its
//     rows are zeroed (822E4698) and the source is only advanced (its virtual +24); else the source renders into the rows
//     (virtual +20), its virtual +12 says whether it goes on (no: end gain 0), and the rows get the gain unless either
//     end is 1.0 (the game's own test): one gain (822E4810) or a ramp (822E48F8) per row; then virtual +12 again, and
//     +57 is cleared when it says no. Without rows, source or +57 the rows are zeroed;
//   - the node's effect stage (822BEDD0), the end gain stored at +36, and on the frame's last sub-frame +40 = +44.
// Console profile (the nx race): 235 samples, 2.2 ms a frame on JQ Worker 3 / 4: 47 % on one load through a pointer
// loaded just before it (the source object's vtable: a cache miss). The native cannot take the miss away; it runs the
// function's own control flow without the copy's context traffic, the calls through the header's struct (C) and the
// source's virtual calls through the generated dispatch. Exact (flagged inexact: it declines under the guard, as every
// native that calls guest code must). The registers it leaves: the last callee's, and r9 / r10 of the tail.
// ---------------------------------------------------------------------------------------------------------------------
namespace n_822E17C8 {
template <class C, class S>
inline bool Native(PPCContext& __restrict ctx, uint8_t* base, S& s) {
  using namespace blur_audio;
  if (!On(kN822E17C8) || !Runs<S>()) [[unlikely]] return false;
  const uint32_t node = ctx.r3.u32, g = ctx.r4.u32, sp0 = ctx.r1.u32;
  if (!detail::Straight(node, 64) || !detail::Straight(g, 48)) [[unlikely]] return false;
  if (LdW(base, node + 8) == LdW(base, g + 0)) return true;
  double f30, f31;
  if (LdB(base, node + 59) != 0) {
    f31 = double(Bits(LdW(base, node + 44)));
    f30 = LdH(base, g + 6) == 0 ? double(Bits(LdW(base, node + 40))) : f31;
  } else {
    const double f0 = double(Bits(LdW(base, node + 40)));
    ctx.f13.f64 = double(Bits(LdW(base, g + 20)));
    ctx.f12.f64 = double(Bits(LdW(base, g + 16)));
    ctx.f11.f64 = double(Bits(LdW(base, node + 44)));
    ctx.f10.f64 = REX_FADDS(ctx.f13.f64, ctx.f12.f64);
    ctx.f9.f64 = REX_FSUBS(ctx.f11.f64, f0);
    f30 = double(Bits(LdW(base, node + 36)));
    f31 = REX_FMADDS_KNOWN(ctx.f10.f64, ctx.f9.f64, f0);
  }
  const uint32_t frame = sp0 - 144u;
  const uint32_t index = LdW(base, node + 4);
  if (index != 0xFFFFFFFFu) {
    const uint32_t rows = (index << 2) + LdW(base, g + 28);
    uint32_t source = 0;
    if (LdW(base, node + 52) != 0xFFFFFFFFu && LdW(base, node + 48) != 0xFFFFFFFFu)
      source = LdW(base, g + 24) + LdW(base, node + 52);
    ctx.r1.u64 = frame;
    const auto zero_rows = [&] {
      ctx.r3.u64 = rows;
      ctx.r4.u64 = LdH(base, node + 22);
      C::sub_822E4698(ctx, base, s);
    };
    const auto call_virtual = [&](uint32_t slot) {  // the source object's vtable entry at +slot
      const uint32_t fn = LdW(base, LdW(base, source) + slot);
      BLUR_CALL_VIRTUAL(fn);  // 2026-11-17: the site-cached dispatch (CG-08) on the Switch
    };
    if (LdB(base, node + 57) != 0 && source != 0) {
      const double thr = double(Bits(LdW(base, 0x8202E3FCu)));
      const bool inaudible = !(f30 >= thr) && f31 < thr;
      // blur-nx (2026-11-17, the 24-hour run, inexact audio): a voice whose start and end gains are both under
      // audio_nx_voice_floor (Blur's own floor above is 1e-4, -80 dB) takes the game's own inaudible path (its rows
      // zeroed, the source advanced through its +24 instead of rendered through +20 / +12).
      const bool floored = !inaudible && blur_nx_voice_floor > 0.0f && !(std::fabs(f30) >= double(blur_nx_voice_floor)) &&
                           std::fabs(f31) < double(blur_nx_voice_floor);
      REX_NX_CENSUS(35);
      REX_NX_CENSUS_IF(inaudible, 36);
      REX_NX_CENSUS_IF(floored, 37);
      if (inaudible || floored || LdB(base, node + 31) == 0) {
        zero_rows();
        ctx.r3.u64 = source;
        ctx.r4.u64 = g;
        call_virtual(24);
      } else {
        ctx.r3.u64 = source;
        ctx.r4.u64 = rows;
        ctx.r5.u64 = g;
        call_virtual(20);
        ctx.r3.u64 = source;
        ctx.r4.u64 = g + 36;
        call_virtual(12);
        if ((ctx.r3.u32 & 0xFFu) == 0) f31 = double(Bits(0x00000000u));
        const double one = double(Bits(0x3F800000u));
        if (!(f30 == one) && !(f31 == one)) {
          const uint32_t n = LdH(base, node + 22);
          for (uint32_t k = 0; k < n; ++k) {
            ctx.r3.u64 = rows + (k << 10);
            ctx.r4.u64 = rows + (k << 10);
            if (f30 == f31) {
              ctx.f1.f64 = f31;
              C::sub_822E4810(ctx, base, s);
            } else {
              ctx.r7.s64 = 256;
              ctx.f2.f64 = f31;
              ctx.f1.f64 = f30;
              C::sub_822E48F8(ctx, base, s);
            }
          }
        }
      }
      ctx.r3.u64 = source;
      ctx.r4.u64 = g + 36;
      call_virtual(12);
      if ((ctx.r3.u32 & 0xFFu) == 0) {
        *::banjo::hot::Raw(base, node + 57) = 0;
        ::banjo::hot::Card(base, node + 57, 1);
      }
    } else {
      zero_rows();
    }
    ctx.r3.u64 = node;
    ctx.r4.u64 = g;
    C::sub_822BEDD0(ctx, base, s);
    ctx.r1.u64 = sp0;
  }
  StW(base, node + 36, std::bit_cast<uint32_t>(float(f31)));
  ctx.r10.u64 = LdH(base, g + 6);
  ctx.r9.s64 = int64_t(LdH(base, g + 4)) - 1;
  if (ctx.r10.s32 == ctx.r9.s32) StW(base, node + 40, rex::ppc::QuietSingleBits(LdW(base, node + 44)));
  return true;
}
}  // namespace n_822E17C8

// ---------------------------------------------------------------------------------------------------------------------
// 822D2378 - the sound system's update (its XThread; r3 = the system). Read from its generated statements:
//   - for each voice of the list at +12 (next at +68): its four flags at +52..+55 from its bytes +78..+81, its timing
//     (+20 += 1.0 and +14 + 1 wrapping at the source's u16 count, 0.0 then; +28 = 1 / +24; +24 = the source's +12, +36
//     = +28 * +20, the words +32, +36, +40 from +88, the global at 0x82C8DE84 and +92; +8 = the word at
//     [0x82C8DE90] + 0x27BD8), and, when it has something to do, its job entry {voice, its +4 word} added to the current
//     array of 8-byte entries; a full array (the word at 0x82C8E034 entries) is posted as jobs (8227C078) and a new one
//     taken (822F1390 from the system's pool when the byte at 0x82C8E00C is set, else 8217C648, its pointer kept in a
//     growing vector on the stack at +88 / +92 / +96, 824C6888 growing it by half);
//   - the last array posted, the jobs waited for (822862B0) when any voice was posted, the arrays freed (822F0E48, or
//     8217C750 each); the voice count stored at the system's +0x1E05C; 822ED708 on the list's lock at 0x82C9E220;
//   - for each voice again: +32 / +36 / +40 refreshed, its node list walked (822CBAD0), and for each of its +128
//     sub-entries (36 bytes at +120 + +88) with +39 set, at most [+120 + +88] of them, 822E4028; then 822EE948 and
//     822ED788 on the lock, and the vector's own storage freed.
// Console profile (the nx race): 105 samples, 1.0 ms a frame on its XThread: spread over the voice list's loads. The
// native: the same steps without the copy's context traffic, the job vector in the guest frame the callees read
// (sp - 272 + 80..96, as the original). Exact; flagged inexact (natives that call guest code decline under the guard).
// ---------------------------------------------------------------------------------------------------------------------
namespace n_822D2378 {
template <class C, class S>
inline bool Native(PPCContext& __restrict ctx, uint8_t* base, S& s) {
  using namespace blur_audio;
  if (!On(kN822D2378) || !Runs<S>()) [[unlikely]] return false;
  const uint32_t sys = ctx.r3.u32, sp0 = ctx.r1.u32, frame = sp0 - 272u;
  if (!detail::Straight(frame + 80, 20) || !detail::Straight(sys, 16)) [[unlikely]] return false;
  const auto StB = [&](uint32_t a, uint32_t v) {
    *::banjo::hot::Raw(base, a) = uint8_t(v);
    ::banjo::hot::Card(base, a, 1);
  };
  const auto StH = [&](uint32_t a, uint32_t v) {
    const uint16_t b = __builtin_bswap16(uint16_t(v));
    std::memcpy(::banjo::hot::Raw(base, a), &b, 2);
    ::banjo::hot::Card(base, a, 2);
  };
  const auto StStack = [&](uint32_t a, uint32_t v) {  // a stack word (no card: the copies mark none for the stack)
    v = __builtin_bswap32(v);
    std::memcpy(::banjo::hot::Raw(base, a), &v, 4);
  };
  const uint32_t r23 = LdW(base, LdW(base, 0x82C8DE90u) + 0x27BD8u);
  const bool pool = LdB(base, 0x82C8E00Cu) != 0;
  const uint32_t batch = LdW(base, 0x82C8E034u);
  StStack(frame + 80, 0);
  StStack(frame + 84, 0x82C90000u);
  StStack(frame + 88, 0);
  StStack(frame + 92, 0);
  StStack(frame + 96, 0);
  ctx.r1.u64 = frame;
  uint32_t r28 = 0, array = 0, voices = 0;
  bool posted = false;
  uint32_t pending = 0;
  const auto post = [&] {
    ctx.r3.u64 = 0x82B9D100u;
    ctx.r7.u64 = LdW(base, sys + 0x1E054u);
    ctx.r9.s64 = 4;
    ctx.r6.u64 = LdW(base, sys + 0x1E050u);
    ctx.r8.u64 = frame + 80;
    ctx.r5.s64 = 0;
    ctx.r4.u64 = array;
    C::sub_8227C078(ctx, base, s);
  };
  for (uint32_t v = LdW(base, sys + 12); v != 0; v = LdW(base, v + 68)) {
    const uint32_t b81 = LdB(base, v + 81), b80 = LdB(base, v + 80), b78 = LdB(base, v + 78), b79 = LdB(base, v + 79);
    ++voices;
    const uint32_t e = v + 8;
    StB(e + 44, b81 && !b80);
    StB(e + 45, !b81 && b80);
    StB(e + 46, b79 && !b78);
    StB(e + 47, !b79 && b78);
    StB(v + 81, b80);
    StB(v + 79, b78);
    double f0 = double(float(double(Bits(LdW(base, e + 12))) + 1.0));
    uint32_t r10 = LdH(base, e + 6) + 1u;
    const double f12 = double(Bits(LdW(base, e + 8)));
    const uint32_t src = LdW(base, v + 72);
    const uint32_t r9 = LdW(base, src + 8) & 0xFFFFu;
    const double f13 = double(Bits(LdW(base, src + 12)));
    if (!(r10 < r9)) {
      r10 = 0;
      f0 = 0.0;
    }
    const double rec = double(float(1.0 / f12));
    StW(base, e + 0, r23);
    StH(e + 6, r10);
    StW(base, e + 12, std::bit_cast<uint32_t>(float(f0)));
    StW(base, e + 8, std::bit_cast<uint32_t>(float(f13)));
    StH(e + 4, r9);
    StW(base, e + 20, std::bit_cast<uint32_t>(float(rec)));
    StW(base, e + 16, std::bit_cast<uint32_t>(float(double(float(rec * f0)))));
    StW(base, e + 24, LdW(base, v + 88));
    StW(base, e + 28, LdW(base, 0x82C8DE84u));
    StW(base, e + 32, LdW(base, v + 92));
    const uint32_t flag1 = b78 ? 0u : (r10 == 0 ? 1u : 0u);
    const uint32_t flag2 = (b79 && b78) ? 0u : ((b81 == 0 || b80 == 0) ? 1u : 0u);
    if (!flag1 && !flag2) {
      pending = LdW(base, frame + 80);
      continue;
    }
    StB(v + 5, flag2);
    StB(v + 4, flag1);
    StW(base, v + 0, v);
    StB(v + 6, LdB(base, 0x82C8E074u));
    if (LdW(base, frame + 80) == 0) {
      if (pool) {
        ctx.r3.u64 = sys + 0x1E070u;
        ctx.r4.u64 = uint32_t((batch + 1u) << 3);
        C::sub_822F1390(ctx, base, s);
        array = ctx.r3.u32;
      } else {
        ctx.r3.u64 = batch > 0x1FFFFFFFu ? 0xFFFFFFFFFFFFFFFFull : uint64_t(uint32_t(batch << 3));
        C::sub_8217C648(ctx, base, s);
        array = ctx.r3.u32;
        bool store = true;
        const uint32_t cap = LdW(base, frame + 92);
        if (int32_t(r28) == int32_t(cap)) {
          const int32_t three = int32_t(cap + (cap << 1));
          ctx.r5.u64 = r28;
          ctx.r3.u64 = frame + 88;
          ctx.r4.s64 = int64_t(three / 2) + 1;  // srawi + addze: halved toward zero
          C::sub_824C6888(ctx, base, s);
          store = (ctx.r3.u32 & 0xFFu) != 0;
          r28 = LdW(base, frame + 96);
        }
        if (store) {
          const uint32_t slot = (r28 << 2) + LdW(base, frame + 88);
          if (slot != 0) StW(base, slot, array);
        }
        ++r28;
        StStack(frame + 96, r28);
      }
    }
    const uint32_t at = LdW(base, frame + 80) << 3;
    StW(base, array + at, LdW(base, v + 0));
    StW(base, array + at + 4, LdW(base, v + 4));
    const uint32_t count = LdW(base, frame + 80) + 1u;
    StStack(frame + 80, count);
    pending = count;
    if (!(count < batch)) {
      post();
      pending = 0;
      StStack(frame + 80, 0);
    }
    posted = true;
  }
  if (pending != 0) {
    post();
    posted = true;
  }
  if (posted) {
    ctx.r5.u64 = 0x0FFFFFFFu;
    ctx.r3.u64 = LdW(base, sys + 0x1E050u);
    ctx.r4.s64 = 0;
    C::sub_822862B0(ctx, base, s);
  }
  if (pool) {
    ctx.r3.u64 = sys + 0x1E070u;
    C::sub_822F0E48(ctx, base, s);
  } else if (int32_t(r28) > 0) {
    const uint32_t data = LdW(base, frame + 88);
    for (uint32_t k = 0; k < r28; ++k) {
      ctx.r3.u64 = LdW(base, data + 4 * k);
      C::sub_8217C750(ctx, base, s);
    }
  }
  constexpr uint32_t kLock = 0x82C9E220u;
  StW(base, sys + 0x1E05Cu, voices);
  ctx.r3.u64 = kLock;
  C::sub_822ED708(ctx, base, s);
  for (uint32_t v = LdW(base, sys + 12); v != 0; v = LdW(base, v + 68)) {
    StW(base, v + 32, LdW(base, v + 88));
    StW(base, v + 40, LdW(base, v + 92));
    StW(base, v + 36, LdW(base, LdW(base, frame + 84) - 8572u));
    ctx.r4.u64 = v;
    ctx.r3.u64 = LdW(base, v + 72);
    C::sub_822CBAD0(ctx, base, s);
    if (LdW(base, v + 128) == 0) continue;
    const uint32_t most = LdW(base, LdW(base, v + 120) + LdW(base, v + 88));
    if (most == 0) continue;
    uint32_t done = 0;
    for (uint32_t k = 0, off = 0; k < LdW(base, v + 128) && done < most; ++k, off += 36) {
      const uint32_t r5 = LdW(base, v + 120) + LdW(base, v + 88);
      const uint32_t ent = r5 + off;
      if (LdB(base, ent + 39) != 0) {
        ctx.r5.u64 = r5;
        ctx.r4.u64 = ent + 4;
        ctx.r3.u64 = LdW(base, 0x82C8DE90u) + 0x1E0E8u;
        C::sub_822E4028(ctx, base, s);
        ++done;
      }
    }
  }
  ctx.r3.u64 = kLock;
  C::sub_822EE948(ctx, base, s);
  ctx.r3.u64 = kLock;
  C::sub_822ED788(ctx, base, s);
  ctx.r3.u64 = LdW(base, frame + 88);
  if (ctx.r3.u32 != 0) C::sub_8217C750(ctx, base, s);
  ctx.r1.u64 = sp0;
  return true;
}
}  // namespace n_822D2378

// ---------------------------------------------------------------------------------------------------------------------
// 822BEDD0 - the audio graph's effect stage (r3 = the node, r4 = the graph's frame context). Read from its generated
// statements: unless the node's stamp (+8) is the frame's or its index (+4) is -1, each of its u16 +24 effect entries
// (8 bytes {type, offset} at node + u16 +26; type -1 skipped) whose type - 2 is one of the jump table's 14 cases calls
// that effect (r3 = r4[+24] + offset, r4 = the node's rows r4[+28] + index * 4, r5 = r4, r6 = the node's +31), then
// the node is stamped with the frame (+8 = r4[+0]); r3 = 1. The cases: 0 822E80E0, 1 822E53C8, 2 822E57E0,
// 4 822E4FE8, 6 822E6E98, 10 822E61E0, 11 822E8418, 16 822DA210 (the compressor: its own native), 18 822E69A0,
// 19 822E98B0 (the convolution reverb: its own native), 21 822E95F8, 22 822BD5E8, 23 822BD6E8, 25 822E4EA0.
// Console profile (the nx race): 249 samples, 2.3 ms a frame with the effects its copy inlines (the compressor's
// chain the hottest). The native: the dispatch without the copy's context traffic, every effect through the header's
// calls (C), the natives among them first. Exact; flagged inexact (it calls guest code: it declines under the guard).
// ---------------------------------------------------------------------------------------------------------------------
namespace n_822BEDD0 {
template <class C, class S>
inline bool Native(PPCContext& __restrict ctx, uint8_t* base, S& s) {
  using namespace blur_audio;
  if (!On(kN822BEDD0) || !Runs<S>()) [[unlikely]] return false;
  const uint32_t node = ctx.r3.u32, g = ctx.r4.u32, sp0 = ctx.r1.u32;
  if (!detail::Straight(node, 32) || !detail::Straight(g, 32)) [[unlikely]] return false;
  const uint32_t list = node + LdH(base, node + 26);
  if (LdW(base, node + 8) != LdW(base, g + 0) && LdW(base, node + 4) != 0xFFFFFFFFu) {
    const uint32_t rows = (LdW(base, node + 4) << 2) + LdW(base, g + 28);
    ctx.r1.u64 = sp0 - 128u;
    for (uint32_t k = 0; k < LdH(base, node + 24); ++k) {
      const uint32_t entry = list + 8u * k;
      const uint32_t type = LdW(base, entry);
      if (type == 0xFFFFFFFFu) continue;
      const uint32_t effect = LdW(base, g + 24) + LdW(base, entry + 4);
      ctx.r6.u64 = LdB(base, node + 31);
      ctx.r3.u64 = effect;
      ctx.r4.u64 = rows;
      ctx.r5.u64 = g;
      switch (type - 2u) {
        case 0: C::sub_822E80E0(ctx, base, s); break;
        case 1: C::sub_822E53C8(ctx, base, s); break;
        case 2: C::sub_822E57E0(ctx, base, s); break;
        case 4: C::sub_822E4FE8(ctx, base, s); break;
        case 6: C::sub_822E6E98(ctx, base, s); break;
        case 10: C::sub_822E61E0(ctx, base, s); break;
        case 11: C::sub_822E8418(ctx, base, s); break;
        case 16: C::sub_822DA210(ctx, base, s); break;
        case 18: C::sub_822E69A0(ctx, base, s); break;
        case 19: C::sub_822E98B0(ctx, base, s); break;
        case 21: C::sub_822E95F8(ctx, base, s); break;
        case 22: C::sub_822BD5E8(ctx, base, s); break;
        case 23: C::sub_822BD6E8(ctx, base, s); break;
        case 25: C::sub_822E4EA0(ctx, base, s); break;
        default: break;
      }
    }
    ctx.r1.u64 = sp0;
    StW(base, node + 8, LdW(base, g + 0));
  }
  ctx.r3.u64 = 1;
  return true;
}
}  // namespace n_822BEDD0

// ---------------------------------------------------------------------------------------------------------------------
// 822E98B0 - the audio graph's convolution reverb (an effect: r3 = its state, r4 = the row it processes in place).
// Read from its generated statements. Nothing (r3 = 1) when its +10 is 0 or the byte at 0x82C9E1E5 is set. With +212
// set, a first call lays out its buffers (vector set-up, the 82262368 table): the native declines that call (before
// anything is written) and the copy does it. Every other call, in order:
//   - the shared index at 0x82C9E1EC (on the first ever call, from 0x82C9E1F0's bit 0: +164 - 256) advanced by 256
//     modulo the length N (+164); the row copied into the input ring (+60 + +264 * 4, 822EA3C0); the index advanced
//     by 256 modulo N again;
//   - for each of the +124 partitions k: 822EA170 (r3 = +64 + 1024 k, r4 = +60 + ((+264 + 256 k) mod N) * 4,
//     r5 = +80 + 1024 k); 822D84C8 (+64, N); 822E9198 (the state, +64, N, the words +116..+148 and +156 on the stack at
//     sp + 88); 822D8918 (+64, N);
//   - for each partition again: 822EA170 (in place, +80's), the gain +48 * the image's 0x8202E764 (822E4810), and
//     822E9F20 adding it into the output ring at +68 + ((+264 + 256 k) mod N) * 4;
//   - the output ring's block at +264 copied to the row (822EA3C0) and zeroed (eight dcbzl), +264 = (+264 + 256) mod N.
// Console profile (the nx race): 131 samples, 1.2 ms a frame (with its inlined callees) on JQ Worker 3 / 4. The
// native: the same calls through the header's struct (C) without the copy's context traffic, the index arithmetic in
// registers; the caller's parameter word at sp + 28 and the stack argument written as the original writes them, the
// registers r4-r10 its callers may read left as the original's tail leaves them. Declines too when N is 0 or above 2^31
// (the original's trap and divide forms). Exact; flagged inexact (it calls guest code: it declines under the guard).
// ---------------------------------------------------------------------------------------------------------------------
namespace n_822E98B0 {
template <class C, class S>
inline bool Native(PPCContext& __restrict ctx, uint8_t* base, S& s) {
  using namespace blur_audio;
  if (!On(kN822E98B0) || !Runs<S>()) [[unlikely]] return false;
  const uint32_t p = ctx.r3.u32, row = ctx.r4.u32, sp0 = ctx.r1.u32, frame = sp0 - 368u;
  if (!detail::Straight(p, 272) || !detail::Straight(frame + 80, 300)) [[unlikely]] return false;
  if (LdB(base, p + 10) == 0 || LdB(base, 0x82C9E1E5u) != 0) {
    ctx.r3.u64 = 1;
    return true;
  }
  const uint32_t n = LdW(base, p + 164);
  if (LdB(base, p + 212) != 0 || n == 0 || n > 0x7FFFFFFFu) [[unlikely]] return false;
  const auto StStack = [&](uint32_t a, uint32_t v) {
    v = __builtin_bswap32(v);
    std::memcpy(::banjo::hot::Raw(base, a), &v, 4);
  };
  const auto LdD = [&](uint32_t a) {
    uint64_t v;
    std::memcpy(&v, ::banjo::hot::Raw(base, a), 8);
    return __builtin_bswap64(v);
  };
  const auto Mod = [](uint32_t a, uint32_t m) { return uint32_t(int32_t(a) - (int32_t(a) / int32_t(m)) * int32_t(m)); };
  StStack(sp0 + 28, row);  // stw r4,396(r1): the caller's parameter word
  ctx.r1.u64 = frame;
  constexpr uint32_t kIndex = 0x82C9E1ECu, kFirst = 0x82C9E1F0u;
  uint32_t idx;
  const uint32_t first = LdW(base, kFirst);
  if ((first & 1u) == 0) {
    StW(base, kFirst, first | 1u);
    idx = LdW(base, p + 164) - 256u;
  } else {
    idx = LdW(base, kIndex);
  }
  StW(base, kIndex, Mod(idx + 256u, n));
  ctx.r3.u64 = (LdW(base, p + 264) << 2) + LdW(base, p + 60);
  ctx.r4.u64 = row;
  C::sub_822EA3C0(ctx, base, s);
  StW(base, kIndex, Mod(LdW(base, kIndex) + 256u, LdW(base, p + 164)));
  const int32_t parts = int32_t(LdW(base, p + 124));
  for (int32_t k = 0; k < int32_t(LdW(base, p + 124)); ++k) {
    const uint32_t at = LdW(base, p + 264) + 256u * uint32_t(k);
    const uint32_t m = LdW(base, p + 164);
    ctx.r3.u64 = LdW(base, p + 64) + 1024u * uint32_t(k);
    ctx.r4.u64 = ((at - (at / m) * m) << 2) + LdW(base, p + 60);  // divwu
    ctx.r5.u64 = 1024u * uint32_t(k) + LdW(base, p + 80);
    C::sub_822EA170(ctx, base, s);
  }
  (void)parts;
  ctx.r4.u64 = LdW(base, p + 164);
  ctx.r3.u64 = LdW(base, p + 64);
  C::sub_822D84C8(ctx, base, s);
  {
    const uint64_t a156 = LdD(p + 156);
    uint64_t be = __builtin_bswap64(a156);
    std::memcpy(::banjo::hot::Raw(base, frame + 88), &be, 8);  // std r11,88(r1): the ninth argument
    ctx.r3.u64 = p;
    ctx.r5.u64 = LdW(base, p + 164);
    ctx.r4.u64 = LdW(base, p + 64);
    ctx.r6.u64 = LdD(p + 116);
    ctx.r7.u64 = LdD(p + 124);
    ctx.r8.u64 = LdD(p + 132);
    ctx.r9.u64 = LdD(p + 140);
    ctx.r10.u64 = LdD(p + 148);
    C::sub_822E9198(ctx, base, s);
  }
  ctx.r4.u64 = LdW(base, p + 164);
  ctx.r3.u64 = LdW(base, p + 64);
  C::sub_822D8918(ctx, base, s);
  const double f31 = double(Bits(LdW(base, 0x8202E764u)));
  for (int32_t k = 0; k < int32_t(LdW(base, p + 124)); ++k) {
    const uint32_t w = LdW(base, p + 64) + 1024u * uint32_t(k);
    ctx.r4.u64 = w;
    ctx.r5.u64 = 1024u * uint32_t(k) + LdW(base, p + 80);
    ctx.r3.u64 = w;
    C::sub_822EA170(ctx, base, s);
    ctx.r4.u64 = LdW(base, p + 64) + 1024u * uint32_t(k);
    ctx.f1.f64 = REX_FMULS(double(Bits(LdW(base, p + 48))), f31);
    ctx.r3.u64 = ctx.r4.u64;
    C::sub_822E4810(ctx, base, s);
    const uint32_t at = LdW(base, p + 264) + 256u * uint32_t(k);
    const uint32_t m = LdW(base, p + 164);
    ctx.r5.u64 = LdW(base, p + 64) + 1024u * uint32_t(k);
    ctx.r4.u64 = ((at - (at / m) * m) << 2) + LdW(base, p + 68);
    ctx.r3.u64 = ctx.r4.u64;
    C::sub_822E9F20(ctx, base, s);
  }
  ctx.r3.u64 = row;
  ctx.r4.u64 = (LdW(base, p + 264) << 2) + LdW(base, p + 68);
  C::sub_822EA3C0(ctx, base, s);
  {
    const uint32_t z = (LdW(base, p + 264) << 2) + LdW(base, p + 68);
    for (uint32_t b = 0; b < 1024; b += 128) {
      const uint32_t at = (z + b) & ~127u;
      ::banjo::hot::Card(base, at, 128);
      std::memset(::banjo::hot::Raw(base, at), 0, 128);
    }
  }
  // The tail: +264 = (+264 + 256) mod N, the registers as the original leaves them.
  const uint64_t r8 = uint64_t(LdW(base, p + 264)) + 256u;
  const uint32_t r9 = LdW(base, p + 164);
  const int32_t q = int32_t(uint32_t(r8)) / int32_t(r9);
  const uint32_t rot = (uint32_t(r8) << 1) | (uint32_t(r8) >> 31);
  const int64_t r6 = int64_t(q) * int64_t(int32_t(r9));
  const uint64_t r4 = r8 - uint64_t(r6);
  StW(base, p + 264, uint32_t(r4));
  ctx.r10.s64 = 896;
  ctx.r3.s64 = 1;
  ctx.r8.u64 = r8;
  ctx.r9.u64 = r9;
  ctx.r7.s64 = q;
  ctx.r5.u64 = uint64_t(rot) - 1u;
  ctx.r6.s64 = r6;  // mullw as the generated code writes it: the product of the two sign-extended words
  ctx.r4.u64 = r4;
  ctx.r1.u64 = sp0;
  return true;
}
}  // namespace n_822E98B0
