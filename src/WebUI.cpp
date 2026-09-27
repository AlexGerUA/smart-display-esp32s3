// WebUI.cpp — web interface styled like the WEATHER/MARKETS screens: graphite,
// cards, orange accent, Bahnschrift (on Windows; system font otherwise).
//
// Pages are streamed (sendContent) instead of being built into one String.
// The live dashboard (/) pulls /screen.bmp to show what is on the display.
#include "WebUI.h"
#include "Settings.h"
#include "NetService.h"
#include "Diag.h"
#include "Display.h"
#include "Controls.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Update.h>
#include <esp_heap_caps.h>

WebUI web;

static WebServer server(80);
static DNSServer dns;
static bool      captive = false;
static uint32_t  restartAt = 0;   // 0 — no restart scheduled

static void scheduleRestart(uint32_t ms) { restartAt = (millis() + ms) | 1; }

// ?demo=1 — for documentation screenshots: network name, IP, MAC and nearby
// networks are replaced with placeholders so nothing personal ends up public
static bool demo = false;
static String pv(const String& real, const char* fake) { return demo ? String(fake) : real; }

// Mode titles and descriptions — same order as DisplayMode
static const char* MODE_TITLE[MODE_COUNT] = {"Clock", "Crypto", "Space", "Analog",
                                             "Ping", "Cat", "Weather", "Markets"};
static const char* MODE_DESC[MODE_COUNT] = {
    "Classic: time, date, weather",  "Classic: 6 prices and a clock",
    "Starfield flight and time",     "Chronograph with hands",
    "Latency to a site every second", "Eyes with random emotions",
    "Modern: all the weather data",  "Modern prices with a 24 h chart"};

// ------------------------------------------------------------------ style

