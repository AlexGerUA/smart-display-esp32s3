// Diag.h — reboot diagnostics that survive a restart.
//
// Boot counter and the previous run's uptime live in RTC memory (RTC_NOINIT):
// it survives software reset, panic, watchdog and brownout — only a full
// power loss clears it. The reset reason (esp_reset_reason) and these numbers
// are printed to Serial regularly, so any serial connection catches them even
// if the boot log has already scrolled past.
#pragma once
#include <Arduino.h>

namespace Diag {
    void begin();                 // at the very start of setup()
    void loop();                  // every loop(): uptime counter + Serial heartbeat
    const char* resetName();      // human-readable
    int  resetCode();
    uint32_t bootCount();
    uint32_t prevUptimeSec();     // how long the previous run lasted
    String summary();             // one line for Serial and the web page
}
