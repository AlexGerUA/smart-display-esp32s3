#include "Display.h"
#include "NetService.h"
#include "CatMode.h"
#include <TFT_eSPI.h>
#include <PNGdec.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <esp_heap_caps.h>
#include <soc/soc_memory_layout.h>
#include <memory>
#include "PsramAlloc.h"

// Fonts and icons are included here only (the original pulled them into several .cpp files).
#include "assets/Volkswagen100.h"
#include "assets/Volkswagen80.h"
#include "assets/Volkswagen30.h"
#include "assets/Volkswagen19.h"
#include "assets/bitmaps.h"
// Bahnschrift (tools/make_vlw.py) — modern WEATHER and MARKETS modes
#include "assets/BsTiny.h"
#include "assets/BsMed.h"
#include "assets/BsNum.h"
#include "assets/BsClock.h"

Display display;

// ================================================================ frame and background

static constexpr int W = TFT_WIDTH, H = TFT_HEIGHT;   // 240 x 280, portrait

static TFT_eSPI    tft;
static TFT_eSprite fb(&tft);          // frame buffer, PSRAM
static uint16_t*   fbPix = nullptr;   // its pixels (sprite byte order)
static uint16_t*   bgPix = nullptr;   // copy of the mode's clean background, PSRAM
static bool        batch = false;     // true — fields are not pushed one by one

constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
static constexpr uint16_t C_GRAY   = rgb(204, 204, 204);
static constexpr uint16_t C_ORANGE = rgb(237, 147, 95);
static constexpr uint16_t C_BROWN  = rgb(85, 63, 47);
static constexpr uint16_t C_DARK   = rgb(23, 23, 23);
static constexpr uint16_t C_LINE   = rgb(142, 142, 142);
static constexpr uint16_t C_BEIGE  = rgb(196, 180, 162);

enum BgKind : uint8_t { BG_GRADIENT, BG_CRYPTO, BG_BLACK, BG_DARK, BG_MODERN };
static BgKind bgKind = BG_GRADIENT;

// Background gradients are computed once
static uint16_t gradLut[H];     // classic NORMAL: sand -> grey-blue
static uint16_t modernLut[H];   // modern modes: graphite -> near black

static inline uint16_t swap16(uint16_t c) { return (c >> 8) | (c << 8); }

// Background colour without the PSRAM copy (fallback for boards without PSRAM)
static uint16_t fallbackBg(int y) {
    switch (bgKind) {
        case BG_GRADIENT: return gradLut[constrain(y, 0, H - 1)];
        case BG_CRYPTO:   return y < 128 ? C_BROWN : (y < 132 ? C_LINE : C_DARK);
        case BG_DARK:     return C_DARK;
        case BG_MODERN:   return modernLut[constrain(y, 0, H - 1)];
        default:          return TFT_BLACK;
    }
}

// Background colour under a pixel. TFT_eSPI also asks this for glyph anti-aliasing.
static uint16_t bgAt(uint16_t x, uint16_t y) {
    if (x >= W || y >= H) return TFT_BLACK;
    return bgPix ? swap16(bgPix[y * W + x]) : fallbackBg(y);
}

static void saveBg() {
    if (bgPix) memcpy(bgPix, fbPix, W * H * sizeof(uint16_t));
}

static bool clip(int& x, int& y, int& w, int& h) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > W) w = W - x;
    if (y + h > H) h = H - y;
    return w > 0 && h > 0;
}

// Restore a rectangle to the clean background
static void restore(int x, int y, int w, int h) {
    if (!clip(x, y, w, h)) return;
    for (int r = y; r < y + h; r++) {
        if (bgPix) memcpy(fbPix + r * W + x, bgPix + r * W + x, w * sizeof(uint16_t));
        else fb.drawFastHLine(x, r, w, fallbackBg(r));
    }
}

static void flush(int x, int y, int w, int h) {
    if (batch || !clip(x, y, w, h)) return;
    fb.pushSprite(x, y, x, y, w, h);
}

static void flushAll() { fb.pushSprite(0, 0); }

// ---- hold progress bar (3 px at the bottom, over any mode)
static constexpr int BAR_H = 3, BAR_Y = H - BAR_H;
static float    barP = -1;
static bool     barWhite = false;
static bool     barDirty = false;

// Draw the bar into the frame (for modes that push the whole frame)
static void overlayBar() {
    if (barP < 0) return;
    int w = constrain((int)(barP * W), 0, W);
    fb.fillRect(0, BAR_Y, w, BAR_H, barWhite ? TFT_WHITE : rgb(237, 147, 95));
}

// Refresh only the bar, for modes that don't redraw the whole frame
static void refreshBar() {
    restore(0, BAR_Y, W, BAR_H);
    overlayBar();
    flush(0, BAR_Y, W, BAR_H);
    barDirty = false;
}

// ================================================================ text

static const uint8_t* curFont = nullptr;

// loadFont parses the glyph table — don't do it for every string
static void useFont(const uint8_t* f) {
    if (curFont == f) return;
    if (curFont) fb.unloadFont();
    if (f) fb.loadFont(f);
    curFont = f;
}

static int textW(const char* s, const uint8_t* font) {
    useFont(font);
    return fb.textWidth(s);
}

// Field: restore background, draw text clipped to the field, push
static void field(int x, int y, int w, int h, const char* s, uint16_t fg,
                  const uint8_t* font, bool center = false) {
    restore(x, y, w, h);
    useFont(font);
    int tx = center ? x + (w - fb.textWidth(s)) / 2 : x;
    fb.setViewport(x, y, w, h, false);    // false: coordinates stay in screen space
    fb.setTextColor(fg, fg, false);       // background comes from the bgAt callback
    fb.drawString(s, tx, y);
    fb.resetViewport();
    flush(x, y, w, h);
}

// Gradient screen for status messages
static void gradientScreen() {
    bgKind = BG_GRADIENT;
    for (int y = 0; y < H; y++) fb.drawFastHLine(0, y, W, gradLut[y]);
    saveBg();
}

static void line(int x, int y, const char* s, uint16_t fg) {
    useFont(Volkswagen19);
    fb.setTextColor(fg, fg, false);
    fb.drawString(s, x, y);
}

// ================================================================ Wi-Fi icon

static int wifiBucket() {
    if (WiFi.status() != WL_CONNECTED) return 0;
    int r = WiFi.RSSI();
    return r > -60 ? 4 : r > -70 ? 3 : r > -80 ? 2 : 1;
}

static void drawWifi(int bucket, uint16_t color) {
    static const unsigned char* icons[] = {
        epd_bitmap_non_wi_fi, epd_bitmap_low_wi_fi, epd_bitmap_low_medium_wi_fi,
        epd_bitmap_medium_wi_fi, epd_bitmap_hight_wi_fi};
    restore(200, 5, 23, 18);
    fb.drawBitmap(200, 5, icons[bucket], 23, 18, color);
    flush(200, 5, 23, 18);
}

// ================================================================ weather icon

