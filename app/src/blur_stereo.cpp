// blur-nx (2026-11-13, the user: "force the game to stereo do as many additional modifications you need"): Blur's own
// audio graph mixing two channels instead of 5.1. The patches are statements of the generated code (tools/stereo_mix.py:
// the mixer and insert widths, and in the node loop sub_8230AD50 the input counts, the gain fold and the row resets),
// each behind blur_stereo_mix: off, the original statements' paths run. This file holds the key, the flag the
// statements read (latched before the game starts: RenutApp::OnPostSetup) and the fold.
//
// The fold: a mixer narrowed to two output rows keeps its gain matrix for five or six speakers (the panners write it),
// and each row reaches the two speakers as the port's own 5.1 fold would carry those speakers (rex::audio::StereoFold:
// center and surround at -3 dB, LFE dropped, the same scale applied after the mix): left = front left + center x c +
// LFE x l + surround left x s, right likewise. The mix is linear in its gains, so folding the gains equals folding
// the mix, and the port's fold of the then-silent channels 2-5 keeps the level the 5.1 mix had.

#include <rex/audio/downmix.h>
#include <rex/cvar.h>
#include <rex/logging.h>

#include <bit>
#include <cstdint>

REXCVAR_DEFINE_BOOL(audio_stereo_mix, false, "Audio",
                    "Blur's audio graph mixes two channels instead of 5.1 (its speaker mixers, patch inputs and "
                    "outputs and surround effects built with two channels, each mixer's speaker gains folded to left "
                    "and right as the port's 5.1 fold would): less mixing work; the surround effects then run on the "
                    "folded pair (not identical to the 5.1 mix folded afterwards); read before the game starts")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

extern "C" {
bool blur_stereo_mix = false;  // the generated statements' flag (tools/stereo_mix.py's REX_BLUR_STEREO_*)
}

namespace {
float g_center = 0.70710678f;
float g_lfe = 0.0f;
float g_surround = 0.70710678f;
}  // namespace

// Output 'out' (0 left, 1 right) of a gain row of 'width' speakers: 6 FL FR C LFE RL RR, 5 FL FR C RL RR, 4 FL FR RL
// RR, 3 FL FR C. The result as the float's bits (the generated code's register form).
extern "C" uint32_t BlurStereoFoldBits(float g0, float g1, float g2, float g3, float g4, float g5, uint32_t out,
                                       uint32_t width) {
  float v;
  switch (width) {
    case 6:
      v = (out ? g1 + g_surround * g5 : g0 + g_surround * g4) + g_center * g2 + g_lfe * g3;
      break;
    case 5:
      v = (out ? g1 + g_surround * g4 : g0 + g_surround * g3) + g_center * g2;
      break;
    case 4:
      v = out ? g1 + g_surround * g3 : g0 + g_surround * g2;
      break;
    case 3:
      v = (out ? g1 : g0) + g_center * g2;
      break;
    default:
      v = out ? g1 : g0;
      break;
  }
  return std::bit_cast<uint32_t>(v);
}

namespace blur::stereo {

void Latch() {
  blur_stereo_mix = REXCVAR_GET(audio_stereo_mix);
  const rex::audio::StereoFold fold = rex::audio::GetStereoFold();
  g_center = fold.center;
  g_lfe = fold.lfe;
  g_surround = fold.surround;
  if (blur_stereo_mix) {
    REXLOG_INFO("[audio] stereo mix (audio_stereo_mix): Blur's graph mixes two channels; speaker gains folded with "
                "center {:.3f}, LFE {:.3f}, surround {:.3f}",
                g_center, g_lfe, g_surround);
  }
}

}  // namespace blur::stereo