static const char CSS[] PROGMEM = R"CSS(
:root{--bg:#0d0e11;--panel:#16181c;--card:#1e2126;--edge:#2c3038;--text:#eeeeea;--mid:#a8abb2;
--lo:#6f737c;--acc:#ed935f;--acc2:#f4b089;--up:#48cc7c;--down:#ee5656;--r:14px;color-scheme:dark}
*{box-sizing:border-box;margin:0;padding:0}
body{background:radial-gradient(1200px 600px at 50% -200px,#23262d,var(--bg));background-color:var(--bg);
color:var(--text);font:15px/1.5 Bahnschrift,'DIN Alternate','Segoe UI',system-ui,sans-serif;min-height:100vh;padding:0 16px 40px}
.wrap{max-width:820px;margin:0 auto}
header{display:flex;align-items:center;gap:12px;padding:22px 2px 14px}
.logo{width:34px;height:34px;border-radius:10px;background:linear-gradient(145deg,#2a2d34,#15171b);
border:1px solid var(--edge);display:grid;place-items:center}
.logo i{width:10px;height:10px;border-radius:50%;background:var(--acc);box-shadow:0 0 12px var(--acc)}
h1{font-size:18px;letter-spacing:.14em;font-weight:600}
.ver{margin-left:auto;font-size:12px;color:var(--lo);border:1px solid var(--edge);border-radius:99px;padding:3px 10px}
nav{display:flex;gap:6px;overflow-x:auto;padding-bottom:14px;scrollbar-width:none}
nav a{color:var(--mid);text-decoration:none;padding:8px 14px;border-radius:99px;border:1px solid transparent;white-space:nowrap}
nav a:hover{color:var(--text);border-color:var(--edge)}
nav a.on{color:#111;background:var(--acc);font-weight:600}
.card{background:linear-gradient(180deg,var(--card),var(--panel));border:1px solid var(--edge);border-radius:var(--r);
padding:18px;margin-bottom:14px}
.card h2{font-size:12px;letter-spacing:.12em;text-transform:uppercase;color:var(--lo);font-weight:600;margin-bottom:12px}
.grid{display:grid;gap:10px;grid-template-columns:repeat(auto-fill,minmax(150px,1fr))}
.tile{background:var(--panel);border:1px solid var(--edge);border-radius:12px;padding:10px 12px}
.tile small{display:block;font-size:11px;letter-spacing:.08em;text-transform:uppercase;color:var(--lo)}
.tile b{font-size:19px;font-weight:600;font-variant-numeric:tabular-nums}
.tile span{color:var(--mid);font-size:13px}
.dash{display:grid;grid-template-columns:auto 1fr;gap:14px;align-items:start}
.bezel{background:#050506;border:1px solid var(--edge);border-radius:22px;padding:12px;width:fit-content;
box-shadow:0 20px 40px rgba(0,0,0,.45),inset 0 0 0 1px #000}
.bezel img{display:block;width:240px;height:280px;border-radius:6px;image-rendering:pixelated;background:#000}
.live{font-size:12px;color:var(--lo);margin-top:8px;display:flex;align-items:center;gap:6px}
.live i{width:7px;height:7px;border-radius:50%;background:var(--up);box-shadow:0 0 8px var(--up)}
.modes{display:grid;gap:10px;grid-template-columns:repeat(auto-fill,minmax(170px,1fr))}
.mode{all:unset;cursor:pointer;background:var(--panel);border:1px solid var(--edge);border-radius:12px;padding:12px 14px;
transition:border-color .15s,transform .15s}
.mode:hover{border-color:var(--lo);transform:translateY(-1px)}
.mode b{display:block;font-size:16px;font-weight:600}
.mode span{font-size:12px;color:var(--mid)}
.mode.on{border-color:var(--acc);box-shadow:0 0 0 1px var(--acc) inset}
.mode.on b{color:var(--acc2)}
.chips{display:flex;flex-wrap:wrap;gap:6px}
.chip{all:unset;cursor:pointer;font-size:13px;padding:6px 12px;border-radius:99px;border:1px solid var(--edge);color:var(--mid)}
.chip.on{border-color:var(--acc);color:var(--acc2)}
label{display:block;font-size:12px;letter-spacing:.06em;text-transform:uppercase;color:var(--lo);margin:12px 0 6px}
input,select{width:100%;background:#0f1013;color:var(--text);border:1px solid var(--edge);border-radius:10px;
padding:11px 12px;font:inherit}
input:focus,select:focus{outline:none;border-color:var(--acc)}
.row{display:grid;gap:10px;grid-template-columns:repeat(auto-fill,minmax(120px,1fr))}
.chk{display:flex;align-items:center;gap:10px;margin-top:12px;color:var(--mid);font-size:14px}
.chk input{width:auto;accent-color:var(--acc)}
.chk label{all:unset}
.btn{all:unset;box-sizing:border-box;cursor:pointer;display:inline-block;text-align:center;padding:11px 18px;border-radius:10px;
background:var(--acc);color:#141414;font-weight:600;margin-top:14px}
.btn:hover{background:var(--acc2)}
.btn.ghost{background:transparent;color:var(--text);border:1px solid var(--edge)}
.btn.ghost:hover{border-color:var(--lo)}
.btn.danger{background:transparent;color:var(--down);border:1px solid #5a2a2a}
.btn.full{width:100%}
.inl{display:inline}
.note{color:var(--mid);font-size:13px;margin-top:10px}
.note a{color:var(--acc2)}
.bar{height:6px;background:var(--panel);border-radius:99px;overflow:hidden;margin-top:12px;border:1px solid var(--edge)}
.bar i{display:block;height:100%;width:0;background:var(--acc);transition:width .2s}
.ok{color:var(--up)}
footer{color:var(--lo);font-size:12px;text-align:center;margin-top:24px}
@media(max-width:640px){.dash{grid-template-columns:1fr}.bezel{margin:0 auto}}
)CSS";

// Switch modes without reloading the page (tiles and chips)
static const char MODE_JS[] PROGMEM = R"JS(<script>
document.querySelectorAll('[data-m]').forEach(b=>b.onclick=async()=>{
 await fetch('/mode',{method:'POST',body:new URLSearchParams({mode:b.dataset.m,ajax:1})});
 document.querySelectorAll('[data-m]').forEach(x=>x.classList.toggle('on',x.dataset.m===b.dataset.m));
 const s=document.getElementById('scr');if(s)setTimeout(()=>s.src='/screen.bmp?'+Date.now(),700)});
</script>)JS";

// ------------------------------------------------------------------ helpers

static void out(const char* s) { server.sendContent(s); }
static void out(const String& s) { server.sendContent(s); }

static String esc(const String& in) {
    String o;
    o.reserve(in.length() + 8);
    for (char ch : in) {
        switch (ch) {
            case '&': o += "&amp;"; break;
            case '<': o += "&lt;"; break;
            case '>': o += "&gt;"; break;
            case '\'': o += "&#39;"; break;
            case '"': o += "&quot;"; break;
            default: o += ch;
        }
    }
    return o;
}

static void pageBegin(const char* title, const char* tab) {
    demo = server.hasArg("demo");
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/html; charset=utf-8", "");
    out("<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'><title>");
    out(title);
    out(" · Smart Display</title><style>");
    out(FPSTR(CSS));
    out("</style></head><body><div class='wrap'><header><div class='logo'><i></i></div><h1>SMART DISPLAY</h1>"
        "<span class='ver'>v" PROJECT_VERSION "</span></header>");
    if (tab) {
        static const char* tabs[][2] = {{"/", "Dashboard"}, {"/mode", "Modes"}, {"/wifi", "Wi-Fi"},
                                        {"/crypto", "Crypto"}, {"/system", "System"}};
        out("<nav>");
        for (auto& t : tabs)
            out(String("<a href='") + t[0] + "'" + (strcmp(tab, t[0]) == 0 ? " class='on'" : "") + ">" + t[1] + "</a>");
        out("</nav>");
    }
}

static void pageEnd() {
    out("<footer>Smart Display v" PROJECT_VERSION " · ESP32-S3</footer></div></body></html>");
    server.sendContent("");
}

static void cardBegin(const char* title) { out(String("<section class='card'><h2>") + title + "</h2>"); }
static void cardEnd() { out("</section>"); }

static void tile(const char* label, const String& value, const String& sub = "") {
    out(String("<div class='tile'><small>") + label + "</small><b>" + esc(value) + "</b>" +
        (sub.length() ? "<br><span>" + esc(sub) + "</span>" : String("")) + "</div>");
}

// "Done" page with automatic redirect
static void donePage(const char* title, const String& msg, const char* back, int seconds) {
    pageBegin(title, nullptr);
    cardBegin(title);
    out("<p>" + msg + "</p>");
    if (seconds > 0) {
        out(String("<p class='note'>Returning in <span id='t'>") + seconds + "</span> s…</p>"
            "<script>let n=" + seconds + ";setInterval(()=>{if(--n<=0)location.href='" + back +
            "';else t.textContent=n},1000)</script>");
    } else {
        out(String("<a class='btn' href='") + back + "'>Back</a>");
    }
    cardEnd();
    pageEnd();
}

static String uptime() {
    uint32_t s = millis() / 1000;
    char b[32];
    if (s >= 86400) snprintf(b, sizeof(b), "%lu d %lu h", s / 86400, (s % 86400) / 3600);
    else snprintf(b, sizeof(b), "%lu h %lu min", s / 3600, (s % 3600) / 60);
    return b;
}

// ------------------------------------------------------------------ dashboard

static void handleRoot() {
    SettingsData s = settings.get();
    WeatherInfo w = net.weather();
    pageBegin("Dashboard", "/");

    out("<div class='dash'><div><div class='bezel'><img id='scr' src='/screen.bmp' alt='Display'></div>"
        "<div class='live'><i></i>Live display snapshot</div></div><div>");
    cardBegin("Status");
    out("<div class='grid'>");
    tile("Mode", MODE_TITLE[s.mode]);
    tile("Wi-Fi", pv(WiFi.SSID(), "HomeNetwork"), String(WiFi.RSSI()) + " dBm");
    tile("IP", pv(WiFi.localIP().toString(), "192.168.1.50"));
    tile("Weather", w.valid ? String((int)lroundf(w.temp)) + "°" : "—", w.valid ? w.text : "no key or no data");
    tile("Uptime", uptime());
    tile("Memory", String(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024) + " KB",
         "PSRAM " + String(ESP.getFreePsram() / 1024) + " KB");
    out("</div>");
    cardEnd();

    cardBegin("Quick switch");
    out("<div class='chips'>");
    for (int i = 0; i < MODE_COUNT; i++)
        out(String("<button class='chip") + (i == s.mode ? " on" : "") + "' data-m='" + i + "'>" + MODE_TITLE[i] + "</button>");
    out("</div>");
    cardEnd();
    out("</div></div>");
    out(FPSTR(MODE_JS));
    out("<script>setInterval(()=>{if(!document.hidden)scr.src='/screen.bmp?'+Date.now()},3000)</script>");
    pageEnd();
}

// ------------------------------------------------------------------ modes

static void handleModeForm() {
    SettingsData s = settings.get();
    pageBegin("Modes", "/mode");
    cardBegin("Display mode");
    out("<div class='modes'>");
    for (int i = 0; i < MODE_COUNT; i++)
        out(String("<button class='mode") + (i == s.mode ? " on" : "") + "' data-m='" + i + "'><b>" +
            MODE_TITLE[i] + "</b><span>" + MODE_DESC[i] + "</span></button>");
    out("</div><p class='note'>Modes switch instantly. On the device: tap — next mode, "
        "hold 2 s — menu.</p>");
    cardEnd();

    cardBegin("Ping");
    out("<form method='POST' action='/mode'><input type='hidden' name='mode' value='4'>"
        "<label for='ping'>Host to ping</label><input id='ping' name='ping' value='" + esc(s.pingHost) + "'>"
        "<button class='btn'>Save and start ping</button></form>");
    cardEnd();
    out(FPSTR(MODE_JS));
    pageEnd();
}

static void handleModeSubmit() {
    SettingsData s = settings.get();
    String m = server.hasArg("mode") ? server.arg("mode") : server.arg("display_mode");
    s.mode = m.toInt();
    if (server.arg("ping").length()) s.pingHost = server.arg("ping");
    settings.set(s);   // main.cpp switches the screen on the settings version bump
    if (server.hasArg("ajax")) {
        server.send(200, "application/json", "{\"ok\":true}");
        return;
    }
    donePage("Mode applied", String("Now showing: ") + MODE_TITLE[settings.get().mode] + ".", "/mode", 2);
}

// ------------------------------------------------------------------ Wi-Fi

static void handleWiFiForm() {
    SettingsData s = settings.get();
    int n = WiFi.scanNetworks();
    pageBegin("Wi-Fi", "/wifi");
    out("<form method='POST' action='/wifi'>");
    cardBegin("Network");
    out("<label for='ssid'>Network</label><select id='ssid' name='ssid'>");
    if (n <= 0) out("<option value=''>No networks found</option>");
    if (demo) n = min(n, 3);
    static const char* FAKE[] = {"HomeNetwork", "Neighbor_5G", "Guest"};
    for (int i = 0; i < n; i++) {
        String ssid = esc(pv(WiFi.SSID(i), FAKE[i % 3]));
        out("<option value='" + ssid + "'" + (WiFi.SSID(i) == s.ssid ? " selected" : "") + ">" + ssid + " · " +
            WiFi.RSSI(i) + " dBm" + (WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? " · open" : "") + "</option>");
    }
    WiFi.scanDelete();
    out("</select><label for='pw'>Password</label><input id='pw' type='password' name='password' "
        "placeholder='empty — keep current'>");
    cardEnd();

    cardBegin("City and time");
    out("<label for='city'>City for weather</label><input id='city' name='city' placeholder='empty — detect by IP' value='" +
        esc(s.city) + "'>");
    out(String("<div class='chk'><input type='checkbox' id='ta' name='tzauto'") + (s.tzAuto ? " checked" : "") +
        "><label for='ta'>Time zone automatically (by IP)</label></div>"
        "<div class='row'><div><label for='tz'>Fallback time zone</label><select id='tz' name='timezone'>");
    for (int i = -12; i <= 14; i++)
        out(String("<option value='") + i + "'" + (i == s.tzHours ? " selected" : "") + ">UTC" + (i >= 0 ? "+" : "") + i + "</option>");
    out(String("</select></div></div><div class='chk'><input type='checkbox' id='dst' name='dst'") + (s.dst ? " checked" : "") +
        "><label for='dst'>EU daylight saving rule</label></div>"
        "<button class='btn full'>Save and reconnect</button>");
    cardEnd();
    out("</form>");
    pageEnd();
}

static void handleWiFiSubmit() {
    SettingsData s = settings.get();
    String oldSsid = s.ssid;
    s.ssid = server.arg("ssid");
    // Empty field on the same network — keep the old password
    if (server.arg("password").length() || s.ssid != oldSsid) s.pass = server.arg("password");
    s.city = server.arg("city");
    s.tzHours = server.arg("timezone").toInt();
    s.tzAuto = server.hasArg("tzauto");
    s.dst = server.hasArg("dst");
    settings.set(s);
    donePage("Saved", "The device will reboot and join \"" + esc(s.ssid) + "\".", "/", 12);
    scheduleRestart(1500);
}

static void handleWiFiReset() {
    settings.clearWiFi();
    donePage("Wi-Fi reset", "The device will reboot into access point \"" AP_SSID "\".", "/", 12);
    scheduleRestart(1500);
}

// ------------------------------------------------------------------ crypto

static void handleCryptoForm() {
    SettingsData s = settings.get();
    CryptoInfo c = net.crypto();
    pageBegin("Crypto", "/crypto");
    cardBegin("Coins (USDT pairs on Bybit)");
    out("<form method='POST' action='/crypto'><div class='row'>");
    String list = s.cryptoList + ",,,,,,";
    int start = 0;
    for (int i = 0; i < CRYPTO_COUNT; i++) {
        int comma = list.indexOf(',', start);
        String sym = list.substring(start, comma);
        start = comma + 1;
        String now = (c.rate[i] > 0 && c.sym[i] == sym) ? "$" + String(c.rate[i], c.rate[i] < 1 ? 4 : 2) : "";
        out(String("<div><label for='c") + i + "'>#" + (i + 1) + (now.length() ? " · " + now : "") +
            "</label><input id='c" + i + "' name='c" + i + "' value='" + esc(sym) + "'></div>");
    }
    out(String("</div><label for='iv'>Refresh interval, seconds (30–900)</label><input id='iv' type='number' name='interval' "
               "min='30' max='900' value='") + (s.cryptoIntervalMs / 1000) + "'>"
        "<button class='btn'>Save</button></form>"
        " <form method='POST' action='/crypto_reset' class='inl' onsubmit=\"return confirm('Restore the default list?')\">"
        "<button class='btn ghost'>Default list</button></form>"
        "<p class='note'>A coin without a Bybit spot pair is shown as a dash.</p>");
    cardEnd();
    pageEnd();
}

static void handleCryptoSubmit() {
    SettingsData s = settings.get();
    String list;
    for (int i = 0; i < CRYPTO_COUNT; i++) {
        String sym = server.arg("c" + String(i));
        sym.trim();
        if (!sym.length()) continue;
        if (list.length()) list += ',';
        list += sym;
    }
    s.cryptoList = list;
    if (server.hasArg("interval")) s.cryptoIntervalMs = server.arg("interval").toInt() * 1000UL;
    settings.set(s);   // the network task picks it up without a reboot
    donePage("Saved", "Coins: " + esc(settings.get().cryptoList), "/crypto", 3);
}

static void handleCryptoReset() {
    SettingsData s = settings.get();
    s.cryptoList = DEFAULT_CRYPTO_LIST;
    settings.set(s);
    donePage("List restored", "Coins: " DEFAULT_CRYPTO_LIST, "/crypto", 3);
}

// ------------------------------------------------------------------ system

static void handleSystemForm() {
    SettingsData s = settings.get();
    pageBegin("System", "/system");

    cardBegin("Weather");
    // The key is never sent back to the page — only its last 4 characters as a hint
    String hint = s.weatherKey.length() >= 4 ? "saved ••••" + s.weatherKey.substring(s.weatherKey.length() - 4)
                                              : String("paste your key");
    if (demo && s.weatherKey.length()) hint = "saved ••••a1b2";
    out("<form method='POST' action='/system'><label for='wk'>WeatherAPI.com key</label>"
        "<input id='wk' name='wkey' autocomplete='off' placeholder='" + esc(hint) + "'>"
        "<button class='btn'>Save key</button></form>"
        "<p class='note'>Free key: <a href='https://www.weatherapi.com/my/' target='_blank'>weatherapi.com/my</a></p>");
    cardEnd();

    cardBegin("Firmware update");
    out(R"HTML(<input type='file' id='fw' accept='.bin' hidden>
<label for='fw' class='btn ghost' style='margin:0'>Choose firmware.bin</label> <span class='note' id='fn'></span>
<div class='bar'><i id='pb'></i></div>
<button class='btn' id='up'>Upload and update</button><p class='note' id='us'>firmware.bin from .pio/build/supermini</p>
<script>
fw.onchange=()=>{fn.textContent=fw.files[0]?fw.files[0].name+' · '+Math.round(fw.files[0].size/1024)+' KB':''};
up.onclick=()=>{const f=fw.files[0];if(!f){us.textContent='Choose a file first';return}
const x=new XMLHttpRequest(),d=new FormData();d.append('update',f);
x.upload.onprogress=e=>{pb.style.width=(e.loaded/e.total*100)+'%';us.textContent=Math.round(e.loaded/e.total*100)+'%'};
x.onload=()=>{us.innerHTML=x.status==200?'<span class=ok>Done, rebooting…</span>':'Update failed'};
x.open('POST','/update?ajax=1');x.send(d)};
</script>)HTML");
    cardEnd();

    cardBegin("Diagnostics");
    out("<div class='grid'>");
    tile("Last restart", Diag::resetName(), "boot #" + String(Diag::bootCount()));
    tile("Previous run", String(Diag::prevUptimeSec()) + " s");
    tile("Uptime", uptime());
    tile("Free RAM", String(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024) + " KB",
         "lowest " + String(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024) + " KB");
    tile("Free PSRAM", String(ESP.getFreePsram() / 1024) + " KB");
    tile("MAC", pv(WiFi.macAddress(), "AA:BB:CC:DD:EE:FF"));
    out("</div>");
    cardEnd();

    cardBegin("Reset");
    out("<form method='POST' action='/reboot' class='inl'><button class='btn ghost'>Reboot</button></form> "
        "<form method='POST' action='/wifi_reset' class='inl' onsubmit=\"return confirm('Reset Wi-Fi? The device will start an access point.')\">"
        "<button class='btn ghost'>Reset Wi-Fi</button></form> "
        "<form method='POST' action='/reset_all' class='inl' onsubmit=\"return confirm('Erase ALL settings?')\">"
        "<button class='btn danger'>Factory reset</button></form>");
    cardEnd();
    pageEnd();
}

static void handleSystemSubmit() {
    SettingsData s = settings.get();
    String k = server.arg("wkey");
    k.trim();
    if (k.length()) s.weatherKey = k;   // empty — keep the saved key
    settings.set(s);   // weather is re-requested right away
    donePage("Key saved", "Weather will refresh in a few seconds.", "/system", 3);
}

static void handleReboot() {
    donePage("Rebooting", "The device is rebooting.", "/", 10);
    scheduleRestart(1000);
}

static void handleResetAll() {
    settings.factoryReset();
    donePage("Settings erased", "The device will reboot into access point \"" AP_SSID "\".", "/", 12);
    scheduleRestart(1500);
}

static void handleUpdateDone() {
    bool ok = !Update.hasError();
    if (server.hasArg("ajax")) server.send(ok ? 200 : 500, "text/plain", ok ? "OK" : "FAIL");
    else donePage(ok ? "Updated" : "Error", ok ? "Firmware updated, rebooting." : "The update failed.", "/", ok ? 12 : 0);
    if (ok) scheduleRestart(1000);
}

static void handleUpdateUpload() {
    HTTPUpload& up = server.upload();
    if (up.status == UPLOAD_FILE_START) {
        Serial.printf("[OTA] %s\n", up.filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
    } else if (up.status == UPLOAD_FILE_WRITE) {
        if (Update.write(up.buf, up.currentSize) != up.currentSize) Update.printError(Serial);
    } else if (up.status == UPLOAD_FILE_END) {
        if (Update.end(true)) Serial.printf("[OTA] OK, %u bytes\n", up.totalSize);
        else Update.printError(Serial);
    }
}

// ------------------------------------------------------------------ API

static void handleScreen() {
    server.setContentLength(Display::BMP_SIZE);
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "image/bmp", "");
    display.writeBmp([](const uint8_t* d, size_t n) { server.sendContent((const char*)d, n); });
}

static void handleStatus() {
    SettingsData s = settings.get();
    WeatherInfo w = net.weather();
    char b[420];
    snprintf(b, sizeof(b),
             "{\"version\":\"%s\",\"mode\":%u,\"modeName\":\"%s\",\"ssid\":\"%s\",\"rssi\":%d,\"ip\":\"%s\","
             "\"uptime\":%lu,\"ramKB\":%u,\"psramKB\":%u,\"reset\":\"%s\",\"boot\":%u,"
             "\"weather\":{\"valid\":%s,\"temp\":%.1f,\"location\":\"%s\"}}",
             PROJECT_VERSION, s.mode, MODE_TITLE[s.mode], esc(WiFi.SSID()).c_str(), WiFi.RSSI(),
             WiFi.localIP().toString().c_str(), millis() / 1000, heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024,
             ESP.getFreePsram() / 1024, Diag::resetName(), Diag::bootCount(), w.valid ? "true" : "false", w.temp,
             esc(w.location).c_str());
    server.send(200, "application/json", b);
}

// Open the menu on the display (documentation screenshots); it closes after 15 s
static void handleMenu() {
    controls.openMenu(server.arg("sel").toInt());
    server.send(200, "application/json", "{\"ok\":true}");
}

static void handleNotFound() {
    if (captive) {
        // In access point mode any address redirects to the Wi-Fi page
        server.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/wifi", true);
        server.send(302, "text/plain", "");
        return;
    }
    server.send(404, "text/plain", "Not found");
}

// ------------------------------------------------------------------ lifecycle

void WebUI::begin() {
    server.on("/", HTTP_GET, handleRoot);
    server.on("/mode", HTTP_GET, handleModeForm);
    server.on("/mode", HTTP_POST, handleModeSubmit);
    server.on("/wifi", HTTP_GET, handleWiFiForm);
    server.on("/wifi", HTTP_POST, handleWiFiSubmit);
    server.on("/wifi_reset", HTTP_POST, handleWiFiReset);
    server.on("/crypto", HTTP_GET, handleCryptoForm);
    server.on("/crypto", HTTP_POST, handleCryptoSubmit);
    server.on("/crypto_reset", HTTP_POST, handleCryptoReset);
    server.on("/system", HTTP_GET, handleSystemForm);
    server.on("/system", HTTP_POST, handleSystemSubmit);
    server.on("/reboot", HTTP_POST, handleReboot);
    server.on("/reset_all", HTTP_POST, handleResetAll);
    server.on("/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
    server.on("/screen.bmp", HTTP_GET, handleScreen);
    server.on("/api/status", HTTP_GET, handleStatus);
    server.on("/api/menu", HTTP_POST, handleMenu);
    server.onNotFound(handleNotFound);
    server.begin();
    Serial.println("[Web] Server on port 80");
}

void WebUI::startCaptivePortal() {
    dns.start(53, "*", WiFi.softAPIP());
    captive = true;
}

void WebUI::stopCaptivePortal() {
    if (!captive) return;
    dns.stop();
    captive = false;
}

void WebUI::loop() {
    if (captive) dns.processNextRequest();
    server.handleClient();
    if (restartAt && (int32_t)(millis() - restartAt) >= 0) {
        Serial.println("[System] Restarting...");
        Serial.flush();
        delay(100);
        ESP.restart();
    }
}