static const unsigned char* iconByCode(int c) {
    // https://www.weatherapi.com/docs/weather_conditions.json
    if (c == 1000) return epd_bitmap_clear_sky;
    if (c == 1003) return epd_bitmap_few_clouds;
    if (c == 1006) return epd_bitmap_scattered_clouds;
    if (c == 1009) return epd_bitmap_broken_clouds;
    if (c == 1030 || c == 1135 || c == 1147) return epd_bitmap_mist;
    // Thunder before snow: 1087 and 1273–1282 used to fall into the snow range
    if (c == 1087 || (c >= 1273 && c <= 1282)) return epd_bitmap_thunderstorm;
    if (c == 1066 || c == 1069 || c == 1114 || c == 1117 ||
        (c >= 1204 && c <= 1237) || (c >= 1249 && c <= 1264)) return epd_bitmap_snow;
    if (c == 1186 || c == 1189 || c == 1192 || c == 1195 || c == 1201 ||
        c == 1243 || c == 1246) return epd_bitmap_shower_rain;
    if (c == 1063 || c == 1072 || c == 1150 || c == 1153 || c == 1168 || c == 1171 ||
        c == 1180 || c == 1183 || c == 1198 || c == 1240) return epd_bitmap_rain;
    return epd_bitmap_clear_sky;
}

static constexpr int ICON_SZ = 64;

struct IconCtx { PNG* png; int x, y; };

static int pngDraw(PNGDRAW* d) {
    IconCtx* c = static_cast<IconCtx*>(d->pUser);
    const int ICON_X = c->x;
    int y = c->y + d->y;
    if (y >= H) return 1;
    int w = min(d->iWidth, ICON_SZ);
    if (d->iPixelType == PNG_PIXEL_TRUECOLOR_ALPHA) {
        // Real alpha: blend with the background under each pixel
        const uint8_t* p = d->pPixels;
        for (int x = 0; x < w; x++, p += 4) {
            if (!p[3]) continue;
            int px = ICON_X + x;
            uint16_t col = rgb(p[0], p[1], p[2]);
            if (p[3] < 255) col = fb.alphaBlend(p[3], col, bgAt(px, y));
            fb.drawPixel(px, y, col);
        }
    } else {
        // Palette / no alpha: PNGdec blends with the row's background colour
        uint16_t bg = bgAt(ICON_X + w / 2, y);
        uint32_t bg888 = ((bg >> 8) & 0xF8) | (((bg >> 3) & 0xFC) << 8) | ((uint32_t)((bg << 3) & 0xF8) << 16);
        uint16_t px[ICON_SZ];
        c->png->getLineAsRGB565(d, px, PNG_RGB565_LITTLE_ENDIAN, bg888);
        for (int x = 0; x < w; x++) fb.drawPixel(ICON_X + x, y, px[x]);
    }
    return 1;
}

// Weather icon: PNG from WeatherAPI (alpha-blended), or a built-in mono icon offline
static void drawIcon(int code, int ICON_X = 17, int ICON_Y = 213) {
    restore(ICON_X, ICON_Y, ICON_SZ, ICON_SZ);
    PsBytes data;
    bool ok = false;
    if (net.iconPng(data)) {
        // The ~45 KB decoder lives in PSRAM only while decoding
        void* mem = psMalloc(sizeof(PNG));
        if (mem) {
            PNG* png = new (mem) PNG();
            IconCtx ctx{png, ICON_X, ICON_Y};
            if (png->openRAM(data.data(), data.size(), pngDraw) == PNG_SUCCESS) {
                ok = png->decode(&ctx, 0) == PNG_SUCCESS;
                png->close();
            }
            png->~PNG();
            free(mem);
        }
    }
    if (!ok) fb.drawBitmap(ICON_X, ICON_Y, iconByCode(code), 56, 45, TFT_WHITE);
    flush(ICON_X, ICON_Y, ICON_SZ, ICON_SZ);
}

// ================================================================ clock

static bool localNow(tm& t) {
    time_t now = time(nullptr);
    if (now < MIN_VALID_EPOCH) return false;   // nothing is drawn before NTP
    localtime_r(&now, &t);
    return true;
}

static const char* MONTHS[12] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                 "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
static const char* WDAYS[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};

static void digit(int x, int y, int w, int h, char ch, uint16_t col, const uint8_t* font) {
    char s[2] = {ch, 0};
    field(x, y, w, h, s, col, font);
}

// State shared by the modes: what is on the screen now
static struct {
    int h, m, s, day, mon, wday;
    uint32_t weatherVer, iconVer, cryptoVer;
    int wifi;
    uint32_t ip;
    uint32_t lastPoll;
    time_t lastT;
} st;

static void resetState() {
    memset(&st, 0xFF, sizeof(st));   // all -1: redraw from scratch
    st.lastPoll = 0;
    st.lastT = 0;
}

// ================================================================ NORMAL

static void drawIp(int x, uint16_t col) {
    st.ip = (uint32_t)WiFi.localIP();
    field(x, 5, 145, 20, WiFi.localIP().toString().c_str(), col, Volkswagen19);
}

static void normalStatic() {
    bgKind = BG_GRADIENT;
    for (int y = 0; y < H; y++) fb.drawFastHLine(0, y, W, gradLut[y]);
    fb.fillRect(0, 50, 120, 2, TFT_BLACK);
    fb.fillRect(160, 90, 80, 2, TFT_BLACK);
    fb.fillRect(0, 185, 240, 2, TFT_BLACK);
    fb.fillRect(150, 185, 2, 95, TFT_BLACK);
    for (int o = -5; o <= 5; o++) fb.drawLine(120 + o, 50, 160 + o, 91, TFT_BLACK);   // 11 px diagonal
    fb.fillCircle(70, 38, 3, TFT_BLACK);
    fb.fillCircle(65, 203, 3, TFT_BLACK);
    fb.drawBitmap(170, 220, epd_bitmap_WIND, 51, 37, C_GRAY);
    saveBg();
}

static void normalWeather() {
    WeatherInfo w = net.weather();
    st.weatherVer = net.weatherVersion();
    char buf[32];

    drawIp(15, TFT_WHITE);

    if (w.valid) snprintf(buf, sizeof(buf), "%d%%", (int)lroundf(w.humidity));
    else strcpy(buf, "--%");
    field(15, 195, 50, 20, buf, TFT_BLACK, Volkswagen19);

    if (w.valid) snprintf(buf, sizeof(buf), "%d", w.pressure);
    else strcpy(buf, "----");
    field(85, 195, 50, 20, buf, TFT_BLACK, Volkswagen19);

    // City: from settings, else from geolocation. Not WeatherAPI's location —
    // by IP it returns a district rather than the city
    String city = settings.get().city;
    if (city.isEmpty()) city = net.geo().city;
    if (city.isEmpty()) city = w.location;
    city.toUpperCase();
    field(0, 161, 170, 22, city.c_str(), C_ORANGE, Volkswagen19, true);

    if (w.valid) snprintf(buf, sizeof(buf), "%dC", (int)lroundf(w.temp));
    else strcpy(buf, "--C");
    field(81, 232, 60, 30, buf, TFT_WHITE, Volkswagen30);

    if (w.valid) snprintf(buf, sizeof(buf), "%.1f m/s", w.wind);
    else strcpy(buf, "-- m/s");
    field(152, 195, 86, 22, buf, TFT_BLACK, Volkswagen19, true);

    if (net.iconVersion() == st.iconVer || !w.valid) drawIcon(w.code);   // otherwise the icon branch draws it
}

