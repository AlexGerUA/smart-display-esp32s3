#include "NetService.h"
#include "Settings.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <lwip/dns.h>

NetService net;

// TLS (mbedTLS is internal-RAM only in this core) needs ~40 KB in one block.
// If there isn't that much, the request is postponed instead of crashing.
static constexpr size_t TLS_MIN_BLOCK = 48 * 1024;

// ------------------------------------------------------------------ helpers

static String urlEncode(const String& s) {
    static const char hex[] = "0123456789ABCDEF";
    String out;
    out.reserve(s.length() * 3);
    for (size_t i = 0; i < s.length(); i++) {
        uint8_t c = s[i];
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += (char)c;
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

// "BTC,ETH,..." -> exactly CRYPTO_COUNT slots (the rest empty)
static void parseList(const String& list, String out[CRYPTO_COUNT]) {
    int idx = 0, start = 0;
    while (idx < CRYPTO_COUNT && start <= (int)list.length()) {
        int comma = list.indexOf(',', start);
        if (comma < 0) comma = list.length();
        String s = list.substring(start, comma);
        s.trim();
        s.toUpperCase();
        if (s.length()) out[idx++] = s;
        start = comma + 1;
    }
    while (idx < CRYPTO_COUNT) out[idx++] = "";
}

// "06:52 AM" / "07:01 PM" -> "06:52" / "19:01"
static String to24h(const char* s) {
    int h = 0, m = 0;
    char ap[3] = {0};
    if (sscanf(s, "%d:%d %2s", &h, &m, ap) < 2) return "";
    if ((ap[0] == 'P' || ap[0] == 'p') && h < 12) h += 12;
    if ((ap[0] == 'A' || ap[0] == 'a') && h == 12) h = 0;
    char buf[6];
    snprintf(buf, sizeof(buf), "%02d:%02d", h, m);
    return buf;
}

static bool tlsMemoryOk(const char* what) {
    size_t block = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    if (block >= TLS_MIN_BLOCK) return true;
    Serial.printf("[Net] %s postponed: largest free RAM block %u KB\n", what, block / 1024);
    return false;
}

static wifi_power_t dbmToPower(float dbm) {
    return (wifi_power_t)(int)(dbm * 4);   // wifi_power_t units are 0.25 dBm
}

// ------------------------------------------------------------------ Wi-Fi

void NetService::begin() {
    if (!_mtx) _mtx = xSemaphoreCreateMutex();
}

bool NetService::connectSaved(void (*progress)(uint8_t, uint32_t)) {
    SettingsData s = settings.get();
    if (s.ssid.isEmpty()) {
        Serial.println("[WiFi] No saved network");
        return false;
    }
    WiFi.persistent(false);          // credentials are kept in Settings, not by the SDK
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(HOSTNAME);
    WiFi.setAutoReconnect(true);     // after boot, reconnects are handled by the stack

    // 5 attempts with rising power and wait time. Low power first means a
    // smaller current peak; mixed WPA2/WPA3 routers often reject the first try.
    static const struct { float dbm; uint16_t waitMs; } TRY[WIFI_TRIES] = {
        {8.5f, 3000}, {11.0f, 3500}, {13.0f, 4000}, {15.0f, 5000}, {19.5f, 6000},
    };
    uint32_t t0 = millis();
    for (uint8_t i = 0; i < WIFI_TRIES; i++) {
        Serial.printf("[WiFi] attempt %u of %u: '%s', %.1f dBm, %u ms\n",
                      i + 1, WIFI_TRIES, s.ssid.c_str(), TRY[i].dbm, TRY[i].waitMs);
        if (i > 0) {
            WiFi.disconnect();
            delay(100);
        }
        WiFi.setTxPower(dbmToPower(TRY[i].dbm));
        WiFi.begin(s.ssid.c_str(), s.pass.c_str());
        uint32_t ta = millis();
        while (millis() - ta < TRY[i].waitMs) {
            if (WiFi.status() == WL_CONNECTED) {
                // Keep the TX power that worked: changing it after association
                // killed all traffic on the Super Mini (DHCP ok, then no DNS/ARP).
                // After reconnects DHCP sometimes leaves DNS empty and no name
                // resolves — fall back to public DNS servers.
                if ((uint32_t)WiFi.dnsIP(0) == 0) {
                    ip_addr_t d1 = IPADDR4_INIT_BYTES(1, 1, 1, 1);
                    ip_addr_t d2 = IPADDR4_INIT_BYTES(8, 8, 8, 8);
                    dns_setserver(0, &d1);
                    dns_setserver(1, &d2);
                    Serial.println("[WiFi] No DNS from DHCP — using 1.1.1.1 / 8.8.8.8");
                }
                Serial.printf("[WiFi] connected on attempt %u: IP %s, DNS %s, RSSI %d, %.1f dBm\n",
                              i + 1, WiFi.localIP().toString().c_str(),
                              WiFi.dnsIP(0).toString().c_str(), WiFi.RSSI(), TRY[i].dbm);
                return true;
            }
            if (progress) progress(i + 1, millis() - t0);
            delay(100);
        }
    }
    Serial.println("[WiFi] All attempts failed");
    return false;
}

void NetService::startAP() {
    // AP_STA: the station keeps trying the saved network. Once it connects,
    // main.cpp switches to normal operation.
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(AP_SSID);
    _ap = true;
    Serial.printf("[WiFi] Access point '%s', IP %s\n", AP_SSID, WiFi.softAPIP().toString().c_str());
}

void NetService::leaveAP() {
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    _ap = false;
    Serial.println("[WiFi] Access point off, running as station");
}

// ------------------------------------------------------------------ task

void NetService::startTask() {
    begin();
    // 16 KB stack — room for a TLS handshake. Stacks are internal RAM.
    xTaskCreatePinnedToCore(taskEntry, "net", 16384, this, 1, nullptr, 0);
}

void NetService::taskEntry(void* self) {
    static_cast<NetService*>(self)->run();
}

void NetService::setCryptoActive(bool on, bool withSpark) {
    // screen entered (or a chart is needed) — fetch fresh data right away
    if (on && (!_cryptoActive || (withSpark && !_sparkActive))) _cryptoReq = true;
    _cryptoActive = on;
    _sparkActive = on && withSpark;
}

void NetService::run() {
    uint32_t nextGeo = 0, nextWeather = 0, lastCrypto = 0;
    bool wasOnline = false;
    uint32_t seenSettings = 0;
    String city, key, clist;
    String syms[CRYPTO_COUNT];

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(200));
        bool online = _fetching && WiFi.status() == WL_CONNECTED;
        if (!online) { wasOnline = false; continue; }
        uint32_t now = millis();
        if (!wasOnline) {              // just (re)connected — refresh everything
            wasOnline = true;
            nextGeo = nextWeather = now;
            _cryptoReq = true;
        }

        // New settings from the web page — applied without a reboot
        if (settings.version() != seenSettings) {
            seenSettings = settings.version();
            SettingsData s = settings.get();
            if (s.city != city || s.weatherKey != key) nextWeather = now;
            if (s.cryptoList != clist) {
                parseList(s.cryptoList, syms);
                lock();
                for (int i = 0; i < CRYPTO_COUNT; i++) {
                    _crypto.sym[i] = syms[i];
                    _crypto.rate[i] = 0;
                    _crypto.pct[i] = 0;
                }
                unlock();
                _cryptoVer++;
                _cryptoReq = true;
            }
            city = s.city;
            key = s.weatherKey;
            clist = s.cryptoList;
        }

        if ((int32_t)(now - nextGeo) >= 0) {
            nextGeo = now + (fetchGeo() ? GEO_INTERVAL_MS : NET_RETRY_MS);
        }

        if ((int32_t)(now - nextWeather) >= 0) {
            bool ok = fetchWeather(city, key);
            nextWeather = millis() + (ok ? WEATHER_INTERVAL_MS : NET_RETRY_MS);
            if (ok) {
                lock();
                String url = _weather.iconUrl;
                unlock();
                if (url.length() && url != _iconUrlLoaded) fetchIcon(url);
            }
        }

        if (_cryptoActive) {
            uint32_t interval = settings.get().cryptoIntervalMs;
            if (_cryptoReq || lastCrypto == 0 || millis() - lastCrypto >= interval) {
                _cryptoReq = false;
                fetchCrypto(syms);
                lastCrypto = millis();
            }
        }
    }
}

