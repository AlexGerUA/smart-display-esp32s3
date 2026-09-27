#include "Settings.h"
#include <Preferences.h>

Settings settings;

// Namespace and keys are compatible with the original sketch. "cint" is a
// ULong (the original wrote ULong but read Int, so the interval always reset).
static const char* NS = "config";

void Settings::begin() {
    _mtx = xSemaphoreCreateMutex();
    Preferences p;
    p.begin(NS, false);   // read-write: creates the namespace on first boot without an error log
    SettingsData d;
    auto str = [&](const char* k, const char* def) { return p.isKey(k) ? p.getString(k, def) : String(def); };
    d.ssid       = str("ssid", "");
    d.pass       = str("pass", "");
    d.city       = str("city", "");
    d.weatherKey = str("wkey", "");
    d.cryptoList = str("clist", DEFAULT_CRYPTO_LIST);
    d.pingHost   = str("ping", DEFAULT_PING_HOST);
    if (p.isKey("cint"))   d.cryptoIntervalMs = p.getULong("cint", CRYPTO_INTERVAL_DEFAULT);
    if (p.isKey("tz"))     d.tzHours = p.getInt("tz", 0);
    if (p.isKey("tzauto")) d.tzAuto  = p.getBool("tzauto", true);
    if (p.isKey("dst"))    d.dst     = p.getBool("dst", false);
    if (p.isKey("dmode"))  d.mode    = p.getInt("dmode", MODE_NORMAL);
    p.end();

    sanitize(d);
    _data = d;
    Serial.printf("[Settings] SSID='%s' city='%s' mode=%u tz=%s%d dst=%d\n", d.ssid.c_str(),
                  d.city.c_str(), d.mode, d.tzAuto ? "auto/" : "", d.tzHours, d.dst);
}

SettingsData Settings::get() {
    xSemaphoreTake(_mtx, portMAX_DELAY);
    SettingsData copy = _data;
    xSemaphoreGive(_mtx);
    return copy;
}

void Settings::set(const SettingsData& in) {
    SettingsData d = in;
    sanitize(d);
    xSemaphoreTake(_mtx, portMAX_DELAY);
    _data = d;
    xSemaphoreGive(_mtx);
    save(d);
    _modeDirtyAt = 0;   // the mode was saved together with everything else
    _version++;
}

void Settings::setModeDeferred(uint8_t mode) {
    if (mode >= MODE_COUNT) mode = MODE_NORMAL;
    xSemaphoreTake(_mtx, portMAX_DELAY);
    _data.mode = mode;
    xSemaphoreGive(_mtx);
    _modeDirtyAt = millis() | 1;
    _version++;
}

void Settings::loop() {
    if (_modeDirtyAt && millis() - _modeDirtyAt >= MODE_SAVE_DELAY_MS) {
        _modeDirtyAt = 0;
        Preferences p;
        p.begin(NS, false);
        p.putInt("dmode", get().mode);
        p.end();
    }
}

void Settings::clearWiFi() {
    SettingsData d = get();
    d.ssid = "";
    d.pass = "";
    set(d);
}

void Settings::factoryReset() {
    Preferences p;
    p.begin(NS, false);
    p.clear();
    p.end();
    xSemaphoreTake(_mtx, portMAX_DELAY);
    _data = SettingsData();
    xSemaphoreGive(_mtx);
    _modeDirtyAt = 0;
    _version++;
}

void Settings::save(const SettingsData& d) {
    // NVS only writes values that changed, so saving the full set costs no extra wear
    Preferences p;
    p.begin(NS, false);
    p.putString("ssid", d.ssid);
    p.putString("pass", d.pass);
    p.putString("city", d.city);
    p.putString("wkey", d.weatherKey);
    p.putString("clist", d.cryptoList);
    p.putString("ping", d.pingHost);
    p.putULong("cint", d.cryptoIntervalMs);
    p.putInt("tz", d.tzHours);
    p.putBool("tzauto", d.tzAuto);
    p.putBool("dst", d.dst);
    p.putInt("dmode", d.mode);
    p.end();
}

void Settings::sanitize(SettingsData& d) {
    d.cryptoIntervalMs = constrain(d.cryptoIntervalMs, CRYPTO_INTERVAL_MIN, CRYPTO_INTERVAL_MAX);
    if (d.mode >= MODE_COUNT) d.mode = MODE_NORMAL;
    if (d.tzHours < -12 || d.tzHours > 14) d.tzHours = 0;
    d.city.trim();
    d.weatherKey.trim();
    d.pingHost.trim();
    d.cryptoList.trim();
    d.cryptoList.toUpperCase();
    if (d.cryptoList.isEmpty()) d.cryptoList = DEFAULT_CRYPTO_LIST;
    if (d.pingHost.isEmpty()) d.pingHost = DEFAULT_PING_HOST;
}
