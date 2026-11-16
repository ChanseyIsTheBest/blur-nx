// banjo-nx - hand-written native versions of hot guest functions (CPU enhancements banjo_hand_*).
//
// The localized copies of tools/localize_native.py keep a function's own statements; these replace the statements:
// the XDK C runtime's memcpy and memset and Direct3D's two shader float-constant writers, written from what the
// routines do. They are sa2-nx's natives (app/src/cpu/hot/n_827F05C0.h, n_827EFEA0.h, n_d3d_constants.h, GPL-3.0;
// the memcpy came there from masseffect-nx's n_82AC4AF0.h): tools/crt_signatures.py of sa2-nx finds the same three C
// runtime routines in Banjo's image (unique matches), and the two constant writers are the same instructions with
// the same two offsets. Banjo's own additions are the two writers of Direct3D's state flush (namespace flush below).
// What is different here is how they are used and checked:
//
//   * Each is a template on the store policy of banjo_hot_common.h, so the guard's logged form records what it is
//     about to write like any generated copy (no hand-written list of written ranges).
//   * tools/localize_native.py puts them IN FRONT of the generated copy of the same function wherever that copy is
//     used: as a hook of its own, and inside every other function's copy that contains it (app/src/cpu/hot/hand.txt).
//     Inside a caller's copy the call is inlined: no hook, no hand-over of every register (on the Switch that
//     hand-over is what made copies with real calls slower than their originals).
//   * A native returns false to DECLINE a call it does not cover (nothing written yet): the generated copy of the
//     original then runs, in the same place.
//   * The registers the original leaves behind as scratch (hand.txt gives them per function; tools/localize_native.py
//     takes them from the original's own statements) are NOT reproduced: the natives write no register. Those
//     registers are left out of the guard's register compare for every hook whose copy contains the native. Sound
//     because they are caller-saved scratch of assembly routines and of a callee the callers reach through the
//     normal calling convention: a caller that read one after the call would get a different value, would act on
//     it, and the guard would see that in everything else it compares.
//
// ARM (the A57 is where this has to pay, and sa2-nx's first native Direct3D layer lost there for reasons no PC run
// showed: docs/api-renderer.md of sa2-nx, section 6): nothing here is per thread (no thread_local: a function call on
// the Switch), nothing copies a context, no loop keeps a count where a store could alias it, nothing is byte-swapped
// word by word (the copies are guest order to guest order), and sizes up to 128 bytes (memcpy; 64 for memset) are
// moved with overlapping 16/8/4/2-byte loads and stores without a call into the C library. In a PC run of title,
// menus, intro and gameplay 62 % of the 21.9 million memcpy calls were under 64 bytes and 21 % were 64 to 127; 46 %
// of the memsets were 4 to 7 bytes; 98 % of the 26 million constant uploads were 1 to 7 vectors.
//
// Guest function addresses may be named in this folder only (tools/direct_calls.py and tools/localize_native.py
// read every other source's addresses as hooked functions; these four are hooked, by app/src/cpu/hot/localized.txt).
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>

#include <rex/ppc/context.h>

#if defined(__aarch64__)
#include <arm_neon.h>
#endif

#include "cpu/banjo_hot_common.h"
#include "cpu/hot/blur_block_copy.h"  // blur-nx (2026-11-11): the natives' block copies (VS-07)
#include "cpu/hot/blur_gather.h"  // blur-nx (2026-11-11): the resampler natives' table pairs
#include "cpu/hot/blur_swap.h"  // blur-nx (2026-11-12): word swaps for lane-wise natives (CPU batch 3)
// blur-nx (2026-11-12; port 96): the natives' fmadds / fnmsubs take the generated code's form (tools/fma_single.py
// writes it into the pch; a file without the pch gets the same definition here).
#ifndef REX_FMADDS
#include <rex/ppc/fma_single.h>
#define REX_FMADDS(a, b, c) rex::ppc::FmaddsSingle((a), (b), (c))
#endif
#include <rex/ppc/diet_ops.h>  // katamari-nx: rex::ppc::SingleToDoublePure