// ------------------------------------------------------------------ requests

bool NetService::fetchGeo() {
    WiFiClient client;
    HTTPClient http;
    http.setConnectTimeout(5000);
    http.setTimeout(5000);
    // ip-api is free over plain http only. The IANA timezone is more reliable
    // than offset: for Europe/Kyiv ip-api returns offset=0.
    if (!http.begin(client, "http://ip-api.com/json/?fields=status,city,offset,timezone")) return false;
    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        Serial.printf("[Geo] HTTP %d\n", code);
        http.end();
        return false;
    }
    String body = http.getString();
    http.end();

    JsonDocument doc(PsramJsonAllocator::instance());
    if (deserializeJson(doc, body) || strcmp(doc["status"] | "", "success") != 0) {
        Serial.println("[Geo] Bad response");
        return false;
    }
    GeoInfo g;
    g.valid = true;
    g.offsetSec = doc["offset"] | 0;
    g.tzName = doc["timezone"] | "";
    g.city = doc["city"] | "";
    lock();
    _geo = g;
    unlock();
    _geoVer++;
    Serial.printf("[Geo] %s, %s, offset %+.1f h\n", g.city.c_str(), g.tzName.c_str(), g.offsetSec / 3600.0f);
    return true;
}

bool NetService::fetchWeather(const String& city, const String& key) {
    if (key.isEmpty()) {
        Serial.println("[Weather] No WeatherAPI key — skipping");
        return true;   // not a network error, no need to retry every minute
    }
    if (!tlsMemoryOk("Weather")) return false;
    // City from settings, else from geolocation, and auto:ip only as a last
    // resort — it tends to return a district rather than the city
    String q = city;
    if (q.isEmpty()) { lock(); q = _geo.city; unlock(); }
    // forecast.json (days=1): current weather + min/max, rain chance, sunrise
    // and sunset. The ~25 KB response lands in PSRAM (String > 4 KB); the
    // filter keeps a few hundred bytes of it.
    String url = "https://api.weatherapi.com/v1/forecast.json?days=1&aqi=no&alerts=no&key=" +
                 urlEncode(key) + "&q=" + (q.length() ? urlEncode(q) : String("auto:ip"));

    String body;
    {
        WiFiClientSecure client;
        client.setInsecure();
        HTTPClient http;
        http.setConnectTimeout(5000);
        http.setTimeout(8000);
        if (!http.begin(client, url)) return false;
        int code = http.GET();
        if (code != HTTP_CODE_OK) {
            Serial.printf("[Weather] HTTP %d\n", code);
            http.end();
            return false;
        }
        body = http.getString();
        http.end();
    }   // TLS released before parsing

    // Filter: only the needed fields reach memory
    JsonDocument filter(PsramJsonAllocator::instance());
    filter["location"]["name"] = true;
    JsonObject f = filter["current"].to<JsonObject>();
    for (const char* k : {"temp_c", "humidity", "pressure_mb", "wind_kph", "feelslike_c", "gust_kph",
                          "uv", "vis_km", "cloud", "precip_mm", "is_day", "wind_dir"}) f[k] = true;
    f["condition"]["code"] = true;
    f["condition"]["icon"] = true;
    f["condition"]["text"] = true;
    JsonObject fd = filter["forecast"]["forecastday"][0].to<JsonObject>();
    for (const char* k : {"maxtemp_c", "mintemp_c", "daily_chance_of_rain"}) fd["day"][k] = true;
    fd["astro"]["sunrise"] = true;
    fd["astro"]["sunset"] = true;

    JsonDocument doc(PsramJsonAllocator::instance());
    if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) {
        Serial.println("[Weather] JSON error");
        return false;
    }
    JsonObject cur = doc["current"];
    if (cur.isNull()) return false;

    // All numbers via float: "| 0" with an int default rejects 1012.0 and yields 0
    WeatherInfo w;
    w.valid    = true;
    w.temp     = cur["temp_c"] | 0.0f;
    w.humidity = cur["humidity"] | 0.0f;
    w.pressure = (int)lroundf(cur["pressure_mb"] | 0.0f);
    w.wind     = (cur["wind_kph"] | 0.0f) / 3.6f;
    w.code     = (int)(cur["condition"]["code"] | 0.0f);
    w.location = doc["location"]["name"] | "";
    String icon = cur["condition"]["icon"] | "";
    if (icon.startsWith("//")) icon = "https:" + icon;
    else if (icon.startsWith("http://")) icon.replace("http://", "https://");
    w.iconUrl = icon;

    w.feels    = cur["feelslike_c"] | 0.0f;
    w.gust     = (cur["gust_kph"] | 0.0f) / 3.6f;
    w.uv       = cur["uv"] | 0.0f;
    w.visKm    = cur["vis_km"] | 0.0f;
    w.precipMm = cur["precip_mm"] | 0.0f;
    w.cloud    = (int)(cur["cloud"] | 0.0f);
    w.isDay    = (cur["is_day"] | 1.0f) != 0;
    w.windDir  = cur["wind_dir"] | "";
    w.text     = cur["condition"]["text"] | "";
    JsonObject day = doc["forecast"]["forecastday"][0]["day"];
    w.tMax       = day["maxtemp_c"] | w.temp;
    w.tMin       = day["mintemp_c"] | w.temp;
    w.rainChance = (int)(day["daily_chance_of_rain"] | 0.0f);
    JsonObject astro = doc["forecast"]["forecastday"][0]["astro"];
    w.sunrise = to24h(astro["sunrise"] | "");
    w.sunset  = to24h(astro["sunset"] | "");

    lock();
    _weather = w;
    unlock();
    _weatherVer++;
    Serial.printf("[Weather] %s: %.1f C, %d%%, %d hPa, %.1f m/s, code %d\n",
                  w.location.c_str(), w.temp, (int)w.humidity, w.pressure, w.wind, w.code);
    return true;
}

