#include "alarm.h"
#include <Arduino.h>
#include <Preferences.h>
#include <time.h>
#include "board/board.h"
#include "screen.h"
#include "timekeeping.h"

// Default: 06:45 Sunday to Friday, Saturday off.
static int16_t schedule[7] = {6 * 60 + 45, 6 * 60 + 45, 6 * 60 + 45, 6 * 60 + 45,
                              6 * 60 + 45, 6 * 60 + 45, ALARM_OFF};
static int32_t  handled_day = -1;      // year * 1000 + yday of the last fired alarm
static bool     ringing     = false;
static uint32_t ring_start  = 0;
static uint32_t last_check  = 0;
static Preferences prefs;

static const char* const DAY[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

static void save_schedule(void) {
    prefs.putBytes("sched", schedule, sizeof(schedule));
}

void alarm_init(void) {
    prefs.begin("alarm", false);
    if (prefs.getBytesLength("sched") == sizeof(schedule))
        prefs.getBytes("sched", schedule, sizeof(schedule));
    else
        save_schedule();                // first boot: store the defaults
    handled_day = prefs.getInt("handled", -1);
    alarm_print();
}

static void start_ringing(const char* why) {
    if (ringing) return;
    ringing = true;
    ring_start = millis();
    if (!sound_alarm_start()) Serial.println("alarm: sound busy, retrying");
    screen_wake();
    Serial.printf("alarm: RINGING (%s)\n", why);
}

static void stop_ringing(const char* why) {
    if (!ringing) return;
    ringing = false;
    sound_alarm_stop();
    Serial.printf("alarm: stopped (%s) after %lu s\n", why, (millis() - ring_start) / 1000);
}

void alarm_tick(void) {
    uint32_t now_ms = millis();

    if (ringing) {
        screen_wake();                              // keep the screen on while ringing
        if (!sound_is_playing()) sound_alarm_start();   // e.g. a beep was playing when it fired
        if (now_ms - ring_start >= ALARM_MAX_RING_MS) stop_ringing("timeout");
        return;
    }

    if (now_ms - last_check < 1000) return;
    last_check = now_ms;
    if (!time_is_valid()) return;                   // never fire on an unknown time

    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    int32_t today = (t.tm_year + 1900) * 1000 + t.tm_yday;
    if (today == handled_day) return;

    int sched = schedule[t.tm_wday];
    if (sched == ALARM_OFF) return;
    int mins = t.tm_hour * 60 + t.tm_min;
    if (mins < sched || mins >= sched + ALARM_LATE_MIN) return;

    handled_day = today;
    prefs.putInt("handled", handled_day);           // a reboot now will not ring again
    start_ringing(mins == sched ? "on time" : "late");
}

bool alarm_is_ringing(void) { return ringing; }
void alarm_dismiss(void)    { stop_ringing("dismissed"); }
void alarm_test(void)       { start_ringing("test"); }

int alarm_get(int wday) { return (wday >= 0 && wday < 7) ? schedule[wday] : ALARM_OFF; }

void alarm_set(int wday, int minutes) {
    if (wday < 0 || wday > 6) return;
    if (minutes != ALARM_OFF && (minutes < 0 || minutes >= 24 * 60)) return;
    schedule[wday] = (int16_t)minutes;
    save_schedule();
    // Setting today's alarm to a later time should still ring today.
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    if (t.tm_wday == wday && minutes != ALARM_OFF && t.tm_hour * 60 + t.tm_min < minutes) {
        handled_day = -1;
        prefs.putInt("handled", -1);
    }
}

void alarm_print(void) {
    Serial.print("alarm:");
    for (int d = 0; d < 7; d++) {
        if (schedule[d] == ALARM_OFF) Serial.printf(" %s off", DAY[d]);
        else Serial.printf(" %s %02d:%02d", DAY[d], schedule[d] / 60, schedule[d] % 60);
    }
    Serial.printf("%s\n", ringing ? "  [RINGING]" : "");
}