static void normalClock(const tm& t) {
    char s[4];
    if (t.tm_hour != st.h) {
        snprintf(s, sizeof(s), "%02d", t.tm_hour);
        digit(8, 65, 55, 80, s[0], TFT_WHITE, Volkswagen100);
        digit(63, 65, 55, 80, s[1], TFT_WHITE, Volkswagen100);
        st.h = t.tm_hour;
    }
    if (t.tm_min != st.m) {
        snprintf(s, sizeof(s), "%02d", t.tm_min);
        // Height 64, not 70 as in the original: 95..165 overlapped the city row
        // (y 161) and every minute change clipped the top of the city name
        digit(118, 95, 43, 64, s[0], C_GRAY, Volkswagen80);
        digit(161, 95, 43, 64, s[1], C_GRAY, Volkswagen80);
        st.m = t.tm_min;
    }
    if (t.tm_sec != st.s) {
        snprintf(s, sizeof(s), "%02d", t.tm_sec);
        field(203, 95, 37, 30, s, C_ORANGE, Volkswagen30);
        st.s = t.tm_sec;
    }
    if (t.tm_mday != st.day || t.tm_mon != st.mon) {
        snprintf(s, sizeof(s), "%02d", t.tm_mday);
        field(80, 30, 40, 18, s, TFT_WHITE, Volkswagen19);
        field(20, 30, 40, 18, MONTHS[t.tm_mon], TFT_BLACK, Volkswagen19);
        st.day = t.tm_mday;
        st.mon = t.tm_mon;
    }
    if (t.tm_wday != st.wday) {
        field(180, 70, 40, 18, WDAYS[t.tm_wday], TFT_WHITE, Volkswagen19);
        st.wday = t.tm_wday;
    }
}

static void tickNormal() {
    tm t;
    time_t now = time(nullptr);
    if (now != st.lastT && localNow(t)) {
        st.lastT = now;
        normalClock(t);
    }
    if (net.weatherVersion() != st.weatherVer) normalWeather();
    if (net.iconVersion() != st.iconVer) {
        st.iconVer = net.iconVersion();
        drawIcon(net.weather().code);
    }
    if (millis() - st.lastPoll > 2000) {        // RSSI and IP — not every frame
        st.lastPoll = millis();
        int b = wifiBucket();
        if (b != st.wifi) { st.wifi = b; drawWifi(b, C_GRAY); }
        if ((uint32_t)WiFi.localIP() != st.ip) drawIp(15, TFT_WHITE);
    }
}

// ================================================================ CRYPTO

static CryptoInfo shown;   // what the rows currently show

static String formatRate(double rate) {
    int dec = rate >= 1000 ? 0 : rate >= 1 ? 2 : rate >= 0.01 ? 4 : rate >= 0.0001 ? 6 : 8;
    char raw[32];
    snprintf(raw, sizeof(raw), "%.*f", dec, rate);
    // At most 9 significant digits so it fits the column
    String out;
    int digits = 0;
    for (const char* p = raw; *p && digits < 9; p++) {
        if (isdigit((uint8_t)*p)) { out += *p; digits++; }
        else if (*p == '.') out += '.';
    }
    if (out.endsWith(".")) out.remove(out.length() - 1);
    return out;
}

// Rows 136..276, step 24: the last one stays clear of the hold bar (277..279).
// Columns: symbol 0..52, price 52..152, percent 152..240.
static constexpr int CR_TOP = 136, CR_STEP = 24, CR_H = 20;

static void cryptoRow(int i, const CryptoInfo& c) {
    int y = CR_TOP + i * CR_STEP;
    bool outer = batch;
    restore(0, y, W, CR_H);
    if (c.sym[i].length()) {
        bool have = c.rate[i] > 0;   // no price yet — a dash, not zeros
        char pct[12];
        if (have) snprintf(pct, sizeof(pct), "%+.2f%%", c.pct[i]);
        else strcpy(pct, "--");
        batch = true;   // three columns pushed as one row
        field(0, y, 52, CR_H, c.sym[i].c_str(), C_LINE, Volkswagen19, true);
        field(52, y, 100, CR_H, have ? formatRate(c.rate[i]).c_str() : "--", C_BEIGE, Volkswagen19, true);
        field(152, y, 88, CR_H, pct, C_LINE, Volkswagen19, true);
        batch = outer;
    }
    flush(0, y, W, CR_H);
}

static void cryptoRows(bool force) {
    CryptoInfo c = net.crypto();
    st.cryptoVer = net.cryptoVersion();
    for (int i = 0; i < CRYPTO_COUNT; i++) {
        if (force || c.sym[i] != shown.sym[i] || c.rate[i] != shown.rate[i] || c.pct[i] != shown.pct[i]) {
            cryptoRow(i, c);
        }
    }
    shown = c;
}

static void cryptoStatic() {
    bgKind = BG_CRYPTO;
    fb.fillRect(0, 0, W, 128, C_BROWN);
    fb.fillRect(0, 128, W, 4, C_LINE);
    fb.fillRect(0, 132, W, H - 132, C_DARK);
    saveBg();
}

static void cryptoClock(const tm& t) {
    const uint16_t cH = C_BEIGE, cM = C_LINE, cS = C_ORANGE;
    char s[4];
    if (t.tm_hour != st.h) {
        snprintf(s, sizeof(s), "%02d", t.tm_hour);
        digit(8, 20, 55, 80, s[0], cH, Volkswagen100);
        digit(63, 20, 55, 80, s[1], cH, Volkswagen100);
        st.h = t.tm_hour;
    }
    if (t.tm_min != st.m) {
        snprintf(s, sizeof(s), "%02d", t.tm_min);
        digit(118, 50, 43, 70, s[0], cM, Volkswagen80);
        digit(161, 50, 43, 70, s[1], cM, Volkswagen80);
        st.m = t.tm_min;
    }
    snprintf(s, sizeof(s), "%02d", t.tm_sec);
    field(203, 28, 37, 30, s, cS, Volkswagen30);   // 5 px below the Wi-Fi icon
    st.s = t.tm_sec;
}

static void tickCrypto() {
    tm t;
    time_t now = time(nullptr);
    if (now != st.lastT && localNow(t)) {
        st.lastT = now;
        cryptoClock(t);
    }
    if (net.cryptoVersion() != st.cryptoVer) cryptoRows(false);
    if (millis() - st.lastPoll > 2000) {
        st.lastPoll = millis();
        int b = wifiBucket();
        if (b != st.wifi) { st.wifi = b; drawWifi(b, TFT_BLACK); }
        if ((uint32_t)WiFi.localIP() != st.ip) drawIp(30, TFT_BLACK);
    }
}

// ================================================================ SPACE

static constexpr int NSTARS = 768;
static uint8_t sx[NSTARS], sy[NSTARS], sz[NSTARS];
static uint8_t za, zb, zc, zx;
static constexpr int SC_W = 170, SC_H = 50, SC_X = (W - SC_W) / 2, SC_Y = (H - SC_H) / 2;
static uint32_t lastFrame = 0;

static inline uint8_t rng() {
    zx++;
    za = za ^ zc ^ zx;
    zb = zb + za;
    zc = (zc + (zb >> 1)) ^ za;
    return zc;
}

static inline bool inClock(int x, int y) {
    return x >= SC_X && x < SC_X + SC_W && y >= SC_Y && y < SC_Y + SC_H;
}

static void spaceEnter() {
    bgKind = BG_BLACK;
    fb.fillSprite(TFT_BLACK);
    saveBg();
    za = random(256); zb = random(256); zc = random(256); zx = random(256);
    uint8_t depth = 255;
    for (int i = 0; i < NSTARS; i++) {
        sx[i] = rng();
        sy[i] = rng();
        sz[i] = depth--;
        if (!depth) depth = 255;
    }
}

