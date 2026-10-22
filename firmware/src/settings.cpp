#include "settings.h"
#include <Arduino.h>
#include <Preferences.h>
#include <string.h>
#include "alarm.h"
#include "board/board.h"
#include "dismiss.h"
#include "mc_clock.h"
#include "screen.h"

#define STORE_VERSION 1

static Preferences prefs;
static settings_t  cur = {100, 60, ALARM_SNOOZE_MIN, ALARM_MAX_SNOOZES, 9, 1, 0xFF, 80, 22 * 60, 6 * 60};
static char        pin[PIN_MAX_LEN + 1] = "";

static bool valid(const settings_t* s) {
    unsigned all = (1u << mc_clock_face_count()) - 1;
    return s->volume <= 100 && s->ramp_s <= 300 && s->snooze_min >= 1 && s->snooze_min <= 30 &&
           s->snoozes <= 5 && s->math_max >= 5 && s->math_max <= 12 && s->math_count >= 1 &&
           s->math_count <= 5 && (s->faces & all) != 0 && s->brightness >= 10 && s->brightness <= 100 &&
           s->night_start < 24 * 60 && s->night_end < 24 * 60;
}

static void apply(void) {
    sound_set_volume(cur.volume);
    sound_set_ramp_ms(cur.ramp_s * 1000UL);
    alarm_set_snooze(cur.snooze_min, cur.snoozes);
    dismiss_set_problems(cur.math_max, cur.math_count);
    mc_clock_set_enabled(cur.faces);
    screen_set_day_brightness((uint8_t)(cur.brightness * 255 / 100));
    screen_set_night(cur.night_start, cur.night_end);
}

static void save(void) {
    prefs.putUChar("ver", STORE_VERSION);
    prefs.putBytes("all", &cur, sizeof(cur));
}

void settings_init(void) {
    prefs.begin("settings", false);
    settings_t s;
    if (prefs.getUChar("ver", 0) == STORE_VERSION && prefs.getBytesLength("all") == sizeof(s)) {
        prefs.getBytes("all", &s, sizeof(s));
        if (valid(&s)) cur = s;
        else Serial.println("settings: stored values out of range, using defaults");
    } else if (prefs.isKey("vol")) {
        cur.volume = prefs.getUChar("vol", 100);          // step 7a stored only the volume
        prefs.remove("vol");
        save();
    }
    prefs.getString("pin", pin, sizeof(pin));
    apply();
    settings_print();
}

const settings_t* settings_get(void) { return &cur; }

bool settings_set(const settings_t* s) {
    if (!valid(s)) return false;
    cur = *s;
    save();
    apply();
    return true;
}

uint8_t settings_volume(void) { return cur.volume; }

void settings_set_volume(uint8_t vol) {
    if (vol > 100) vol = 100;
    cur.volume = vol;
    save();
    sound_set_volume(vol);
}

void settings_print(void) {
    Serial.printf("settings: volume %u, ramp %u s, snooze %u min x%u, math 2-%u x%u, faces 0x%02x, "
                  "brightness %u%%, night %02u:%02u-%02u:%02u, PIN %s\n",
                  cur.volume, cur.ramp_s, cur.snooze_min, cur.snoozes, cur.math_max, cur.math_count,
                  cur.faces, cur.brightness, cur.night_start / 60, cur.night_start % 60, cur.night_end / 60,
                  cur.night_end % 60, pin[0] ? "set" : "NOT SET (serial: pin NNNN)");
}

bool settings_pin_is_set(void) { return pin[0] != 0; }

bool settings_set_pin(const char* p) {
    size_t n = strlen(p);
    if (n != 0 && (n < PIN_MIN_LEN || n > PIN_MAX_LEN)) return false;
    for (size_t i = 0; i < n; i++)
        if (p[i] < '0' || p[i] > '9') return false;
    strcpy(pin, p);
    prefs.putString("pin", pin);
    return true;
}

bool settings_check_pin(const char* p) { return pin[0] && strcmp(p, pin) == 0; }
