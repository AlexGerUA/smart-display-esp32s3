// Smart Display — ESP32-S3 Super Mini + ST7789 240x280. Wiring: docs/wiring.png.
//
// Tasks:
//   loop (core 1, 16 KB stack) — display, web, button, time zone;
//   net  (core 0, 16 KB)       — weather, icon, crypto, geolocation;
//   ping (core 0, 8 KB)        — single task, active only in PING mode.
// Shared data = snapshots behind a mutex + version counters; no globals
// written by two tasks.
#include <Arduino.h>
#include <WiFi.h>
#include "Config.h"
#include "Diag.h"
#include "Settings.h"
#include "NetService.h"
#include "TimeZone.h"
#include "Display.h"
#include "Controls.h"
#include "WebUI.h"

// A roomier loop stack for HTTP handlers and rendering
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

static bool running = false;           // false — setup access point is up
static uint32_t seenSettings = 0;
static uint8_t appliedMode = 0xFF;

static void startRunning() {
    running = true;
    net.setFetching(true);
    appliedMode = settings.get().mode;
    display.setMode(appliedMode);
}

// ------------------------------------------------------------------ setup

static void connectProgress(uint8_t, uint32_t elapsedMs) { display.showConnecting(elapsedMs); }

void setup() {
    Diag::begin();                                        // first of all — the reset reason
    Serial.begin(115200);
    uint32_t t0 = millis();
    while (!Serial && millis() - t0 < 1500) delay(10);   // USB CDC — don't wait forever
    Serial.printf("\n=== Smart Display v%s — %s ===\n[Diag] %s\n", PROJECT_VERSION,
                  ESP.getChipModel(), Diag::summary().c_str());

    settings.begin();
    display.begin();          // frame buffer before Wi-Fi, while internal RAM is free
    controls.begin();
    net.begin();

    if (net.connectSaved(connectProgress)) {
        web.begin();
    } else {
        net.startAP();
        web.begin();
        web.startCaptivePortal();
        display.showAP(AP_SSID, WiFi.softAPIP().toString());
    }
    TimeZone::begin();
    net.startTask();
    seenSettings = settings.version();

    if (!net.isAP()) startRunning();
    Serial.printf("[Diag] %s\n", Diag::summary().c_str());
}

// ------------------------------------------------------------------ loop

void loop() {
    Diag::loop();
    settings.loop();
    web.loop();

    if (!running) {
        // The station connected while the access point was up — start running
        if (WiFi.status() == WL_CONNECTED) {
            web.stopCaptivePortal();
            net.leaveAP();
            startRunning();
        }
        delay(5);
        return;
    }

    controls.loop(true);
    TimeZone::update();
    if (settings.version() != seenSettings) {      // mode changed by the button or the web
        seenSettings = settings.version();
        uint8_t m = settings.get().mode;
        if (m != appliedMode) {
            appliedMode = m;
            display.setMode(m);
        }
    }

    display.tick();
    delay(1);
}