static void tickSpace() {
    if (millis() - lastFrame < 20) return;   // ~50 fps
    lastFrame = millis();
    const int cx = W / 2, cy = H / 2;
    for (int i = 0; i < NSTARS; i++) {
        if (sz[i] <= 1) {                   // spawn a new star far away
            sx[i] = rng();
            sy[i] = rng();
            sz[i] = 255;
            continue;
        }
        int ox = ((int)sx[i] - cx) * 256 / sz[i] + cx;
        int oy = ((int)sy[i] - cy) * 256 / sz[i] + cy;
        if (ox >= 0 && ox < W && oy >= 0 && oy < H && !inClock(ox, oy)) fb.drawPixel(ox, oy, TFT_BLACK);
        sz[i] -= 2;
        if (sz[i] <= 1) continue;
        int x = ((int)sx[i] - cx) * 256 / sz[i] + cx;
        int y = ((int)sy[i] - cy) * 256 / sz[i] + cy;
        if (x >= 0 && x < W && y >= 0 && y < H) {
            uint8_t lum = 255 - sz[i];
            if (!inClock(x, y)) fb.drawPixel(x, y, rgb(lum, lum, lum));
        } else {
            sz[i] = 0;                      // flew off the edge
        }
    }
    time_t now = time(nullptr);
    tm t;
    if (now != st.lastT && localNow(t)) {
        st.lastT = now;
        char buf[9];
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
        batch = true;
        field(SC_X, SC_Y + 5, SC_W, SC_H - 5, buf, TFT_WHITE, Volkswagen30, true);
        batch = false;
    }
    overlayBar();
    flushAll();   // whole frame from PSRAM in one go — no flicker
    barDirty = false;
}

// ================================================================ ANALOG
// Sport-chronograph style: dark dial with radial shading, a bezel with a
// minute track and orange 5-minute marks, large 12/3/6/9, bevelled baton
// indices, a red dot above 12, month and weekday, a date window. Skeleton
// hands (outline + light lume) with a drop shadow. The dial is drawn once
// (and once a day for the new date) and kept in PSRAM; every second only the
// background is restored and the anti-aliased hands are drawn.

static constexpr int AN_CX = W / 2, AN_CY = H / 2;   // 120, 140
static constexpr int AN_R = 118;                      // outer bezel radius

static constexpr uint16_t A_EDGE    = rgb(14, 14, 15);
static constexpr uint16_t A_CENTER  = rgb(46, 46, 49);
static constexpr uint16_t A_BEZEL   = rgb(58, 58, 61);
static constexpr uint16_t A_BEZEL_D = rgb(28, 28, 30);
static constexpr uint16_t A_TICK    = rgb(120, 120, 124);
static constexpr uint16_t A_INDEX   = rgb(210, 210, 212);
static constexpr uint16_t A_INDEX_D = rgb(90, 90, 94);
static constexpr uint16_t A_NUM     = rgb(190, 190, 194);
static constexpr uint16_t A_TEXT    = rgb(200, 200, 204);
static constexpr uint16_t A_TEXT_D  = rgb(140, 140, 146);
static constexpr uint16_t A_HAND    = rgb(150, 150, 156);
static constexpr uint16_t A_LUME    = rgb(236, 236, 228);
static constexpr uint16_t A_SHADOW  = rgb(6, 6, 7);
static constexpr uint16_t A_RED     = rgb(225, 38, 38);

static const char* MONTHS_FULL[12] = {"JANUARY", "FEBRUARY", "MARCH", "APRIL", "MAY", "JUNE",
                                      "JULY", "AUGUST", "SEPTEMBER", "OCTOBER", "NOVEMBER", "DECEMBER"};
static const char* WDAYS_FULL[7] = {"SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY"};

// Point on a circle: clock degrees (0 = 12 o'clock, clockwise)
static inline float px(float deg, float r) { return AN_CX + sinf(deg * DEG_TO_RAD) * r; }
static inline float py(float deg, float r) { return AN_CY - cosf(deg * DEG_TO_RAD) * r; }

static uint16_t mix(uint16_t a, uint16_t b, float t) {   // linear a -> b
    return fb.alphaBlend((uint8_t)(t * 255), b, a);
}

static void textCentered(const char* s, int cx, int y, uint16_t col, const uint8_t* font) {
    useFont(font);
    fb.setTextColor(col, col, false);   // background from bgAt — the dial is already saved
    fb.drawString(s, cx - fb.textWidth(s) / 2, y);
}

static void analogDial(const tm& t) {
    bgKind = BG_BLACK;
    fb.fillSprite(TFT_BLACK);

    // Bezel: lighter outside, darker inside — a bevel effect
    for (int r = AN_R; r > AN_R - 12; r--)
        fb.fillCircle(AN_CX, AN_CY, r, mix(A_BEZEL, A_BEZEL_D, (AN_R - r) / 12.0f));
    // Dial: radial shading from centre to edge
    const int RD = AN_R - 12;
    for (int r = RD; r > 0; r -= 2)
        fb.fillCircle(AN_CX, AN_CY, r, mix(A_CENTER, A_EDGE, powf((float)r / RD, 1.6f)));
    saveBg();   // everything below anti-aliases against the real dial

    // Minute track on the bezel; 5-minute marks in orange
    for (int i = 0; i < 60; i++) {
        float d = i * 6;
        if (i % 5 == 0) {
            if (i == 0) continue;   // red dot above 12
            fb.drawWedgeLine(px(d, AN_R - 2), py(d, AN_R - 2), px(d, AN_R - 10), py(d, AN_R - 10),
                             1.6f, 1.2f, C_ORANGE);
        } else {
            fb.drawWideLine(px(d, AN_R - 3), py(d, AN_R - 3), px(d, AN_R - 8), py(d, AN_R - 8), 1.0f, A_TICK);
        }
    }
    fb.fillSmoothCircle(px(0, AN_R - 6), py(0, AN_R - 6), 3, A_RED);

    // Baton hour indices (except 12/3/6/9): light edge + dark centre
    for (int h = 1; h < 12; h++) {
        if (h % 3 == 0) continue;
        float d = h * 30;
        fb.drawWedgeLine(px(d, RD - 6), py(d, RD - 6), px(d, RD - 26), py(d, RD - 26), 3.4f, 3.4f, A_INDEX);
        fb.drawWedgeLine(px(d, RD - 9), py(d, RD - 9), px(d, RD - 23), py(d, RD - 23), 1.2f, 1.2f, A_INDEX_D);
    }
    // Large numerals
    textCentered("12", AN_CX, AN_CY - RD + 6, A_NUM, Volkswagen30);
    textCentered("3", AN_CX + RD - 20, AN_CY - 14, A_NUM, Volkswagen30);
    textCentered("9", AN_CX - RD + 20, AN_CY - 14, A_NUM, Volkswagen30);
    textCentered("6", AN_CX, AN_CY + RD - 34, A_NUM, Volkswagen30);

    // Month and weekday in the upper half
    textCentered(MONTHS_FULL[t.tm_mon], AN_CX, AN_CY - 52, A_TEXT, Volkswagen19);
    textCentered(WDAYS_FULL[t.tm_wday], AN_CX, AN_CY - 32, A_TEXT_D, Volkswagen19);

    // Date window above 6
    const int dw = 34, dh = 24, dx = AN_CX - dw / 2, dy = AN_CY + 44;
    fb.fillRoundRect(dx - 1, dy - 1, dw + 2, dh + 2, 4, A_INDEX_D);
    fb.fillRoundRect(dx, dy, dw, dh, 3, rgb(10, 10, 11));
    saveBg();
    char buf[4];
    snprintf(buf, sizeof(buf), "%d", t.tm_mday);
    textCentered(buf, AN_CX, dy + 3, A_LUME, Volkswagen19);

    saveBg();   // finished dial saved in PSRAM
}

