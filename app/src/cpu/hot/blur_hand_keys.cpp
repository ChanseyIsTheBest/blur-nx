// blur-nx (2026-11-11, the user's request from the console race profile of the rebased build, 13 fps with all three
// cores full): the keys of the hand-written natives of Blur's 20 hottest game functions
// (cpu/hot/blur_hand_natives_jq34.h, blur_hand_natives_jq012.h), each in front of its machine copy in every hook that
// contains it. Off: the machine copy runs, as before. A key whose function has no line in hot/hand.txt does nothing.
// The share is of the race's CPU (rex_profile of 2026-11-11 15:18, windows 17-21).
//
// Here and not in cpu/banjo_hot_guest.cpp: a source outside cpu/hot that names a game function's address makes
// tools/localize_native.py treat the function as hooked (no machine copy), and these natives stand in front of copies.

#include <cstdint>

#include <rex/cvar.h>

#include "cpu/hot/banjo_hand_ids.h"  // the ids alone: banjo_hand_natives.h needs the generated pch

#define BLUR_HAND_KEY(ADDR, WHAT)                                                                                   \
  REXCVAR_DEFINE_BOOL(blur_hand_##ADDR, true, "Blur/Enhancements",                                                 \
                      "Hand-written native of sub_" #ADDR " (" WHAT "), in front of its machine copy in every hook " \
                      "that contains it; checked by those hooks' guards. Off: the machine copy")                    \
      .lifecycle(rex::cvar::Lifecycle::kInitOnly);
BLUR_HAND_KEY(822EA500, "job workers 3 and 4, 7.6 % of a core in the race")
BLUR_HAND_KEY(8230AD50, "job workers 3 and 4, 7.5 % of a core in the race")
BLUR_HAND_KEY(822EA3C0, "job workers 3 and 4, 5.5 % of a core in the race")
BLUR_HAND_KEY(822EE628, "an XThread, 4.8 % of a core in the race")
BLUR_HAND_KEY(822E4698, "job workers 3 and 4, 4.4 % of a core in the race")
BLUR_HAND_KEY(822E17C8, "job workers 3 and 4, 3.6 % of a core in the race")
BLUR_HAND_KEY(82953D90, "job workers 3, 4 and 0, 3.6 % of a core in the race")
BLUR_HAND_KEY(822E4CE0, "job workers 4 and 3, 3.0 % of a core in the race")
BLUR_HAND_KEY(822DA210, "job workers 3 and 4, 2.8 % of a core in the race")
BLUR_HAND_KEY(822E53C8, "job workers 3 and 4, 2.6 % of a core in the race")
BLUR_HAND_KEY(8251E6F0, "job workers 0, 1 and 4, 4.9 % of a core in the race")
BLUR_HAND_KEY(8251D238, "job workers 0-2, 3.8 % of a core in the race")
BLUR_HAND_KEY(824F35D8, "job workers 4, 0 and 1, 3.0 % of a core in the race")
BLUR_HAND_KEY(8250A7E8, "job workers 2, 4 and 0, 3.0 % of a core in the race")
BLUR_HAND_KEY(82621458, "job workers 4, 1 and 0, 2.8 % of a core in the race")
BLUR_HAND_KEY(829D2E80, "job workers 1, 2 and 4, 2.6 % of a core in the race")
BLUR_HAND_KEY(82521E68, "job workers 0, 1 and 4, 2.5 % of a core in the race")
BLUR_HAND_KEY(82525FA8, "job workers 0, 2 and 4, 2.5 % of a core in the race")
BLUR_HAND_KEY(82954478, "job workers 4, 0 and 1, 2.3 % of a core in the race")
BLUR_HAND_KEY(825087A0, "job workers 4, 2 and 0, 2.1 % of a core in the race")
BLUR_HAND_KEY(82954F28, "batch 2/3 of the console race's hot functions")
BLUR_HAND_KEY(822E48F8, "batch 2/3 of the console race's hot functions")
BLUR_HAND_KEY(82520D78, "batch 2/3 of the console race's hot functions")
BLUR_HAND_KEY(8230CBB8, "batch 2/3 of the console race's hot functions")
BLUR_HAND_KEY(82244170, "batch 2/3 of the console race's hot functions")
BLUR_HAND_KEY(822E9F20, "batch 2/3 of the console race's hot functions")
BLUR_HAND_KEY(82500AC0, "batch 2/3 of the console race's hot functions")
BLUR_HAND_KEY(8230A4D8, "batch 2/3 of the console race's hot functions")
BLUR_HAND_KEY(822D92D8, "batch 2/3 of the console race's hot functions")
BLUR_HAND_KEY(822D9040, "batch 2/3 of the console race's hot functions")
BLUR_HAND_KEY(822D79A8, "batch 2/3 of the console race's hot functions")
BLUR_HAND_KEY(82432918, "batch 2/3 of the console race's hot functions")
BLUR_HAND_KEY(8251E438, "batch 2/3 of the console race's hot functions")
BLUR_HAND_KEY(82526568, "batch 2/3 of the console race's hot functions")
BLUR_HAND_KEY(8230B778, "the 15 ms batch: audio (inexact allowed)")
BLUR_HAND_KEY(822E29A8, "the 15 ms batch: the resampler's recursive filter (exact)")
BLUR_HAND_KEY(822D7DF0, "the 15 ms batch: the resampler's stereo converter (exact)")
BLUR_HAND_KEY(822E98B0, "the 15 ms batch: audio (inexact allowed)")
BLUR_HAND_KEY(822D2378, "the 15 ms batch: audio (inexact allowed)")
BLUR_HAND_KEY(822BEDD0, "the 15 ms batch: audio (inexact allowed)")
#undef BLUR_HAND_KEY

