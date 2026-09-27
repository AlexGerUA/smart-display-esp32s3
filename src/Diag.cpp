#include "Diag.h"
#include <WiFi.h>
#include <esp_system.h>
#include <esp_heap_caps.h>

static constexpr uint32_t MAGIC = 0x5D1A6E01;

// RTC_NOINIT: survives software reset, panic, WDT and brownout
RTC_NOINIT_ATTR static uint32_t rtcMagic;
RTC_NOINIT_ATTR static uint32_t rtcBoots;
RTC_NOINIT_ATTR static uint32_t rtcUptime;   // seconds of the current run

static esp_reset_reason_t reason;
static uint32_t prevUptime = 0;
static uint32_t lastTick = 0, lastBeat = 0;

void Diag::begin() {
    reason = esp_reset_reason();
    if (rtcMagic != MAGIC || reason == ESP_RST_POWERON) {
        rtcMagic = MAGIC;
        rtcBoots = 0;
        rtcUptime = 0;
    }
    prevUptime = rtcUptime;
    rtcBoots++;
    rtcUptime = 0;
}

int Diag::resetCode() { return (int)reason; }
uint32_t Diag::bootCount() { return rtcBoots; }
uint32_t Diag::prevUptimeSec() { return prevUptime; }

const char* Diag::resetName() {
    switch (reason) {
        case ESP_RST_POWERON:   return "power-on";
        case ESP_RST_EXT:       return "external pin";
        case ESP_RST_SW:        return "software";
        case ESP_RST_PANIC:     return "PANIC (crash)";
        case ESP_RST_INT_WDT:   return "interrupt watchdog";
        case ESP_RST_TASK_WDT:  return "task watchdog";
        case ESP_RST_WDT:       return "other watchdog";
        case ESP_RST_DEEPSLEEP: return "deep sleep";
        case ESP_RST_BROWNOUT:  return "BROWNOUT (power sag)";
        case ESP_RST_SDIO:      return "sdio";
        default:                return "unknown";
    }
}

String Diag::summary() {
    char buf[200];
    snprintf(buf, sizeof(buf),
             "boot #%u, reset: %s (%d), prev run %us, up %lus, RAM %u/%u KB (min %u), PSRAM %u KB, RSSI %d",
             rtcBoots, resetName(), (int)reason, prevUptime, millis() / 1000,
             heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024,
             heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024,
             heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024,
             heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024,
             WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0);
    return buf;
}

void Diag::loop() {
    uint32_t now = millis();
    if (now - lastTick >= 1000) {
        lastTick = now;
        rtcUptime = now / 1000;
    }
    if (now - lastBeat >= 30000) {   // heartbeat every 30 s
        lastBeat = now;
        Serial.printf("[Diag] %s\n", summary().c_str());
    }
}