// Skeleton hand: shadow, tapered outline, light lume inside
static void skeletonHand(float deg, float len, float tail, float wBase, float wTip, float lumeFrom) {
    const float sx = 2.0f, sy = 3.0f;   // shadow down and to the right
    fb.drawWedgeLine(px(deg + 180, tail) + sx, py(deg + 180, tail) + sy, px(deg, len) + sx, py(deg, len) + sy,
                     wBase, wTip, A_SHADOW);
    fb.drawWedgeLine(px(deg + 180, tail), py(deg + 180, tail), px(deg, len), py(deg, len), wBase, wTip, A_HAND);
    fb.drawWedgeLine(px(deg, len * lumeFrom), py(deg, len * lumeFrom), px(deg, len - 4), py(deg, len - 4),
                     wBase * 0.62f, wTip * 0.7f, A_LUME);
}

static void secondHand(float deg) {
    fb.drawWedgeLine(px(deg + 180, 22) + 1, py(deg + 180, 22) + 2, px(deg, AN_R - 16) + 1, py(deg, AN_R - 16) + 2,
                     1.3f, 0.6f, A_SHADOW);
    fb.drawWedgeLine(px(deg + 180, 22), py(deg + 180, 22), px(deg, AN_R - 16), py(deg, AN_R - 16), 1.3f, 0.6f, C_ORANGE);
    fb.fillSmoothCircle(px(deg + 180, 16), py(deg + 180, 16), 4, C_ORANGE);   // counterweight
}

static int analogDay = -1;

static void analogEnter() {
    analogDay = -1;   // tickAnalog draws the dial
    bgKind = BG_BLACK;
    fb.fillSprite(TFT_BLACK);
    saveBg();
}

static void tickAnalog() {
    time_t now = time(nullptr);
    tm t;
    if (now == st.lastT || !localNow(t)) return;   // once a second
    st.lastT = now;

    bool full = t.tm_mday != analogDay;
    if (full) {                                    // new day — new dial
        analogDay = t.tm_mday;
        analogDial(t);
    } else {
        restore(AN_CX - AN_R, AN_CY - AN_R, 2 * AN_R + 1, 2 * AN_R + 1);
    }

    float s = t.tm_sec, m = t.tm_min + s / 60.0f, h = (t.tm_hour % 12) + m / 60.0f;
    // Short tails: longer ones merged with shadows into a blob at the hub
    skeletonHand(h * 30, 62, 7, 4.8f, 2.2f, 0.32f);
    skeletonHand(m * 6, 96, 7, 3.8f, 1.6f, 0.30f);
    secondHand(s * 6);
    fb.fillSmoothCircle(AN_CX, AN_CY, 7, A_BEZEL);     // hub cap
    fb.fillSmoothCircle(AN_CX, AN_CY, 3, C_ORANGE);

    if (full) flushAll();
    else flush(AN_CX - AN_R, AN_CY - AN_R, 2 * AN_R + 1, 2 * AN_R + 1);
}

// ================================================================ PING
// One persistent task: created on the first entry into the mode, then it
// sleeps while the mode is inactive. Creating a task per entry used to leave
// dozens of them alive during fast switching.

static constexpr int PING_LINES = 14;
static struct {
    SemaphoreHandle_t mtx;
    TaskHandle_t task;
    volatile bool active;
    volatile uint32_t gen;      // bumped on every entry — stale results are dropped
    volatile uint32_t ver;
    String host;
    int sent, recv, minMs, maxMs;
    float avgMs;
    String lines[PING_LINES];   // [0] is the newest
} ping;
static uint32_t pingShown = 0;

static void pingTask(void*) {
    for (;;) {
        if (!ping.active || WiFi.status() != WL_CONNECTED) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        xSemaphoreTake(ping.mtx, portMAX_DELAY);
        String host = ping.host;
        uint32_t gen = ping.gen;
        xSemaphoreGive(ping.mtx);

        uint32_t t0 = millis();
        IPAddress ip;
        bool dnsOk = WiFi.hostByName(host.c_str(), ip) == 1;
        bool tcpOk = false, httpOk = false;
        if (dnsOk && ping.active) {
            WiFiClient cl;
            tcpOk = cl.connect(ip, 80, 3000);
            cl.stop();
        }
        if (tcpOk && ping.active) {
            WiFiClient cl;
            HTTPClient http;
            http.setConnectTimeout(3000);
            http.setTimeout(3000);
            if (http.begin(cl, "http://" + host + "/")) {
                httpOk = http.GET() > 0;
                http.end();
            }
        }
        int total = millis() - t0;

        xSemaphoreTake(ping.mtx, portMAX_DELAY);
        if (ping.active && ping.gen == gen) {
            ping.sent++;
            if (httpOk) {
                ping.recv++;
                if (ping.recv == 1) { ping.minMs = ping.maxMs = total; ping.avgMs = total; }
                else {
                    ping.minMs = min(ping.minMs, total);
                    ping.maxMs = max(ping.maxMs, total);
                    ping.avgMs += (total - ping.avgMs) / ping.recv;
                }
            }
            for (int i = PING_LINES - 1; i > 0; i--) ping.lines[i] = ping.lines[i - 1];
            char ln[64];
            if (httpOk) snprintf(ln, sizeof(ln), "from %s b=32, t=%d", ip.toString().c_str(), total);
            else snprintf(ln, sizeof(ln), "%s: timeout", dnsOk ? ip.toString().c_str() : host.c_str());
            ping.lines[0] = ln;
            ping.ver++;
        }
        xSemaphoreGive(ping.mtx);

        int wait = 1000 - (int)(millis() - t0);    // 1 measurement per second
        if (wait > 0) vTaskDelay(pdMS_TO_TICKS(wait));
    }
}

static void pingEnter() {
    if (!ping.mtx) ping.mtx = xSemaphoreCreateMutex();
    xSemaphoreTake(ping.mtx, portMAX_DELAY);
    ping.gen++;
    ping.host = settings.get().pingHost;
    ping.sent = ping.recv = ping.minMs = ping.maxMs = 0;
    ping.avgMs = 0;
    for (auto& l : ping.lines) l = "";
    ping.ver++;
    ping.active = true;
    xSemaphoreGive(ping.mtx);
    if (!ping.task) xTaskCreatePinnedToCore(pingTask, "ping", 8192, nullptr, 1, &ping.task, 0);
    pingShown = 0;
    bgKind = BG_BLACK;
    fb.fillSprite(TFT_BLACK);
    saveBg();
}

static void pingLeave() {
    ping.active = false;
}

