// NetService.h — Wi-Fi and all network requests.
//
// Connecting at boot happens in the loop task (blocking, with progress on the
// screen). After that everything network-related runs in its own task on
// core 0: weather, icon, crypto, geolocation. The display and web code in
// loop() only pick up ready data snapshots by version counters, so HTTPS
// never stalls the clock.
#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "Config.h"
#include "PsramAlloc.h"

struct WeatherInfo {
    bool   valid = false;
    float  temp = 0, humidity = 0, wind = 0;   // °C, %, m/s
    int    pressure = 0;                        // hPa
    int    code = 0;                            // WeatherAPI condition code
    String location;                            // city name from the API response
    String iconUrl;
    // Extended — for the modern WEATHER mode
    float  feels = 0, gust = 0, uv = 0, visKm = 0, precipMm = 0;
    int    cloud = 0, rainChance = 0;
    float  tMax = 0, tMin = 0;
    bool   isDay = true;
    String text;                                // condition text
    String windDir;                             // NNW etc.
    String sunrise, sunset;                     // "06:52", 24-hour
};

constexpr int SPARK_N = 24;                     // hourly prices for 24 h

struct CryptoInfo {
    String sym[CRYPTO_COUNT];
    double rate[CRYPTO_COUNT] = {0};
    float  pct[CRYPTO_COUNT]  = {0};            // 24 h change, %
    double hi[CRYPTO_COUNT] = {0}, lo[CRYPTO_COUNT] = {0};   // 24 h range
    double turnover[CRYPTO_COUNT] = {0};        // 24 h turnover, USDT
    float  spark[CRYPTO_COUNT][SPARK_N] = {};   // oldest -> newest
    uint8_t sparkN[CRYPTO_COUNT] = {0};
};

struct GeoInfo {
    bool    valid = false;
    int32_t offsetSec = 0;                      // current UTC offset
    String  tzName;                             // IANA name, e.g. Europe/Kyiv
    String  city;
};

class NetService {
public:
    void begin();                               // mutex; before any getter

    // ---- Wi-Fi (loop task) ----
    bool connectSaved(void (*progress)(uint8_t attempt, uint32_t elapsedMs));
    void startAP();
    void leaveAP();
    bool isAP() const { return _ap; }

    // ---- background task ----
    void startTask();
    void setFetching(bool on) { _fetching = on; }   // only while running
    void setCryptoActive(bool on, bool withSpark = false);   // crypto — only on its screens

    // ---- data: copies taken under the mutex ----
    uint32_t weatherVersion() const { return _weatherVer; }
    uint32_t cryptoVersion()  const { return _cryptoVer; }
    uint32_t iconVersion()    const { return _iconVer; }
    uint32_t geoVersion()     const { return _geoVer; }
    WeatherInfo weather();
    CryptoInfo  crypto();
    GeoInfo     geo();
    bool        iconPng(PsBytes& out);

private:
    static void taskEntry(void* self);
    void run();
    bool fetchGeo();
    bool fetchWeather(const String& city, const String& key);
    bool fetchIcon(const String& url);
    void fetchCrypto(const String syms[CRYPTO_COUNT]);
    int  fetchSpark(class HTTPClient& http, class WiFiClientSecure& client, const String& sym, float out[SPARK_N]);
    void lock()   { xSemaphoreTake(_mtx, portMAX_DELAY); }
    void unlock() { xSemaphoreGive(_mtx); }

    SemaphoreHandle_t _mtx = nullptr;
    volatile bool _ap = false;
    volatile bool _fetching = false;
    volatile bool _cryptoActive = false;
    volatile bool _cryptoReq = false;
    volatile bool _sparkActive = false;

    WeatherInfo _weather;
    CryptoInfo  _crypto;
    GeoInfo     _geo;
    PsBytes     _icon;
    String      _iconUrlLoaded;

    volatile uint32_t _weatherVer = 0, _cryptoVer = 0, _iconVer = 0, _geoVer = 0;
};

extern NetService net;
