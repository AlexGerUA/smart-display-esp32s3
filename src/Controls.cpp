#include "Controls.h"
#include "Config.h"
#include "Settings.h"
#include "Display.h"

Controls controls;

void Controls::begin() {
    if (TOUCH_IS_TTP223) {
        // The TTP223 drives its output (HIGH = touch); pulldown covers a loose wire.
        // The hardware touch sensor is NOT enabled.
        pinMode(TOUCH_PIN, INPUT_PULLDOWN);
        Serial.printf("[Touch] GPIO%u: TTP223 module\n", TOUCH_PIN);
        return;
    }
    // Bare wire/pad: baseline = untouched average; on the S3 the reading RISES on touch
    uint32_t sum = 0;
    for (int i = 0; i < 16; i++) {
        sum += touchRead(TOUCH_PIN);
        delay(5);
    }
    _base = sum / 16.0f;
    Serial.printf("[Touch] GPIO%u: capacitive, baseline %.0f, threshold +%.0f%%\n", TOUCH_PIN, _base, TOUCH_RATIO * 100);
}

bool Controls::readTouch() {
    if (TOUCH_IS_TTP223) return digitalRead(TOUCH_PIN) == HIGH;

    // Hysteresis: press above baseline by TOUCH_RATIO, release at half of it.
    // The baseline follows slow drift, only while untouched.
    float v = touchRead(TOUCH_PIN);
    if (!_touched && v > _base * (1 + TOUCH_RATIO)) _touched = true;
    else if (_touched && v < _base * (1 + TOUCH_RATIO / 2)) _touched = false;
    if (!_touched) _base += (v - _base) * 0.01f;
    return _touched;
}

void Controls::applyMode(uint8_t m) {
    display.hideMenu();
    if (settings.get().mode == m) {
        display.setMode(m);            // same mode — just remove the menu
    } else {
        settings.setModeDeferred(m);   // main.cpp switches on the version bump; flash write after a pause
    }
}

void Controls::openMenu(int sel) {
    _sel = constrain(sel, 0, MODE_COUNT - 1);
    _lastActivity = millis();   // the menu timeout counts from now
    display.showMenu(_sel);
}

void Controls::loop(bool enabled) {
    uint32_t now = millis();
    if (now - _lastPoll < 20) return;   // poll 50 times per second
    _lastPoll = now;

    bool t = readTouch();
    if (!enabled) {
        _down = false;
        return;
    }
    bool menu = display.menuOpen();

    if (t && !_down) {
        _down = true;
        _holdFired = false;
        _downAt = now;
    }

    if (t && _down && !_holdFired) {
        uint32_t held = now - _downAt;
        uint32_t need = menu ? SELECT_HOLD_MS : HOLD_MENU_MS;
        if (held >= BAR_SHOW_MS) display.setHoldBar((float)held / need, menu && held * 4 >= need * 3);
        if (held >= need) {
            _holdFired = true;
            display.setHoldBar(-1, false);
            if (menu) {
                applyMode(_sel);
            } else {
                _sel = settings.get().mode;
                display.showMenu(_sel);
            }
            _lastActivity = now;
        }
    }

    if (!t && _down) {
        _down = false;
        display.setHoldBar(-1, false);
        if (!_holdFired && now - _downAt < TAP_MAX_MS) {
            if (menu) {
                _sel = (_sel + 1) % MODE_COUNT;
                display.showMenu(_sel);
            } else {
                applyMode((settings.get().mode + 1) % MODE_COUNT);
            }
        }
        _lastActivity = now;
    }

    if (display.menuOpen() && !_down && now - _lastActivity > MENU_TIMEOUT_MS) {
        applyMode(settings.get().mode);   // silence — close without changes
    }
}