static void tickPing() {
    if (!ping.mtx || ping.ver == pingShown) return;   // redraw only on new data
    xSemaphoreTake(ping.mtx, portMAX_DELAY);
    pingShown = ping.ver;
    int sent = ping.sent, recv = ping.recv, mn = ping.minMs, mx = ping.maxMs;
    int avg = (int)(ping.avgMs + 0.5f);
    String lines[PING_LINES];
    for (int i = 0; i < PING_LINES; i++) lines[i] = ping.lines[i];
    xSemaphoreGive(ping.mtx);

    useFont(nullptr);                    // built-in Font2
    fb.fillSprite(TFT_BLACK);
    fb.setTextColor(TFT_WHITE, TFT_BLACK);
    char buf[48];
    snprintf(buf, sizeof(buf), "S=%d  R=%d  L=%d", sent, recv, sent - recv);
    fb.drawString(buf, (W - fb.textWidth(buf, 2)) / 2, 0, 2);

    const int pad = 5, colW = (W - 2 * pad) / 4;
    char c[4][24];
    snprintf(c[0], 24, "(%d%% loss)", sent ? (sent - recv) * 100 / sent : 0);
    snprintf(c[1], 24, "Min=%d", mn);
    snprintf(c[2], 24, "Max=%d", mx);
    snprintf(c[3], 24, "Aver=%d", avg);
    for (int i = 0; i < 4; i++) {
        int x = pad + i * colW + (colW - fb.textWidth(c[i], 2)) / 2;
        fb.drawString(c[i], max(x, pad), 18, 2);
    }
    const int top = 36, lh = 16;
    for (int i = 0; i < PING_LINES && top + i * lh + lh <= H; i++) {
        fb.drawString(lines[i], 2, top + i * lh, 2);
    }
    overlayBar();
    flushAll();
    barDirty = false;
}

// ================================================================ MODERN MODES
// WEATHER and MARKETS share the analog clock's look: graphite gradient, cards
// with a light border, orange accent, Bahnschrift (DIN-like) font. The classic
// NORMAL and CRYPTO modes are left untouched.

static constexpr uint16_t M_TOP   = rgb(34, 36, 42);
static constexpr uint16_t M_BOT   = rgb(9, 10, 12);
static constexpr uint16_t M_CARD  = rgb(33, 35, 41);
static constexpr uint16_t M_EDGE  = rgb(50, 53, 61);
static constexpr uint16_t M_TRACK = rgb(46, 48, 55);
static constexpr uint16_t T_HI    = rgb(238, 238, 234);
static constexpr uint16_t T_MID   = rgb(168, 171, 178);
static constexpr uint16_t T_LO    = rgb(112, 116, 125);
static constexpr uint16_t C_UP    = rgb(72, 204, 124);
static constexpr uint16_t C_DOWN  = rgb(238, 86, 86);

static const char* MONTH_NAMES[12] = {"January", "February", "March", "April", "May", "June",
                                      "July", "August", "September", "October", "November", "December"};
static const char* WEEKDAY_NAMES[7] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};

// Text in a field with alignment: 0 left, 1 centre, 2 right
static void fieldA(int x, int y, int w, int h, const char* s, uint16_t fg, const uint8_t* font, int align) {
    restore(x, y, w, h);
    useFont(font);
    int tw = fb.textWidth(s);
    int tx = align == 0 ? x : align == 1 ? x + (w - tw) / 2 : x + w - tw;
    fb.setViewport(x, y, w, h, false);
    fb.setTextColor(fg, fg, false);
    fb.drawString(s, tx, y);
    fb.resetViewport();
    flush(x, y, w, h);
}

// Static text straight into the background (card labels) — before saveBg
static void bgText(int x, int y, const char* s, uint16_t fg, const uint8_t* font) {
    useFont(font);
    fb.setTextColor(fg, fg, false);
    fb.drawString(s, x, y);
}

static void modernBase() {
    bgKind = BG_MODERN;
    for (int y = 0; y < H; y++) fb.drawFastHLine(0, y, W, modernLut[y]);
}

static void card(int x, int y, int w, int h) {
    fb.fillRoundRect(x, y, w, h, 8, M_CARD);
    fb.drawRoundRect(x, y, w, h, 8, M_EDGE);
}

// ---------------------------------------------------------------- WEATHER

// Metric grid: 3 x 2 cards, 74x40, 5 px gaps
static constexpr int TL_W = 74, TL_H = 40, TL_X0 = 4, TL_GAP = 5, TL_Y0 = 192, TL_Y1 = 236;
static inline int tlX(int c) { return TL_X0 + c * (TL_W + TL_GAP); }
static inline int tlY(int r) { return r ? TL_Y1 : TL_Y0; }

static const char* TILE_LABEL[6] = {"HUMIDITY", "PRESSURE", "WIND, m/s", "RAIN", "UV INDEX", ""};

static void weatherStatic() {
    modernBase();
    for (int i = 0; i < 6; i++) card(tlX(i % 3), tlY(i / 3), TL_W, TL_H);
    fb.fillRoundRect(30, 89, 180, 3, 1, M_TRACK);          // seconds track
    saveBg();                                              // labels anti-alias against the cards
    for (int i = 0; i < 6; i++) bgText(tlX(i % 3) + 8, tlY(i / 3) + 5, TILE_LABEL[i], T_LO, BsTiny);
    saveBg();
}

static void tileValue(int i, const char* v, uint16_t col = T_HI) {
    fieldA(tlX(i % 3) + 8, tlY(i / 3) + 18, TL_W - 12, 20, v, col, BsMed, 0);
}

static void weatherData() {
    WeatherInfo w = net.weather();
    st.weatherVer = net.weatherVersion();
    char b[40];

    // City — top left
    String city = settings.get().city;
    if (city.isEmpty()) city = net.geo().city;
    if (city.isEmpty()) city = w.location;
    city.toUpperCase();
    fieldA(10, 5, 180, 16, city.c_str(), T_MID, BsTiny, 0);

    if (!w.valid) {
        fieldA(80, 120, 156, 44, "--°", T_HI, BsNum, 0);
        for (int i = 0; i < 6; i++) tileValue(i, "--", T_MID);
        return;
    }
    // Hero: temperature, condition, max/min, feels-like
    snprintf(b, sizeof(b), "%d°", (int)lroundf(w.temp));
    fieldA(80, 118, 96, 46, b, T_HI, BsNum, 0);
    fieldA(80, 166, 156, 22, w.text.c_str(), T_MID, BsMed, 0);
    snprintf(b, sizeof(b), "%d° / %d°", (int)lroundf(w.tMax), (int)lroundf(w.tMin));
    fieldA(150, 122, 84, 22, b, T_HI, BsMed, 2);
    snprintf(b, sizeof(b), "feels %d°", (int)lroundf(w.feels));
    fieldA(140, 146, 94, 16, b, T_LO, BsTiny, 2);

    // Cards
    snprintf(b, sizeof(b), "%d%%", (int)lroundf(w.humidity));
    tileValue(0, b);
    snprintf(b, sizeof(b), "%d", w.pressure);
    tileValue(1, b);
    snprintf(b, sizeof(b), "%.1f", w.wind);
    tileValue(2, b);
    fieldA(tlX(2) + 40, tlY(0) + 20, 28, 16, w.windDir.c_str(), T_LO, BsTiny, 2);   // direction
    snprintf(b, sizeof(b), "%d%%", w.rainChance);
    tileValue(3, b, w.rainChance >= 50 ? rgb(110, 170, 240) : T_HI);
    snprintf(b, sizeof(b), "%.0f", w.uv);
    tileValue(4, b, w.uv >= 6 ? C_ORANGE : T_HI);

    // Last card — the next sun event: sunrise before dawn, sunset after
    tm t;
    bool sunUp = true;
    if (localNow(t)) {
        char now5[6];
        snprintf(now5, sizeof(now5), "%02d:%02d", t.tm_hour, t.tm_min);
        sunUp = !(strcmp(now5, w.sunrise.c_str()) < 0 || strcmp(now5, w.sunset.c_str()) >= 0);
    }
    fieldA(tlX(2) + 8, tlY(1) + 5, TL_W - 12, 14, sunUp ? "SUNSET" : "SUNRISE", T_LO, BsTiny, 0);
    tileValue(5, (sunUp ? w.sunset : w.sunrise).c_str(), C_ORANGE);

    if (net.iconVersion() == st.iconVer) drawIcon(w.code, 8, 116);
}

