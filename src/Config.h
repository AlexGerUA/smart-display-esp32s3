// Config.h — all project constants. Display pins live in platformio.ini
// (TFT_eSPI reads them from build_flags).
#pragma once
#include <Arduino.h>

#define PROJECT_VERSION "2.1.0"

// ---- network ----------------------------------------------------------------
#define AP_SSID  "SmartDisplay"          // setup access point, no password
#define HOSTNAME "smart-display"

// Connecting at boot: 5 attempts with rising TX power and wait time.
// Low power first means a smaller current peak on the Super Mini's stock LDO.
constexpr uint8_t  WIFI_TRIES = 5;
// TX power is NOT changed after connecting — the successful level is kept.
// Changing it after association killed all traffic on the Super Mini
// (DHCP succeeded, then no DNS and no ARP).

// NTP: SNTP runs in the background and resyncs by itself
#define NTP_SERVER_1 "pool.ntp.org"
#define NTP_SERVER_2 "time.google.com"
#define NTP_SERVER_3 "time.cloudflare.com"

constexpr uint32_t WEATHER_INTERVAL_MS = 10UL * 60 * 1000;
constexpr uint32_t GEO_INTERVAL_MS     = 60UL * 60 * 1000;   // time zone offset — hourly
constexpr uint32_t NET_RETRY_MS        = 60UL * 1000;        // after a failed request

// ---- crypto -------------------------------------------------------------------
constexpr int      CRYPTO_COUNT            = 6;
constexpr uint32_t CRYPTO_INTERVAL_DEFAULT = 120000;   // 2 min
constexpr uint32_t CRYPTO_INTERVAL_MIN     = 30000;
constexpr uint32_t CRYPTO_INTERVAL_MAX     = 900000;
#define DEFAULT_CRYPTO_LIST "BTC,ETH,BNB,SOL,XRP,ADA"

#define DEFAULT_PING_HOST "google.com"

// ---- touch button ------------------------------------------------------------
// true  — TTP223 module (output HIGH = touch). The pin is read as plain
//         digital input; the ESP32-S3 capacitive sensor is NOT enabled on a pin
//         driven by the module's output (touchRead saturates there).
// false — bare wire / copper pad on a capacitive-touch GPIO.
constexpr bool     TOUCH_IS_TTP223 = true;
constexpr uint8_t  TOUCH_PIN       = 6;
constexpr float    TOUCH_RATIO     = 0.08f;   // capacitive only: touch = +8% over baseline

// Gestures
constexpr uint32_t TAP_MAX_MS      = 500;     // short tap — next mode / next menu item
constexpr uint32_t HOLD_MENU_MS    = 2000;    // hold outside the menu — open the menu
constexpr uint32_t SELECT_HOLD_MS  = 1000;    // hold in the menu — select (fires at threshold)
constexpr uint32_t BAR_SHOW_MS     = 300;     // hold progress bar appears after 0.3 s
constexpr uint32_t MENU_TIMEOUT_MS = 15000;   // menu closes by itself
// A mode picked with the button is written to flash after a pause, so fast
// switching does not hit NVS on every tap
constexpr uint32_t MODE_SAVE_DELAY_MS = 5000;

// ---- time ---------------------------------------------------------------------
constexpr time_t MIN_VALID_EPOCH = 1700000000;   // nothing is drawn before NTP (1970)
