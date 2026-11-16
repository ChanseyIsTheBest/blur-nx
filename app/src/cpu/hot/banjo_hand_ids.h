// blur-nx (2026-11-11): the hand-written natives' ids, alone: cpu/hot/blur_hand_keys.cpp needs only these, and
// banjo_hand_natives.h's templates need the generated pch's macros (REX_RAW_ADDR, REX_CARD, ...), which that file does
// not include (GCC rejects a template body naming them: -Wtemplate-body).
#pragma once

#include <cstdint>

namespace banjo::hand {

// The natives' own keys (banjo_hot_guest.cpp, cpu/hot/blur_hand_keys.cpp), each read once: 0 not read yet, 1 off, 2 on.
// blur-nx (2026-11-10): banjo-nx's memcpy native only (rr6-nx's list, its own three natives taken out).
// blur-nx (2026-11-11, the user's request from the console race profile of the rebased build): Blur's 20 hottest game
// functions, each with its own key (blur_hand_<address>). Job workers 3 and 4 (and an XThread):
// blur_hand_natives_jq34.h; job workers 0-2: blur_hand_natives_jq012.h. A key whose function has no line in hand.txt
// does nothing.
enum : uint32_t {
  kMemcpy = 0,
  kN822EA500, kN8230AD50, kN822EA3C0, kN822EE628, kN822E4698,
  kN822E17C8, kN82953D90, kN822E4CE0, kN822DA210, kN822E53C8,
  kN8251E6F0, kN8251D238, kN824F35D8, kN8250A7E8, kN82621458,
  kN829D2E80, kN82521E68, kN82525FA8, kN82954478, kN825087A0,
  // 2026-11-11, batches 2 and 3 (the user: 30 from the next 60 hot functions; 14 written):
  kN82954F28, kN822E48F8, kN82520D78, kN8230CBB8, kN82244170,
  kN822E9F20, kN82500AC0, kN8230A4D8, kN822D92D8, kN822D9040,
  kN822D79A8, kN82432918, kN8251E438, kN82526568,
  // 2026-11-16, the 15 ms batch (the user: natives for the top 7 audio functions): blur_hand_natives_audio.h.
  kN8230B778, kN822E29A8, kN822D7DF0, kN82953880, kN82954218, kN822E98B0, kN822D2378, kN822BEDD0,
  kNatives
};

}  // namespace banjo::hand
