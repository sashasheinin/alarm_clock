#pragma once
// Burn-in protection and night mode for the AMOLED.
//  - Night (NIGHT_START_HOUR..NIGHT_END_HOUR): panel off, face deleted. A tap or
//    PWR press turns it on dim for NIGHT_WAKE_MS. Never dark while time is unknown.
//  - Pixel shift: the face moves a few px on a cycle, one step every SHIFT_STEP_MS.
#include <stdint.h>

#define NIGHT_START_HOUR  22
#define NIGHT_END_HOUR    6
#define NIGHT_WAKE_MS     15000
#define NIGHT_BRIGHTNESS  40
#define SHIFT_STEP_MS     (2 * 60 * 1000)

void screen_init(uint8_t day_brightness);
void screen_tick(void);                       // call every loop
bool screen_is_dark(void);
void screen_wake(void);                       // tap / PWR / (later) alarm
void screen_set_day_brightness(uint8_t level);
void screen_print_status(void);
