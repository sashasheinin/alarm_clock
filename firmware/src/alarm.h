#pragma once
// Alarm core. A list of up to ALARM_MAX alarms (time + weekdays + on/off + label),
// stored in NVS. Fires from the local clock only (never waits for the network) and
// never while the time is unknown. If the board was off at the alarm minute, it still
// fires up to ALARM_LATE_MIN late. The last handled alarm is saved as a minute stamp,
// so a reboot does not ring twice, and two alarms due at once ring only once.
// "Skip next" silences the next alarm due to ring, then clears itself.
// Snooze (shake/flip, gesture.cpp) stops the sound for a few minutes, a limited number of
// times per alarm (alarm_set_snooze). The snooze end is saved in NVS, so a reboot during
// a snooze still rings again. Only the math dismiss (or the timeout) ends the alarm.
// Call everything from loop() only (same thread as alarm_tick).
#include <stdint.h>
#include <time.h>

#define ALARM_MAX         8
#define ALARM_LABEL_LEN   16                    // including the terminating 0
#define ALARM_LATE_MIN    30                    // fire up to this many minutes late
#define ALARM_MAX_RING_MS (15UL * 60 * 1000)    // stop ringing by itself after this
#ifndef ALARM_SNOOZE_MIN                        // default; a test build can pass -DALARM_SNOOZE_MIN=1
#define ALARM_SNOOZE_MIN  5
#endif
#define ALARM_MAX_SNOOZES 2                     // default; both can be changed with alarm_set_snooze()

typedef struct {
    uint8_t enabled;
    uint8_t days;                    // bit 0 = Sunday ... bit 6 = Saturday
    int16_t minutes;                 // after midnight, 0..1439
    char    label[ALARM_LABEL_LEN];
} alarm_t;

void alarm_init(void);               // after timekeeping_init(); loads the list from NVS
void alarm_tick(void);               // call every loop
bool alarm_is_ringing(void);
bool alarm_is_active(void);          // ringing or snoozed: the alarm is not over yet
void alarm_dismiss(void);            // math problem (dismiss.cpp) or serial "stop"
bool alarm_snooze(void);             // false if not ringing or no snoozes left
int  alarm_snoozes_left(void);
void alarm_set_snooze(int minutes, int max_count);   // 1-30 min, 0-5 snoozes (0 = no snooze)
time_t alarm_snooze_until(void);     // 0 if not snoozed
void alarm_test(void);               // ring now (serial "alarm test")

int  alarm_count(void);
bool alarm_get(int i, alarm_t* out);
int  alarm_add(const alarm_t* a);    // returns the new index, or -1 if full / invalid
bool alarm_update(int i, const alarm_t* a);
bool alarm_remove(int i);            // later alarms move down one index

bool alarm_skip_next(bool on);       // false if there is no next alarm or the time is unknown
bool alarm_skip_is_set(void);
// Next alarm due to ring (time as minutes since the epoch, list index); false if none in 7 days.
bool alarm_next(int32_t* when_min, int* index);

void alarm_print(void);
