// Display.h — all rendering. Everything is drawn into a 240x280 frame buffer
// in PSRAM; only changed rectangles are pushed to the panel.
//
// A copy of the current mode's clean background (gradient, lines, static
// icons) also lives in PSRAM. Clearing a text field = memcpy from that copy,
// and anti-aliased glyphs blend against the real background under them.
#pragma once
#include <Arduino.h>
#include "Settings.h"

class Display {
public:
    bool begin();                        // false — not enough memory for the frame
    void showConnecting(uint32_t elapsedMs);
    void showAP(const String& ssid, const String& ip);

    void setMode(uint8_t mode);          // full redraw of a mode
    uint8_t mode() const { return _mode; }
    void tick();                         // every loop() while running

    // Mode menu (touch button). While open, the mode is not drawn.
    void showMenu(int selected);
    void hideMenu() { _menu = false; }
    bool menuOpen() const { return _menu; }
    // Hold progress bar at the bottom: p 0..1, p < 0 hides it.
    // white — the hold is about to select.
    void setHoldBar(float p, bool white);

    // Snapshot of the current frame (24-bit BMP) — for checking layouts over the web
    static constexpr size_t BMP_SIZE = 54 + 240 * 3 * 280;
    void writeBmp(void (*sink)(const uint8_t* data, size_t len));

private:
    uint8_t _mode = MODE_NORMAL;
    bool _menu = false;
};

extern Display display;
