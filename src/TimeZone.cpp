#include "TimeZone.h"
#include "Config.h"
#include "Settings.h"
#include "NetService.h"
#include <time.h>

static String applied;
static uint32_t seenSettings = 0, seenGeo = 0;

// Fixed offset. POSIX inverts the sign: UTC+2 -> "UTC-2".
static String posixFixed(int32_t off) {
    char buf[24];
    int a = abs(off), h = a / 3600, m = (a % 3600) / 60;
    char sign = off >= 0 ? '-' : '+';
    if (m) snprintf(buf, sizeof(buf), "UTC%c%d:%02d", sign, h, m);
    else   snprintf(buf, sizeof(buf), "UTC%c%d", sign, h);
    return buf;
}

// Fallback zone; DST follows the EU rule — last Sundays of March and October
// at 01:00 UTC. (The original treated April–September as summer and added an
// hour on top of an offset that already included DST.)
static String posixManual(int h, bool dst) {
    String s = posixFixed(h * 3600);
    if (!dst) return s;
    char rule[48];
    snprintf(rule, sizeof(rule), "DST,M3.5.0/%d,M10.5.0/%d", max(0, 1 + h), max(0, 2 + h));
    return s + rule;
}

static const char* posixForZone(const String& z) {
    static const char* EET_EU = "EET-2EEST,M3.5.0/3,M10.5.0/4";
    static const char* CET_EU = "CET-1CEST,M3.5.0,M10.5.0/3";
    static const struct { const char* zone; const char* posix; } T[] = {
        {"Europe/Kyiv", EET_EU}, {"Europe/Kiev", EET_EU}, {"Europe/Uzhgorod", EET_EU},
        {"Europe/Zaporozhye", EET_EU}, {"Europe/Chisinau", "EET-2EEST,M3.5.0,M10.5.0/3"},
        {"Europe/Bucharest", EET_EU}, {"Europe/Sofia", EET_EU}, {"Europe/Athens", EET_EU},
        {"Europe/Helsinki", EET_EU}, {"Europe/Riga", EET_EU}, {"Europe/Vilnius", EET_EU},
        {"Europe/Tallinn", EET_EU},
        {"Europe/Warsaw", CET_EU}, {"Europe/Berlin", CET_EU}, {"Europe/Prague", CET_EU},
        {"Europe/Bratislava", CET_EU}, {"Europe/Budapest", CET_EU}, {"Europe/Vienna", CET_EU},
        {"Europe/Rome", CET_EU}, {"Europe/Paris", CET_EU}, {"Europe/Madrid", CET_EU},
        {"Europe/Amsterdam", CET_EU}, {"Europe/Brussels", CET_EU}, {"Europe/Zurich", CET_EU},
        {"Europe/Copenhagen", CET_EU}, {"Europe/Stockholm", CET_EU}, {"Europe/Oslo", CET_EU},
        {"Europe/London", "GMT0BST,M3.5.0/1,M10.5.0"}, {"Europe/Dublin", "GMT0IST,M3.5.0/1,M10.5.0"},
        {"Europe/Lisbon", "WET0WEST,M3.5.0/1,M10.5.0"},
    };
    for (auto& t : T) if (z == t.zone) return t.posix;
    return nullptr;
}

// Zones where a zero offset is genuine (other zeros from ip-api are treated as bogus —
// it reports offset=0 for Europe/Kyiv)
static bool zoneIsUtc(const String& z) {
    return z.startsWith("UTC") || z.startsWith("Etc/") || z == "Atlantic/Reykjavik" ||
           z == "Africa/Abidjan" || z == "Africa/Accra" || z == "Africa/Dakar";
}

static String wanted() {
    SettingsData s = settings.get();
    if (s.tzAuto) {
        GeoInfo g = net.geo();
        if (g.valid) {
            if (const char* p = posixForZone(g.tzName)) return p;
            if (g.offsetSec != 0 || zoneIsUtc(g.tzName)) return posixFixed(g.offsetSec);
        }
    }
    return posixManual(s.tzHours, s.dst);
}

void TimeZone::begin() {
    applied = wanted();
    configTzTime(applied.c_str(), NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
    seenSettings = settings.version();
    seenGeo = net.geoVersion();
    Serial.printf("[Time] TZ=%s\n", applied.c_str());
}

void TimeZone::update() {
    if (settings.version() == seenSettings && net.geoVersion() == seenGeo) return;
    seenSettings = settings.version();
    seenGeo = net.geoVersion();
    String tz = wanted();
    if (tz == applied) return;
    applied = tz;
    setenv("TZ", tz.c_str(), 1);
    tzset();
    Serial.printf("[Time] TZ=%s\n", tz.c_str());
}
