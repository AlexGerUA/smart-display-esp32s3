// Controls.h — touch button and gestures:
//
//   mode | short tap (< 0.5 s) | next mode
//   mode | hold 2 s            | open the menu
//   menu | short tap           | next item
//   menu | hold 1 s            | select (the bar turns white near the end)
//
// Holds fire at the threshold — no need to release. The progress bar at the
// bottom appears after 0.3 s. The menu closes itself after 15 s of silence.
#pragma once
#include <Arduino.h>

class Controls {
public:
    void begin();
    void loop(bool enabled);   // enabled — only while running
    void openMenu(int sel);    // open the menu externally (web, for screenshots)
private:
    bool readTouch();
    void applyMode(uint8_t m);

    float    _base = 0;        // capacitive mode: untouched baseline
    bool     _touched = false;
    uint32_t _lastPoll = 0;

    bool     _down = false;
    bool     _holdFired = false;
    uint32_t _downAt = 0;
    int      _sel = 0;
    uint32_t _lastActivity = 0;
};

extern Controls controls;
