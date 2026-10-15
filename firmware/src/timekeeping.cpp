#include "timekeeping.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include <sys/time.h>
#include "board/board.h"

// Build with -DCLOCK_NO_WIFI to test the offline (RTC-only) path.
#if __has_include("secrets.h") && !defined(CLOCK_NO_WIFI)
#include "secrets.h"              // WIFI_SSID, WIFI_PASS (git-ignored, see secrets.example.h)
#endif

#define TZ_ISRAEL "IST-2IDT,M3.4.4/26,M10.5.0"

static TimeSource      source = TIME_NONE;
static volatile bool   ntp_synced_flag = false;    // set from the SNTP task, handled in tick
static uint32_t        last_ntp_ms = 0;

static void ntp_cb(struct timeval* tv) {
    (void)tv;
    ntp_synced_flag = true;
}

static void set_system_time_utc(const struct tm* utc) {
    // newlib has no timegm(), so convert with TZ briefly set to UTC.
    setenv("TZ", "UTC0", 1);
    tzset();
    struct tm copy = *utc;
    time_t t = mktime(&copy);
    setenv("TZ", TZ_ISRAEL, 1);
    tzset();
    struct timeval tv = { t, 0 };
    settimeofday(&tv, NULL);
}

static bool save_to_rtc(void) {
    time_t now = time(NULL);
    struct tm utc;
    gmtime_r(&now, &utc);
    return rtc_write(&utc);
}

bool time_is_valid(void) {
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    return t.tm_year + 1900 >= TIME_VALID_YEAR;
}

TimeSource time_source(void) { return source; }

const char* time_source_name(void) {
    switch (source) {
    case TIME_FROM_RTC:    return "rtc";
    case TIME_FROM_NTP:    return "ntp";
    case TIME_FROM_SERIAL: return "serial";
    default:               return "none";
    }
}

void timekeeping_init(void) {
    setenv("TZ", TZ_ISRAEL, 1);
    tzset();

    // 1. RTC first: the clock is right at boot with no network.
    struct tm rtc;
    bool lost = true;
    if (!rtc_present()) {
        Serial.println("time: RTC not found");
    } else if (!rtc_read(&rtc, &lost)) {
        Serial.println("time: RTC read failed");
    } else if (lost || rtc.tm_year + 1900 < TIME_VALID_YEAR) {
        Serial.println("time: RTC lost power, time unknown until Wi-Fi sync");
    } else {
        set_system_time_utc(&rtc);
        source = TIME_FROM_RTC;
        Serial.printf("time: loaded from RTC, %04d-%02d-%02d %02d:%02d:%02d UTC\n",
                      rtc.tm_year + 1900, rtc.tm_mon + 1, rtc.tm_mday,
                      rtc.tm_hour, rtc.tm_min, rtc.tm_sec);
    }

    // 2. Wi-Fi + SNTP in the background. Nothing waits on it.
#if defined(WIFI_SSID)
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    sntp_set_time_sync_notification_cb(ntp_cb);
    configTzTime(TZ_ISRAEL, "pool.ntp.org", "time.google.com");   // re-syncs hourly
    Serial.printf("time: Wi-Fi connecting to \"%s\"\n", WIFI_SSID);
#else
    Serial.println("time: Wi-Fi disabled (no secrets.h, or built with CLOCK_NO_WIFI)");
#endif
}

void timekeeping_tick(void) {
    if (!ntp_synced_flag) return;
    ntp_synced_flag = false;
    source = TIME_FROM_NTP;
    last_ntp_ms = millis();
    bool ok = save_to_rtc();
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    Serial.printf("time: NTP sync %02d:%02d:%02d local, RTC %s\n",
                  t.tm_hour, t.tm_min, t.tm_sec, ok ? "updated" : "write FAILED");
}

void timekeeping_set_manual(int hh, int mm, int ss) {
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    if (t.tm_year + 1900 < TIME_VALID_YEAR) {   // no date known: use 2026-01-01
        memset(&t, 0, sizeof(t));
        t.tm_year = 2026 - 1900;
        t.tm_mday = 1;
    }
    t.tm_hour = hh;
    t.tm_min = mm;
    t.tm_sec = ss;
    t.tm_isdst = -1;
    struct timeval tv = { mktime(&t), 0 };
    settimeofday(&tv, NULL);
    source = TIME_FROM_SERIAL;
    Serial.printf("time set to %02d:%02d:%02d local, RTC %s\n", hh, mm, ss,
                  save_to_rtc() ? "updated" : "write FAILED");
}

void timekeeping_print_status(void) {
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    Serial.printf("time: %04d-%02d-%02d %02d:%02d:%02d %s, valid %s, source %s\n",
                  t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec,
                  t.tm_isdst > 0 ? "IDT" : "IST", time_is_valid() ? "yes" : "no", time_source_name());
    if (last_ntp_ms) Serial.printf("time: last NTP sync %lu s ago\n", (millis() - last_ntp_ms) / 1000);

    struct tm rtc;
    bool lost;
    if (rtc_read(&rtc, &lost))
        Serial.printf("rtc: %04d-%02d-%02d %02d:%02d:%02d UTC%s\n", rtc.tm_year + 1900, rtc.tm_mon + 1,
                      rtc.tm_mday, rtc.tm_hour, rtc.tm_min, rtc.tm_sec, lost ? " (LOST POWER)" : "");
    else
        Serial.println("rtc: read failed");

#if defined(WIFI_SSID)
    if (WiFi.status() == WL_CONNECTED)
        Serial.printf("wifi: connected, ip %s, rssi %d\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    else
        Serial.printf("wifi: not connected (status %d)\n", (int)WiFi.status());
#endif
}