namespace banjo::hand {

namespace {
#define BLUR_HAND_NAME(A) "sub_" #A
constexpr const char* kBlurNames[kNatives] = {
    "memcpy",
    BLUR_HAND_NAME(822EA500), BLUR_HAND_NAME(8230AD50), BLUR_HAND_NAME(822EA3C0), BLUR_HAND_NAME(822EE628),
    BLUR_HAND_NAME(822E4698), BLUR_HAND_NAME(822E17C8), BLUR_HAND_NAME(82953D90), BLUR_HAND_NAME(822E4CE0),
    BLUR_HAND_NAME(822DA210), BLUR_HAND_NAME(822E53C8), BLUR_HAND_NAME(8251E6F0), BLUR_HAND_NAME(8251D238),
    BLUR_HAND_NAME(824F35D8), BLUR_HAND_NAME(8250A7E8), BLUR_HAND_NAME(82621458), BLUR_HAND_NAME(829D2E80),
    BLUR_HAND_NAME(82521E68), BLUR_HAND_NAME(82525FA8), BLUR_HAND_NAME(82954478), BLUR_HAND_NAME(825087A0),
    BLUR_HAND_NAME(82954F28), BLUR_HAND_NAME(822E48F8), BLUR_HAND_NAME(82520D78), BLUR_HAND_NAME(8230CBB8), BLUR_HAND_NAME(82244170), BLUR_HAND_NAME(822E9F20), BLUR_HAND_NAME(82500AC0), BLUR_HAND_NAME(8230A4D8), BLUR_HAND_NAME(822D92D8), BLUR_HAND_NAME(822D9040), BLUR_HAND_NAME(822D79A8), BLUR_HAND_NAME(82432918), BLUR_HAND_NAME(8251E438), BLUR_HAND_NAME(82526568), BLUR_HAND_NAME(8230B778), BLUR_HAND_NAME(822E29A8), BLUR_HAND_NAME(822D7DF0), BLUR_HAND_NAME(822E98B0), BLUR_HAND_NAME(822D2378), BLUR_HAND_NAME(822BEDD0)};
#undef BLUR_HAND_NAME
#define BLUR_HAND_KEYNAME(A) "blur_hand_" #A
constexpr const char* kBlurKeys[kNatives] = {
    "banjo_hand_memcpy",
    BLUR_HAND_KEYNAME(822EA500), BLUR_HAND_KEYNAME(8230AD50), BLUR_HAND_KEYNAME(822EA3C0), BLUR_HAND_KEYNAME(822EE628),
    BLUR_HAND_KEYNAME(822E4698), BLUR_HAND_KEYNAME(822E17C8), BLUR_HAND_KEYNAME(82953D90), BLUR_HAND_KEYNAME(822E4CE0),
    BLUR_HAND_KEYNAME(822DA210), BLUR_HAND_KEYNAME(822E53C8), BLUR_HAND_KEYNAME(8251E6F0), BLUR_HAND_KEYNAME(8251D238),
    BLUR_HAND_KEYNAME(824F35D8), BLUR_HAND_KEYNAME(8250A7E8), BLUR_HAND_KEYNAME(82621458), BLUR_HAND_KEYNAME(829D2E80),
    BLUR_HAND_KEYNAME(82521E68), BLUR_HAND_KEYNAME(82525FA8), BLUR_HAND_KEYNAME(82954478), BLUR_HAND_KEYNAME(825087A0),
    BLUR_HAND_KEYNAME(82954F28), BLUR_HAND_KEYNAME(822E48F8), BLUR_HAND_KEYNAME(82520D78), BLUR_HAND_KEYNAME(8230CBB8), BLUR_HAND_KEYNAME(82244170), BLUR_HAND_KEYNAME(822E9F20), BLUR_HAND_KEYNAME(82500AC0), BLUR_HAND_KEYNAME(8230A4D8), BLUR_HAND_KEYNAME(822D92D8), BLUR_HAND_KEYNAME(822D9040), BLUR_HAND_KEYNAME(822D79A8), BLUR_HAND_KEYNAME(82432918), BLUR_HAND_KEYNAME(8251E438), BLUR_HAND_KEYNAME(82526568), BLUR_HAND_KEYNAME(8230B778), BLUR_HAND_KEYNAME(822E29A8), BLUR_HAND_KEYNAME(822D7DF0), BLUR_HAND_KEYNAME(822E98B0), BLUR_HAND_KEYNAME(822D2378), BLUR_HAND_KEYNAME(822BEDD0)};
#undef BLUR_HAND_KEYNAME
}  // namespace

const char* BlurName(uint32_t which) { return which < kNatives ? kBlurNames[which] : "?"; }
const char* BlurKey(uint32_t which) { return which < kNatives ? kBlurKeys[which] : "?"; }

bool BlurOwn(uint32_t which) {
  switch (which) {
#define BLUR_HAND_CASE(A) \
  case kN##A:             \
    return bool(REXCVAR_GET(blur_hand_##A));
    BLUR_HAND_CASE(822EA500) BLUR_HAND_CASE(8230AD50) BLUR_HAND_CASE(822EA3C0) BLUR_HAND_CASE(822EE628)
    BLUR_HAND_CASE(822E4698) BLUR_HAND_CASE(822E17C8) BLUR_HAND_CASE(82953D90) BLUR_HAND_CASE(822E4CE0)
    BLUR_HAND_CASE(822DA210) BLUR_HAND_CASE(822E53C8) BLUR_HAND_CASE(8251E6F0) BLUR_HAND_CASE(8251D238)
    BLUR_HAND_CASE(824F35D8) BLUR_HAND_CASE(8250A7E8) BLUR_HAND_CASE(82621458) BLUR_HAND_CASE(829D2E80)
    BLUR_HAND_CASE(82521E68) BLUR_HAND_CASE(82525FA8) BLUR_HAND_CASE(82954478) BLUR_HAND_CASE(825087A0)
    BLUR_HAND_CASE(82954F28) BLUR_HAND_CASE(822E48F8) BLUR_HAND_CASE(82520D78) BLUR_HAND_CASE(8230CBB8) BLUR_HAND_CASE(82244170) BLUR_HAND_CASE(822E9F20) BLUR_HAND_CASE(82500AC0) BLUR_HAND_CASE(8230A4D8) BLUR_HAND_CASE(822D92D8) BLUR_HAND_CASE(822D9040) BLUR_HAND_CASE(822D79A8) BLUR_HAND_CASE(82432918) BLUR_HAND_CASE(8251E438) BLUR_HAND_CASE(82526568) BLUR_HAND_CASE(8230B778) BLUR_HAND_CASE(822E29A8) BLUR_HAND_CASE(822D7DF0) BLUR_HAND_CASE(822E98B0) BLUR_HAND_CASE(822D2378) BLUR_HAND_CASE(822BEDD0)
#undef BLUR_HAND_CASE
    default:
      return false;
  }
}

}  // namespace banjo::hand
