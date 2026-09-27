// CatMode.h — "cat" mode: eyes with ears and random emotions.
// No gyro, microphone or sound — no reaction to the world, just moods on
// their own. A separate file because the eye engine (eyes/) defines a `tft`
// macro that clashes with Display.cpp.
#pragma once
#include <TFT_eSPI.h>

// Face band: ears start at y≈55, the largest eye shape with offset ends at y≈200.
// Only this band is drawn and pushed — 200 rows instead of 280.
constexpr int CAT_Y0 = 40, CAT_Y1 = 240;

namespace CatMode {
    void enter(TFT_eSPI* surface, void (*push)());   // push — frame is ready, send it to the screen
    void tick();
}
