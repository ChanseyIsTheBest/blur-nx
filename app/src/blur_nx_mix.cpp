// blur-nx (2026-11-13, #23 and #24 of docs/cpu-near-exact-50-1113.md; the user chose the near-exact ideas to do): the
// tolerances Blur's audio node loop (sub_8230AD50) gets at two of its kernel choices (tools/blur_mix_nx.py's
// statements, REX_BLUR_NX_*), latched before the game starts (RenutApp::OnPostSetup). 0 is exactly the code before.
//   audio_nx_ramp_eps  (#24): a gain whose target differs from its current value by less than this mixes as the constant
//                      target (the constant kernel) instead of a ramp to it.
//   audio_nx_unity_eps (#23): a constant gain within this of 1.0 mixes as 1.0 (the unity kernel: an add, no multiply).
//   audio_nx_silent_eps (#21): a constant gain under this mixes as 0 (the entry is not mixed, as a zero gain already
//                      is not), and a ramp with both ends under it too.
//   audio_nx_resample_copy (#49): a resampling block that consumes exactly 256 source samples takes the kernels' own
//                      copy (their ratio == 1.0 path): its fraction is 0 at every output, so the interpolation gives
//                      the source samples (apart from a -0, a NaN or a denormal the vector unit flushes).
// Each sample differs by at most eps x |sample| (a 16-bit output step is 2^-15 of full scale).

#include <rex/cvar.h>
#include <rex/logging.h>

#include <algorithm>
#include <cmath>

REXCVAR_DEFINE_DOUBLE(audio_nx_ramp_eps, 0.0, "Audio",
                      "Blur near-exact #24: a mixer gain whose target differs from its current value by less than this "
                      "mixes as the constant target instead of a ramp to it (0: off, as before; at most 0.001 since 2026-11-17)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_DOUBLE(audio_nx_unity_eps, 0.0, "Audio",
                      "Blur near-exact #23: a constant mixer gain within this of 1.0 mixes as 1.0 (the unity kernel) "
                      "(0: off, as before; at most 0.001 since 2026-11-17)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

REXCVAR_DEFINE_DOUBLE(audio_nx_silent_eps, 0.0, "Audio",
                      "Blur near-exact #21: a mixer gain (or a ramp's two ends) under this is not mixed at all, as a "
                      "zero gain is not (0: off, as before; at most 0.001, -60 dB, since 2026-11-17)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
// blur-nx (2026-11-17, the 24-hour run; the user: "do more non-exact audio stuff to save usage"): the voice floor.
REXCVAR_DEFINE_DOUBLE(audio_nx_voice_floor, 0.0, "Audio",
                      "Blur inexact audio: a voice whose start and end gains are both under this takes the game's own "
                      "inaudible path (not rendered, its source advanced), as one under Blur's own 1e-4 (-80 dB) does "
                      "(0: off, as before; 0.001 is -60 dB; at most 0.01)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(audio_nx_skip_silent_rows, false, "Audio",
                    "Blur inexact audio (#197): a mixer child whose input rows are all zero after it ran (a voice under "
                    "the floor, an idle node) is not mixed (only a -0 output would have become +0)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(cpu_nx_exp_memo, false, "CPU",
                    "Blur #43 (exact): the exp native (sub_82954F28) keeps the last 64 arguments per thread with their "
                    "results; a repeated argument (the audio parameter fetch's dB offsets) gives the stored result")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(audio_nx_resample_copy, false, "Audio",
                    "Blur near-exact #49: a resampling block that consumes exactly 256 source samples is copied (the "
                    "kernels' own ratio-1 path) instead of interpolated at fraction 0 (the same samples apart from a -0, "
                    "a NaN or a flushed denormal)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

extern "C" {
float blur_nx_ramp_eps = 0.0f;   // the generated statements' tolerances (tools/blur_mix_nx.py)
float blur_nx_unity_eps = 0.0f;
float blur_nx_silent_eps = 0.0f;
float blur_nx_voice_floor = 0.0f;  // 2026-11-17 (cpu/hot/blur_hand_natives_audio.h, the voice step)
bool blur_nx_skip_silent = false;  // 2026-11-17 (the node loop native, #197)
bool blur_nx_resample_copy = false;
bool blur_nx_exp_memo = false;  // #43 (cpu/hot/blur_hand_natives_batch2.h)
}

namespace blur::nx_mix {

void Latch() {
  constexpr double kMax = 1.0 / 32768.0;
  // 2026-11-17 (the user: "more non-exact audio"): up to 1e-3 (-60 dB: a gain step of 0.009 dB, a ramp's end within
  // 0.1 % of its start); kMax (2^-15) was the near-exact run's bound.
  blur_nx_ramp_eps = float(std::clamp(REXCVAR_GET(audio_nx_ramp_eps), 0.0, 0.001));
  blur_nx_unity_eps = float(std::clamp(REXCVAR_GET(audio_nx_unity_eps), 0.0, 0.001));
  (void)kMax;
  // 2026-11-17: the silent gain may go to -60 dB (the user: "more non-exact audio"); the others stay at 2^-15.
  blur_nx_silent_eps = float(std::clamp(REXCVAR_GET(audio_nx_silent_eps), 0.0, 0.001));
  blur_nx_voice_floor = float(std::clamp(REXCVAR_GET(audio_nx_voice_floor), 0.0, 0.01));
  blur_nx_skip_silent = REXCVAR_GET(audio_nx_skip_silent_rows);
  blur_nx_resample_copy = REXCVAR_GET(audio_nx_resample_copy);
  blur_nx_exp_memo = REXCVAR_GET(cpu_nx_exp_memo);
  if (blur_nx_voice_floor > 0.0f)
    REXLOG_INFO("[audio] inexact: voices under {:g} ({:.1f} dB) take the game's inaudible path", blur_nx_voice_floor,
                20.0 * std::log10(double(blur_nx_voice_floor)));
  if (blur_nx_ramp_eps > 0.0f || blur_nx_unity_eps > 0.0f || blur_nx_silent_eps > 0.0f || blur_nx_resample_copy) {
    REXLOG_INFO("[audio] near-exact mixer: gain ramps under {:g} mixed as constant, gains within {:g} of 1 as unity, "
                "gains under {:g} not mixed, 256-sample resampling blocks copied: {}",
                blur_nx_ramp_eps, blur_nx_unity_eps, blur_nx_silent_eps, blur_nx_resample_copy);
  }
}

}  // namespace blur::nx_mix