bool NetService::fetchIcon(const String& url) {
    if (!tlsMemoryOk("Icon")) return false;
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setConnectTimeout(5000);
    http.setTimeout(5000);
    if (!http.begin(client, url)) return false;
    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        Serial.printf("[Icon] HTTP %d\n", code);
        http.end();
        return false;
    }
    // WeatherAPI 64x64 icons are a few KB; 32 KB is a safety cap
    const size_t MAX_ICON = 32768;
    int len = http.getSize();
    if (len > (int)MAX_ICON) { http.end(); return false; }
    PsBytes buf;
    buf.reserve(len > 0 ? len : 8192);
    WiFiClient* s = http.getStreamPtr();
    uint8_t chunk[512];
    uint32_t last = millis();
    while (http.connected() && (len < 0 || (int)buf.size() < len) && millis() - last < 5000) {
        size_t n = s->available();
        if (!n) { delay(2); continue; }
        n = s->readBytes(chunk, min(n, sizeof(chunk)));
        buf.insert(buf.end(), chunk, chunk + n);
        last = millis();
        if (buf.size() > MAX_ICON) { http.end(); return false; }
    }
    http.end();
    if (buf.size() < 8 || (len > 0 && (int)buf.size() != len)) return false;

    lock();
    _icon.swap(buf);
    unlock();
    _iconUrlLoaded = url;   // the same icon is not re-downloaded every 10 min
    _iconVer++;
    return true;
}

