#pragma once
// Alarm core. One alarm time per weekday, stored in NVS. Fires from the local
// clock only (never waits for the network) and never while the time is unknown.
// If the board was off at the alarm minute, it still fires up to ALARM_LATE_MIN late.
// The date of the last handled alarm is saved, so a reboot does not ring twice.
#include <stdint.h>

#define ALARM_OFF        (-1)
#define ALARM_LATE_MIN   30                     // fire up to this many minutes late
#define ALARM_MAX_RING_MS (15UL * 60 * 1000)    // stop ringing by itself after this

void alarm_init(void);                 // after timekeeping_init(); loads schedule from NVS
void alarm_tick(void);                 // call every loop
bool alarm_is_ringing(void);
void alarm_dismiss(void);              // step 5: BOOT button / serial; step 6: math problem
void alarm_test(void);                 // ring now (serial "alarm test")
int  alarm_get(int wday);              // minutes after midnight, or ALARM_OFF; wday 0 = Sunday
void alarm_set(int wday, int minutes); // saves to NVS
void alarm_print(void);
