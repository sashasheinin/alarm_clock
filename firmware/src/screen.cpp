#include "screen.h"
#include <Arduino.h>
#include <time.h>
#include "board/board.h"
#include "mc_clock.h"
#include "timekeeping.h"

// Pixel-shift path: a 4 px square loop around the home position, so no pixel
// stays lit at exactly the same place for more than SHIFT_STEP_MS.
static const int8_t SHIFT[][2] = {{0, 0}, {2, 0}, {4, 0}, {4, 2}, {4, 4}, {2, 4},
                                  {0, 4}, {-2, 4}, {-4, 4}, {-4, 2}, {-4, 0}, {-4, -2},
                                  {-4, -4}, {-2, -4}, {0, -4}, {2, -4}, {4, -4}, {4, -2}};
#define SHIFT_COUNT ((int)(sizeof(SHIFT) / sizeof(SHIFT[0])))

static uint8_t  day_level  = 200;
static int      night_start = 22 * 60;   // minutes after midnight
static int      night_end   = 6 * 60;
static bool     dark       = false;
static uint8_t  applied    = 0;        // brightness currently on the panel
static uint32_t wake_until = 0;        // millis() until which a night wake lasts
static int      shift_idx  = 0;
static uint32_t shift_ms   = 0;

static bool is_night(void) {
    if (!time_is_valid()) return false;   // unknown time: never go dark
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    int m = t.tm_hour * 60 + t.tm_min;
    if (night_start == night_end) return false;
    if (night_start < night_end) return m >= night_start && m < night_end;
    return m >= night_start || m < night_end;          // crosses midnight
}

static void set_dark(bool d) {
    if (d == dark) return;
    dark = d;
    if (d) {
        mc_clock_sleep();
        display_set_brightness(0);
        display_power(false);
        applied = 0;
        Serial.printf("screen: dark at %lu ms\n", (unsigned long)millis());
    } else {
        display_power(true);
        mc_clock_wake();
        Serial.printf("screen: on at %lu ms\n", (unsigned long)millis());
    }
}

static void apply_brightness(uint8_t level) {
    if (level == applied) return;
    applied = level;
    display_set_brightness(level);
}

void screen_init(uint8_t day_brightness) {
    day_level = day_brightness;
    shift_ms = millis();
    screen_tick();
}

void screen_tick(void) {
    uint32_t now = millis();
    bool night = is_night();
    bool woken = (int32_t)(wake_until - now) > 0;

    if (night && !woken) {
        set_dark(true);
    } else {
        set_dark(false);
        apply_brightness(night ? NIGHT_BRIGHTNESS : day_level);
    }

    if (!dark && now - shift_ms >= SHIFT_STEP_MS) {
        shift_ms = now;
        shift_idx = (shift_idx + 1) % SHIFT_COUNT;
        mc_clock_set_offset(SHIFT[shift_idx][0], SHIFT[shift_idx][1]);
    }
}

bool screen_is_dark(void) { return dark; }

void screen_wake(void) {
    wake_until = millis() + NIGHT_WAKE_MS;
    screen_tick();
}

void screen_set_night(int start_min, int end_min) {
    if (start_min < 0 || start_min >= 24 * 60 || end_min < 0 || end_min >= 24 * 60) return;
    night_start = start_min;
    night_end = end_min;              // screen_tick() applies it on the next loop
}

void screen_set_day_brightness(uint8_t level) {
    day_level = level;
    screen_tick();
}

void screen_print_status(void) {
    Serial.printf("screen: %s, night %s, brightness %u, shift (%d,%d)\n",
                  dark ? "dark" : "on", is_night() ? "yes" : "no", applied,
                  SHIFT[shift_idx][0], SHIFT[shift_idx][1]);
}
