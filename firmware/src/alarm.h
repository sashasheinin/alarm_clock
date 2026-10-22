#pragma once
// Alarm core. A list of up to ALARM_MAX alarms (time + weekdays + on/off + label),
// stored in NVS. Fires from the local clock only (never waits for the network) and
// never while the time is unknown. If the board was off at the alarm minute, it still
// fires up to ALARM_LATE_MIN late. The last handled alarm is saved as a minute stamp,
// so a reboot does not ring twice, and two alarms due at once ring only once.
// "Skip next" silences the next alarm due to ring, then clears itself.
// Call everything from loop() only (same thread as alarm_tick).
#include <stdint.h>

#define ALARM_MAX         8
#define ALARM_LABEL_LEN   16                    // including the terminating 0
#define ALARM_LATE_MIN    30                    // fire up to this many minutes late
#define ALARM_MAX_RING_MS (15UL * 60 * 1000)    // stop ringing by itself after this

typedef struct {
    uint8_t enabled;
    uint8_t days;                    // bit 0 = Sunday ... bit 6 = Saturday
    int16_t minutes;                 // after midnight, 0..1439
    char    label[ALARM_LABEL_LEN];
} alarm_t;

void alarm_init(void);               // after timekeeping_init(); loads the list from NVS
void alarm_tick(void);               // call every loop
bool alarm_is_ringing(void);
void alarm_dismiss(void);            // step 5: BOOT button / serial; step 6: math problem
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