void NetService::fetchCrypto(const String syms[CRYPTO_COUNT]) {
    if (!tlsMemoryOk("Crypto")) return;
    // One keep-alive TLS connection for all coins instead of six handshakes
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setReuse(true);
    http.setConnectTimeout(5000);
    http.setTimeout(8000);

    JsonDocument filter(PsramJsonAllocator::instance());
    filter["retCode"] = true;
    for (const char* k : {"lastPrice", "price24hPcnt", "highPrice24h", "lowPrice24h", "turnover24h"})
        filter["result"]["list"][0][k] = true;

    for (int i = 0; i < CRYPTO_COUNT; i++) {
        if (syms[i].isEmpty()) continue;
        if (!_cryptoActive) break;   // left the screen — stop fetching
        String url = "https://api.bybit.com/v5/market/tickers?category=spot&symbol=" + syms[i] + "USDT";
        if (!http.begin(client, url)) continue;
        int code = http.GET();
        if (code != HTTP_CODE_OK) {
            Serial.printf("[Crypto] %s: HTTP %d\n", syms[i].c_str(), code);
            http.end();
            continue;
        }
        String body = http.getString();
        http.end();

        JsonDocument doc(PsramJsonAllocator::instance());
        if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) continue;
        if ((doc["retCode"] | -1) != 0) continue;
        JsonObject t = doc["result"]["list"][0];
        if (t.isNull()) continue;
        double price = atof(t["lastPrice"] | "0");               // Bybit returns numbers as strings
        float pct = atof(t["price24hPcnt"] | "0") * 100.0f;
        double hi = atof(t["highPrice24h"] | "0"), lo = atof(t["lowPrice24h"] | "0");
        double turnover = atof(t["turnover24h"] | "0");

        // 24 h chart — MARKETS screen only: 24 hourly candles
        float spark[SPARK_N];
        int sparkN = _sparkActive ? fetchSpark(http, client, syms[i], spark) : -1;

        lock();
        if (_crypto.sym[i] == syms[i]) {
            _crypto.rate[i] = price;
            _crypto.pct[i] = pct;
            _crypto.hi[i] = hi;
            _crypto.lo[i] = lo;
            _crypto.turnover[i] = turnover;
            if (sparkN > 0) {
                memcpy(_crypto.spark[i], spark, sizeof(spark));
                _crypto.sparkN[i] = sparkN;
            }
        }
        unlock();
        _cryptoVer++;   // the row updates as soon as its price arrives
    }
}