// katamari-nx: the in-run A/B's target in its off half (renut_engine/gpu/banjo_renut_tuning.cpp).
#include <atomic>
namespace banjo::renut_gpu {
extern std::atomic<int> g_ab_off;
}  // namespace banjo::renut_gpu

namespace banjo::hand {

// The natives' ids and their keys: cpu/hot/banjo_hand_ids.h (blur-nx, 2026-11-11: its own header, so the keys' file
// needs none of this one's templates). katamari-nx: kMatCopy / kMatMul / kMatTranslation were Beautiful Katamari's.
}  // namespace banjo::hand
#include "cpu/hot/banjo_hand_ids.h"
namespace banjo::hand {
extern uint8_t g_state[kNatives];
uint8_t Init(uint32_t which);
[[gnu::always_inline]] inline bool On(uint32_t which) {
  const uint8_t state = g_state[which];
  if (state == 2) [[likely]] return true;
  return state == 0 && Init(which) == 2;
}

namespace detail {

// 16 bytes through a register (any alignment; one NEON load or store on the A57).
struct B16 {
  uint8_t b[16];
};
[[gnu::always_inline]] inline B16 L16(const uint8_t* p) {
  B16 v;
  std::memcpy(v.b, p, 16);
  return v;
}
[[gnu::always_inline]] inline void S16(uint8_t* p, const B16& v) { std::memcpy(p, v.b, 16); }

// 1 <= n <= 64; all loads before all stores (memmove semantics for dst <= src overlap).
[[gnu::always_inline]] inline void CopySmall(uint8_t* d, const uint8_t* s, uint32_t n) {
  if (n >= 16) {
    if (n > 32) {
      const B16 a = L16(s), b = L16(s + 16), c = L16(s + n - 32), e = L16(s + n - 16);
      S16(d, a);
      S16(d + 16, b);
      S16(d + n - 32, c);
      S16(d + n - 16, e);
    } else {
      const B16 a = L16(s), e = L16(s + n - 16);
      S16(d, a);
      S16(d + n - 16, e);
    }
  } else if (n >= 8) {
    uint64_t a, e;
    std::memcpy(&a, s, 8);
    std::memcpy(&e, s + n - 8, 8);
    std::memcpy(d, &a, 8);
    std::memcpy(d + n - 8, &e, 8);
  } else if (n >= 4) {
    uint32_t a, e;
    std::memcpy(&a, s, 4);
    std::memcpy(&e, s + n - 4, 4);
    std::memcpy(d, &a, 4);
    std::memcpy(d + n - 4, &e, 4);
  } else if (n >= 2) {
    uint16_t a, e;
    std::memcpy(&a, s, 2);
    std::memcpy(&e, s + n - 2, 2);
    std::memcpy(d, &a, 2);
    std::memcpy(d + n - 2, &e, 2);
  } else {
    *d = *s;
  }
}

// 64 < n <= 128; all loads before all stores. (Banjo's calls: a fifth are 64 to 127 bytes.)
[[gnu::always_inline]] inline void CopyMedium(uint8_t* d, const uint8_t* s, uint32_t n) {
  const B16 a = L16(s), b = L16(s + 16), c = L16(s + 32), e = L16(s + 48);
  const B16 f = L16(s + n - 64), g = L16(s + n - 48), h = L16(s + n - 32), i = L16(s + n - 16);
  S16(d, a);
  S16(d + 16, b);
  S16(d + 32, c);
  S16(d + 48, e);
  S16(d + n - 64, f);
  S16(d + n - 48, g);
  S16(d + n - 32, h);
  S16(d + n - 16, i);
}

// 1 <= n <= 64, every byte = v.
[[gnu::always_inline]] inline void FillSmall(uint8_t* d, uint8_t v, uint32_t n) {
  if (n >= 16) {
    B16 x;
    std::memset(x.b, v, 16);
    S16(d, x);
    S16(d + n - 16, x);
    if (n > 32) {
      S16(d + 16, x);
      S16(d + n - 32, x);
    }
  } else {
    const uint64_t x = uint64_t(v) * 0x0101010101010101ull;
    if (n >= 8) {
      std::memcpy(d, &x, 8);
      std::memcpy(d + n - 8, &x, 8);
    } else if (n >= 4) {
      std::memcpy(d, &x, 4);
      std::memcpy(d + n - 4, &x, 4);
    } else if (n >= 2) {
      std::memcpy(d, &x, 2);
      std::memcpy(d + n - 2, &x, 2);
    } else {
      *d = v;
    }
  }
}

// [a, a + n) is one stretch of host memory: it does not wrap around 4 GB and (on hosts with the physical-view
// offset) does not cross its boundary. n >= 1.
[[gnu::always_inline]] inline bool Straight(uint32_t a, uint32_t n) {
  if (uint64_t(a) + n > 0x100000000ull) return false;
  return ::banjo::hot::PhysOff(a) == ::banjo::hot::PhysOff(a + n - 1u);
}

// sasrt-nx (2026-11-11, port 74; its idea 57, sasrt-nx's of 2026-11-08): the four stvewx of one vector at e0, e0 + 4,
// e0 + 8, e0 + 12 (e0 4-aligned), merged by tools/localize_native.py (merge_vector_stores): word j of [e0, e0 + 16) gets
// the vector's lane 3 - (((e0 >> 2) + j) & 3) (the generated code's lane-by-address form); with the host's lane order
// that is the vector rotated by (e0 >> 2) & 3 lanes, then one byte-swapped 16-byte store, and the four card marks of
// the four stores (REX_CARD as this header is read: before banjo_hot_prelude.h's card runs, so they mark at once).
// false: the logged form's run is over (nothing written).
template <class S>
[[gnu::always_inline]] inline bool StoreWordsOfVector(uint8_t* base, S& s, uint32_t e0, const PPCVRegister& v) {
  if (!s.Note(e0, 16u)) return false;
  simde__m128i h = simde_mm_load_si128((const simde__m128i*)v.u8);
  switch ((e0 >> 2) & 3u) {
    case 1:
      h = simde_mm_shuffle_epi32(h, 0x93);  // lanes (3, 0, 1, 2)
      break;
    case 2:
      h = simde_mm_shuffle_epi32(h, 0x4E);  // lanes (2, 3, 0, 1)
      break;
    case 3:
      h = simde_mm_shuffle_epi32(h, 0x39);  // lanes (1, 2, 3, 0)
      break;
    default:
      break;
  }
  simde_mm_storeu_si128((simde__m128i*)REX_RAW_ADDR(e0), REX_BSWAP128(h));
  REX_CARD(e0);
  REX_CARD(e0 + 4u);
  REX_CARD(e0 + 8u);
  REX_CARD(e0 + 12u);
  return true;
}

}  // namespace detail

// ---------------------------------------------------------------------------------------------------------------------
// blur-nx: hand-written natives of Blur's own come only from a console profile, past the gate of
// docs/optimising-functions.md (hand.txt has the evidence of each). banjo-nx's memcpy native above stays, unwired.
// 2026-11-11: the 20 hottest game functions of the console race (the user's request), in two files by thread family.
// The second is included once it has its final name (written as a draft first, so no build meets a half-written one).
// ---------------------------------------------------------------------------------------------------------------------
#include "cpu/hot/blur_hand_natives_jq34.h"
#include "cpu/hot/blur_hand_natives_jq012.h"
// 2026-11-11, batches 2 and 3 (the user: 30 from the next 60 hot functions; 14 written by three agents, reviewed here).
#include "cpu/hot/blur_hand_natives_batch2.h"
#include "cpu/hot/blur_hand_natives_batch3a.h"
#include "cpu/hot/blur_hand_natives_batch3b.h"
#include "cpu/hot/blur_hand_natives_batch3c.h"
// 2026-11-16, the 15 ms batch (the user: natives for the top 7 audio functions, which may cheat a bit): the audio
// graph's natives, inexact in the audio they compute and declining under the guard (the file's comment).
#include "cpu/hot/blur_hand_natives_audio.h"

}  // namespace banjo::hand
