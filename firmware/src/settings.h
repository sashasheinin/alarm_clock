#pragma once
// Settings changed from the phone page, stored in NVS (namespace "settings") and applied
// to the modules through their setters. Serial "vol" and PWR brightness steps are
// temporary overrides; the saved values come back at boot.
#include <stdint.h>

#define PIN_MIN_LEN 4
#define PIN_MAX_LEN 8

typedef struct {
    uint8_t  volume;        // 0-100
    uint16_t ramp_s;        // 0-300: alarm gets louder over this long (0 = full at once)
    uint8_t  snooze_min;    // 1-30
    uint8_t  snoozes;       // 0-5 per alarm
    uint8_t  math_max;      // 5-12: largest factor
    uint8_t  math_count;    // 1-5 problems in a row
    uint8_t  faces;         // bit per face in the shuffle, at least one
    uint8_t  brightness;    // 10-100 % in the day
    uint16_t night_start;   // minutes after midnight; start == end = no night
    uint16_t night_end;
} settings_t;

void settings_init(void);              // after screen_init(), before alarm_init(): loads and applies
const settings_t* settings_get(void);
bool settings_set(const settings_t* s);     // validates, saves, applies; false if out of range
uint8_t settings_volume(void);
void    settings_set_volume(uint8_t vol);   // saves and applies, 0..100
void    settings_print(void);

bool settings_pin_is_set(void);
bool settings_set_pin(const char* pin);     // 4-8 digits; "" clears it. Serial only.
bool settings_check_pin(const char* pin);   // false if no PIN is set
