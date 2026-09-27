// Settings.h — all user settings. Read from NVS once at boot, then kept in
// RAM behind a mutex (both loop() and the network task read them).
// Every change bumps version() so tasks can notice it.
#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "Config.h"

enum DisplayMode : uint8_t {
    MODE_NORMAL = 0,   // classic: clock + weather
    MODE_CRYPTO,
    MODE_SPACE,
    MODE_ANALOG,
    MODE_PING,
    MODE_CAT,          // cat eyes with random emotions
    MODE_WEATHER,      // modern weather: all the data
    MODE_MARKETS,      // modern crypto: 24 h chart, turnover
    MODE_COUNT
};

struct SettingsData {
    String   ssid;
    String   pass;
    String   city;                     // empty — resolved by IP
    String   weatherKey;               // WeatherAPI.com key
    String   cryptoList = DEFAULT_CRYPTO_LIST;
    String   pingHost   = DEFAULT_PING_HOST;
    uint32_t cryptoIntervalMs = CRYPTO_INTERVAL_DEFAULT;
    int8_t   tzHours = 0;              // fallback time zone, UTC+N
    bool     tzAuto  = true;           // time zone by IP
    bool     dst     = false;          // fallback zone: EU daylight saving rule
    uint8_t  mode    = MODE_NORMAL;
};

class Settings {
public:
    void begin();
    SettingsData get();
    void set(const SettingsData& d);      // written to NVS right away (web forms)
    void setModeDeferred(uint8_t mode);   // from the button: RAM now, NVS after a pause
    void loop();                          // flushes the deferred mode to NVS
    void clearWiFi();
    void factoryReset();
    uint32_t version() const { return _version; }

private:
    void save(const SettingsData& d);
    static void sanitize(SettingsData& d);
    SettingsData _data;
    SemaphoreHandle_t _mtx = nullptr;
    volatile uint32_t _version = 1;
    uint32_t _modeDirtyAt = 0;            // 0 — nothing pending
};

extern Settings settings;