// Hourly close prices for the last 24 h, oldest to newest.
// Uses the same keep-alive connection as the tickers.
int NetService::fetchSpark(HTTPClient& http, WiFiClientSecure& client, const String& sym, float out[SPARK_N]) {
    String url = "https://api.bybit.com/v5/market/kline?category=spot&interval=60&limit=" + String(SPARK_N) +
                 "&symbol=" + sym + "USDT";
    if (!http.begin(client, url)) return 0;
    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        http.end();
        return 0;
    }
    String body = http.getString();
    http.end();

    JsonDocument doc(PsramJsonAllocator::instance());
    if (deserializeJson(doc, body) || (doc["retCode"] | -1) != 0) return 0;
    JsonArray list = doc["result"]["list"];   // [start, open, high, low, close, ...], newest first
    int n = min((int)list.size(), SPARK_N);
    for (int k = 0; k < n; k++) out[n - 1 - k] = atof(list[k][4] | "0");
    return n;
}

// ------------------------------------------------------------------ getters

WeatherInfo NetService::weather() { lock(); WeatherInfo w = _weather; unlock(); return w; }
CryptoInfo  NetService::crypto()  { lock(); CryptoInfo c = _crypto;   unlock(); return c; }
GeoInfo     NetService::geo()     { lock(); GeoInfo g = _geo;         unlock(); return g; }

bool NetService::iconPng(PsBytes& out) {
    lock();
    out = _icon;
    unlock();
    return !out.empty();
}