static void weatherClock(const tm& t, bool full) {
    char b[48];
    if (full || t.tm_min != st.m) {
        snprintf(b, sizeof(b), "%02d:%02d", t.tm_hour, t.tm_min);
        fieldA(0, 20, W, 66, b, T_HI, BsClock, 1);
        st.m = t.tm_min;
    }
    if (full || t.tm_mday != st.day) {
        snprintf(b, sizeof(b), "%s, %s %d", WEEKDAY_NAMES[t.tm_wday], MONTH_NAMES[t.tm_mon], t.tm_mday);
        fieldA(0, 96, W, 22, b, T_MID, BsMed, 1);
        st.day = t.tm_mday;
    }
    // Seconds — a thin orange bar under the time
    restore(30, 89, 180, 3);
    fb.fillRoundRect(30, 89, 3 + t.tm_sec * 177 / 59, 3, 1, C_ORANGE);
    flush(30, 89, 180, 3);
}

static void tickWeather() {
    tm t;
    time_t now = time(nullptr);
    if (now != st.lastT && localNow(t)) {
        weatherClock(t, st.lastT == 0);
        st.lastT = now;
    }
    if (net.weatherVersion() != st.weatherVer) weatherData();
    if (net.iconVersion() != st.iconVer) {
        st.iconVer = net.iconVersion();
        drawIcon(net.weather().code, 8, 116);
    }
    if (millis() - st.lastPoll > 2000) {
        st.lastPoll = millis();
        int bk = wifiBucket();
        if (bk != st.wifi) { st.wifi = bk; drawWifi(bk, T_MID); }
    }
}

// ---------------------------------------------------------------- MARKETS

static constexpr int MK_Y0 = 34, MK_STEP = 40, MK_H = 37;
static constexpr int SP_X = 76, SP_W = 74, SP_H = 25;   // chart inside a row

// Turnover: 1.23B / 456M / 12K
static String compact(double v) {
    char b[16];
    if (v >= 1e9) snprintf(b, sizeof(b), "$%.2fB", v / 1e9);
    else if (v >= 1e6) snprintf(b, sizeof(b), "$%.0fM", v / 1e6);
    else if (v >= 1e3) snprintf(b, sizeof(b), "$%.0fK", v / 1e3);
    else snprintf(b, sizeof(b), "$%.0f", v);
    return b;
}

static void marketsStatic() {
    modernBase();
    for (int i = 0; i < CRYPTO_COUNT; i++) card(4, MK_Y0 + i * MK_STEP, W - 8, MK_H);
    saveBg();
    bgText(10, 8, "MARKETS", T_HI, BsMed);
    bgText(106, 14, "24h, USDT", T_LO, BsTiny);
    saveBg();
}

static void sparkline(int x, int y, int w, int h, const float* v, int n, uint16_t col) {
    if (n < 2) return;
    float mn = v[0], mx = v[0];
    for (int k = 1; k < n; k++) { mn = min(mn, v[k]); mx = max(mx, v[k]); }
    float span = mx - mn > 0 ? mx - mn : 1;
    auto X = [&](int k) { return x + (float)k * (w - 1) / (n - 1); };
    auto Y = [&](int k) { return y + (h - 1) - (v[k] - mn) / span * (h - 1); };
    // Fill under the line — same colour, faded into the card
    uint16_t fill = mix(M_CARD, col, 0.22f);
    for (int px = 0; px < w; px++) {
        float fk = (float)px * (n - 1) / (w - 1);
        int k = min((int)fk, n - 2);
        float yy = Y(k) + (Y(k + 1) - Y(k)) * (fk - k);
        fb.drawFastVLine(x + px, (int)yy, y + h - (int)yy, fill);
    }
    for (int k = 0; k < n - 1; k++) fb.drawWideLine(X(k), Y(k), X(k + 1), Y(k + 1), 1.6f, col);
    fb.fillSmoothCircle(X(n - 1), Y(n - 1), 2, col);
}

static void marketRow(int i, const CryptoInfo& c) {
    int y = MK_Y0 + i * MK_STEP;
    bool outer = batch;
    restore(4, y, W - 8, MK_H);
    batch = true;
    if (c.sym[i].length()) {
        bool have = c.rate[i] > 0;
        uint16_t col = !have ? T_MID : c.pct[i] >= 0 ? C_UP : C_DOWN;
        fieldA(12, y + 3, 62, 20, c.sym[i].c_str(), T_HI, BsMed, 0);
        fieldA(12, y + 22, 62, 14, have ? compact(c.turnover[i]).c_str() : "", T_LO, BsTiny, 0);
        if (have && c.sparkN[i] > 1) sparkline(SP_X, y + 6, SP_W, SP_H, c.spark[i], c.sparkN[i], col);
        char pct[16];
        if (have) snprintf(pct, sizeof(pct), "%+.2f%%", c.pct[i]);
        else pct[0] = 0;   // no data — a single dash in the price is enough
        fieldA(154, y + 3, 76, 20, have ? formatRate(c.rate[i]).c_str() : "--", T_HI, BsMed, 2);
        fieldA(154, y + 22, 76, 14, pct, col, BsTiny, 2);
    }
    batch = outer;
    flush(4, y, W - 8, MK_H);
}

static void marketsRows() {
    CryptoInfo c = net.crypto();
    st.cryptoVer = net.cryptoVersion();
    for (int i = 0; i < CRYPTO_COUNT; i++) marketRow(i, c);
}

static void tickMarkets() {
    tm t;
    time_t now = time(nullptr);
    if (now != st.lastT && localNow(t)) {
        st.lastT = now;
        if (t.tm_min != st.m) {
            char b[8];
            snprintf(b, sizeof(b), "%02d:%02d", t.tm_hour, t.tm_min);
            fieldA(160, 7, 72, 22, b, T_MID, BsMed, 2);
            st.m = t.tm_min;
        }
    }
    if (net.cryptoVersion() != st.cryptoVer) marketsRows();
}

// ================================================================ CAT

static void catPush() {
    fb.pushSprite(0, CAT_Y0, 0, CAT_Y0, W, CAT_Y1 - CAT_Y0);   // the hold bar is refreshed separately in tick()
}

// ================================================================ menu

static const char* MODE_NAMES[MODE_COUNT] = {"CLOCK", "CRYPTO", "SPACE", "ANALOG", "PING", "CAT", "WEATHER", "MARKETS"};

// ================================================================ public

