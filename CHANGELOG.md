# Changelog

## 2.1.0 — PlatformIO rewrite

A full rewrite of the original Arduino IDE sketch (1.0.x). Same hardware idea, same web
setup, but a different build system, flash layout and feature set. **Read "Upgrading from
1.0.x" before you flash a device that already runs the old firmware.**

### Upgrading from 1.0.x — read this first

- **Web update (OTA) from 1.0.x does not work.** The new firmware is ~1.7 MB and uses a
  different partition table (two 1.9 MB app slots). The old firmware has ~1.3 MB slots and
  will reject the file. Flash once over USB with `firmware/smart-display-full.bin` at
  address **0x0** (see "Flash the firmware" in the README). After that, web updates work
  as before, with `smart-display-ota.bin`.
- **The USB flash erases all settings.** Wi-Fi, city, crypto list, ping host and the
  WeatherAPI key are wiped. The device boots into the `SmartDisplay` access point — set it
  up again. Write down your WeatherAPI key beforehand (the old web page shows it; the new
  one only shows the last 4 characters).
- **Build with PlatformIO, not Arduino IDE.** The display pins and driver settings now
  live in `platformio.ini`; your old `TFT_eSPI/User_Setup.h` is ignored.
- **Check the wiring against `docs/wiring.png`.** Pins are fixed in `platformio.ini`:
  CS 13, DC 12, MOSI 11, SCLK 10, BL 9, **RES 8**. If your display's RES was tied to
  3V3, wire it to GPIO 8.
- **The UI is in English now** (display, web pages, logs).
- **Time zone is detected by IP by default.** The old manual time zone + DST checkbox is
  now the fallback. Untick "Time zone automatically" on the Wi-Fi page to force it.
- **Default crypto list changed** to BTC, ETH, BNB, SOL, XRP, ADA. The refresh interval
  is limited to 30–900 s.

### New

- Three new display modes: **Cat** (animated eyes with random emotions), **Weather**
  (city, big clock, 6 weather cards) and **Markets** (6 coins with 24 h charts).
- **Optional touch button** (TTP223 module on GPIO 6): tap for the next mode, hold 2 s
  for the on-screen menu. The firmware works without it.
- **New web interface**: dashboard with a live display snapshot, quick mode switch,
  diagnostics (last restart reason, boot counter, free memory), factory reset.
- **JSON API**: `/api/status`, `/screen.bmp` (current frame).
- **Browser flashing** without installing any software (esptool-js), see the README.
- Works on boards without PSRAM (4 MB flash is enough).

### Fixed

- Weather was always requested for Kyiv; the saved city was ignored.
- Time was 1 hour off in summer (daylight saving was applied twice). Time now uses SNTP
  with proper POSIX time zone rules.
- The crypto refresh interval was saved in one format and read in another, so it always
  fell back to the default.
- A crypto list shorter than 6 coins left old coins in the empty slots.
- The WeatherAPI key is no longer sent back to the browser in full.

### Removed

- The activation / licence check and everything tied to it (server calls, trial period,
  LittleFS file). The firmware has no external dependencies besides WeatherAPI, ip-api
  and Bybit.

### License

The project is now licensed under **AGPL-3.0** (see `LICENSE`), the same license as the
cat eyes engine it builds on.
