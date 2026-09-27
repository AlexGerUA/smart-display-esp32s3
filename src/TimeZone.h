// TimeZone.h — time zone and SNTP.
//
// Time is kept by the core's SNTP client (background, resyncs by itself). The
// zone is a POSIX string for newlib: from the IANA zone name reported by
// ip-api (exact DST transitions), otherwise from its offset, otherwise the
// fallback zone from settings.
#pragma once
#include <Arduino.h>

namespace TimeZone {
    void begin();    // after the network is up: SNTP + initial zone
    void update();   // in loop(): apply new settings or geolocation
}
