#pragma once
// Settings changed from the phone page, stored in NVS (namespace "settings").
// Serial "vol" stays a temporary test override; the saved volume is applied at boot.
#include <stdint.h>

#define PIN_MIN_LEN 4
#define PIN_MAX_LEN 8

void    settings_init(void);           // after sound_init(); applies the saved volume
uint8_t settings_volume(void);
void    settings_set_volume(uint8_t vol);   // saves and applies, 0..100

bool settings_pin_is_set(void);
bool settings_set_pin(const char* pin);     // 4-8 digits; "" clears it. Serial only.
bool settings_check_pin(const char* pin);   // false if no PIN is set
