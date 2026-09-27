// WebUI.h — settings web interface (dashboard, modes, Wi-Fi, crypto, system,
// OTA) plus the captive portal in access point mode.
//
// Crypto, weather key, mode and time zone changes apply instantly without a
// reboot; where a reboot is needed (Wi-Fi, resets, OTA) it is scheduled
// instead of blocking the handler with delay().
#pragma once
#include <Arduino.h>

class WebUI {
public:
    void begin();
    void startCaptivePortal();
    void stopCaptivePortal();
    void loop();                        // DNS + HTTP + scheduled restart
};

extern WebUI web;