bool Display::begin() {
    tft.init();
    tft.setRotation(0);
    tft.fillScreen(TFT_BLACK);

    for (int y = 0; y < H; y++) {
        float r = (float)y / H;
        gradLut[y] = rgb(169 + r * (78 - 169), 154 + r * (105 - 154), 131 + r * (119 - 131));
        modernLut[y] = rgb(34 - r * 25, 36 - r * 26, 42 - r * 30);   // M_TOP -> M_BOT
    }

    fb.setColorDepth(16);
    fbPix = (uint16_t*)fb.createSprite(W, H);   // ps_calloc when PSRAM is present
    if (!fbPix) {
        Serial.println("[Display] Not enough memory for the frame buffer!");
        return false;
    }
    // The background copy lives in PSRAM only — internal RAM is for Wi-Fi and TLS
    if (psramFound()) {
        bgPix = (uint16_t*)heap_caps_malloc(W * H * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    fb.setCallback(bgAt);
    Serial.printf("[Display] 240x280 frame in %s, background copy: %s\n",
                  esp_ptr_external_ram(fbPix) ? "PSRAM" : "internal RAM",
                  bgPix ? "PSRAM" : "none (fallback)");
    return true;
}

void Display::showConnecting(uint32_t elapsedMs) {
    static int dotsShown = -1;
    static int dotsX = 0;
    if (elapsedMs < 150 || dotsShown < 0) {
        gradientScreen();
        line(20, 100, "Connecting to Wi-Fi", TFT_WHITE);
        dotsX = 20 + textW("Connecting to Wi-Fi", Volkswagen19);
        dotsShown = 0;
        flushAll();
    }
    int dots = (elapsedMs / 500) % 4;
    if (dots != dotsShown) {
        dotsShown = dots;
        field(dotsX, 100, 30, 22, "..." + (3 - dots), TFT_WHITE, Volkswagen19);
    }
}

void Display::showAP(const String& ssid, const String& ip) {
    gradientScreen();
    line(20, 40, "Setup Mode", TFT_YELLOW);
    line(20, 80, ("SSID: " + ssid).c_str(), TFT_WHITE);
    line(20, 110, "No password required", TFT_WHITE);
    line(20, 140, ("IP: " + ip).c_str(), TFT_WHITE);
    line(20, 180, "Connect to WiFi", TFT_CYAN);
    line(20, 210, "and open the page:", TFT_CYAN);
    line(20, 240, ("http://" + ip).c_str(), TFT_GREEN);
    flushAll();
}

void Display::setMode(uint8_t mode) {
    if (_mode == MODE_PING) pingLeave();   // pingEnter below restarts it
    _menu = false;
    _mode = mode < MODE_COUNT ? mode : MODE_NORMAL;
    net.setCryptoActive(_mode == MODE_CRYPTO || _mode == MODE_MARKETS, _mode == MODE_MARKETS);
    resetState();

    batch = true;   // build the whole frame in PSRAM, push it in one go
    switch (_mode) {
        case MODE_NORMAL:
            normalStatic();
            st.iconVer = net.iconVersion();
            normalWeather();
            tickNormal();
            break;
        case MODE_CRYPTO:
            cryptoStatic();
            drawIp(30, TFT_BLACK);
            cryptoRows(true);
            tickCrypto();
            break;
        case MODE_SPACE:  spaceEnter();  break;
        case MODE_ANALOG: analogEnter(); tickAnalog(); break;
        case MODE_PING:   pingEnter();   break;
        case MODE_WEATHER:
            weatherStatic();
            st.iconVer = net.iconVersion();
            weatherData();
            tickWeather();
            break;
        case MODE_MARKETS:
            marketsStatic();
            marketsRows();
            tickMarkets();
            break;
        case MODE_CAT:
            bgKind = BG_BLACK;
            fb.fillSprite(TFT_BLACK);
            saveBg();
            CatMode::enter(&fb, catPush);
            break;
    }
    batch = false;
    flushAll();
    Serial.printf("[Display] Mode %u, free: RAM %u, PSRAM %u\n",
                  _mode, heap_caps_get_free_size(MALLOC_CAP_INTERNAL), ESP.getFreePsram());
}

void Display::showMenu(int sel) {
    if (!_menu && _mode == MODE_PING) pingLeave();   // no pinging while the menu is open
    _menu = true;
    bgKind = BG_DARK;
    fb.fillSprite(C_DARK);
    useFont(nullptr);
    fb.setTextColor(C_LINE, C_DARK);
    const char* hint = "tap: next    hold 1 s: select";
    fb.drawString(hint, (W - fb.textWidth(hint, 2)) / 2, 250, 2);
    const int top = 12, step = 29;   // 8 items above the hint (y 250)
    fb.fillRoundRect(12, top + sel * step - 4, W - 24, 27, 6, C_BROWN);
    saveBg();   // the highlight is part of the background so glyphs blend over it

    useFont(Volkswagen19);
    for (int i = 0; i < MODE_COUNT; i++) {
        fb.setTextColor(i == sel ? C_ORANGE : C_BEIGE, C_DARK, false);
        fb.drawString(MODE_NAMES[i], (W - fb.textWidth(MODE_NAMES[i])) / 2, top + i * step);
    }
    overlayBar();
    flushAll();
    barDirty = false;
}

void Display::setHoldBar(float p, bool white) {
    if (p < 0 && barP < 0) return;
    if (p >= 0) p = constrain(p, 0.0f, 1.0f);
    if (fabsf(p - barP) < 0.01f && white == barWhite) return;
    barP = p;
    barWhite = white;
    barDirty = true;
}

void Display::tick() {
    if (_menu) {
        if (barDirty) refreshBar();
        return;
    }
    switch (_mode) {
        case MODE_NORMAL: tickNormal(); break;
        case MODE_CRYPTO: tickCrypto(); break;
        case MODE_SPACE:  tickSpace();  break;
        case MODE_ANALOG: tickAnalog(); break;
        case MODE_PING:   tickPing();   break;
        case MODE_CAT:    CatMode::tick(); break;
        case MODE_WEATHER: tickWeather(); break;
        case MODE_MARKETS: tickMarkets(); break;
    }
    // Modes that don't push the whole frame get the bar separately
    if (barDirty && _mode != MODE_SPACE) refreshBar();
}

void Display::writeBmp(void (*sink)(const uint8_t*, size_t)) {
    uint8_t hdr[54] = {'B', 'M'};
    auto put32 = [&](int off, uint32_t v) { memcpy(hdr + off, &v, 4); };
    put32(2, BMP_SIZE);
    put32(10, 54);            // pixel data offset
    put32(14, 40);            // BITMAPINFOHEADER
    put32(18, W);
    put32(22, H);             // positive height — bottom-up rows
    hdr[26] = 1;              // planes
    hdr[28] = 24;             // bits per pixel
    put32(34, W * 3 * H);
    sink(hdr, sizeof(hdr));
    uint8_t row[W * 3];
    for (int y = H - 1; y >= 0; y--) {
        for (int x = 0; x < W; x++) {
            uint16_t c = swap16(fbPix[y * W + x]);
            row[x * 3 + 0] = (c << 3) & 0xF8;          // B
            row[x * 3 + 1] = (c >> 3) & 0xFC;          // G
            row[x * 3 + 2] = (c >> 8) & 0xF8;          // R
        }
        sink(row, sizeof(row));
    }
}
