#include "CatMode.h"
#include "eyes/Face.h"

static Face* face = nullptr;
static void (*pushFn)() = nullptr;
static TFT_eSPI* surf = nullptr;
static uint32_t lastFrame = 0;
static uint32_t nextChange = 0;

// Weight = how often an emotion comes up. Calm ones often, anger rarely.
static const struct { eEmotions e; float w; } WEIGHTS[] = {
    {Normal, 3.0f}, {Happy, 1.2f}, {Glee, 0.7f}, {Sleepy, 0.6f}, {Awe, 0.6f},
    {Surprised, 0.5f}, {Focused, 0.5f}, {Skeptic, 0.4f}, {Suspicious, 0.4f},
    {Squint, 0.3f}, {Unimpressed, 0.3f}, {Worried, 0.3f}, {Sad, 0.3f},
    {Annoyed, 0.2f}, {Scared, 0.15f}, {Frustrated, 0.15f}, {Angry, 0.1f}, {Furious, 0.05f},
};

void CatMode::enter(TFT_eSPI* surface, void (*push)()) {
    pushFn = push;
    surf = surface;
    if (!face) {
        // Eye size 60 suits the 240x280 display
        face = new Face(surface, TFT_WIDTH, TFT_HEIGHT, 60);
        face->Behavior.Clear();
        for (auto& w : WEIGHTS) face->Behavior.SetEmotion(w.e, w.w);
        face->RandomBehavior = false;   // mood changes are timed here
        face->RandomLook = true;
        face->RandomBlink = true;
        face->OnPush = [] { if (pushFn) pushFn(); };
        face->DrawTop = CAT_Y0;
        face->DrawBottom = CAT_Y1;
    }
    face->Behavior.GoToEmotion(Normal);
    face->CurrentEmotion = Normal;
    nextChange = millis() + 3000;
    lastFrame = 0;
}

void CatMode::tick() {
    if (!face) return;
    uint32_t now = millis();
    if (now - lastFrame < 33) return;   // ~30 fps
    lastFrame = now;

    if ((int32_t)(now - nextChange) >= 0) {
        eEmotions e = face->Behavior.GetRandomEmotion();
        if (e != face->Behavior.CurrentEmotion) face->Behavior.GoToEmotion(e);
        face->CurrentEmotion = e;   // ear shape
        // A calm look lasts longer, vivid emotions are brief
        nextChange = now + (e == Normal ? random(5000, 12000) : random(2500, 6000));
    }
    // Average frame time (draw + push), logged every 10 s
    static uint32_t sumUs = 0, frames = 0, lastLog = 0;
    uint32_t t0 = micros();
    // Clip to the face band: the very first frames after the engine is created
    // can draw the eyes huge, and anything outside the band would stay in the
    // frame buffer (only the band is ever cleared and pushed).
    surf->setViewport(0, CAT_Y0, TFT_WIDTH, CAT_Y1 - CAT_Y0, false);
    face->Update();
    surf->resetViewport();
    sumUs += micros() - t0;
    frames++;
    if (now - lastLog >= 10000) {
        lastLog = now;
        Serial.printf("[Cat] frame %.1f ms (%u frames)\n", sumUs / 1000.0f / frames, frames);
        sumUs = frames = 0;
    }
}
