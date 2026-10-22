#include "alarm.h"
#include <Arduino.h>
#include <Preferences.h>
#include <string.h>
#include <time.h>
#include "board/board.h"
#include "screen.h"
#include "timekeeping.h"

// NVS layout. Bump STORE_VERSION when alarm_t or this struct changes.
#define STORE_VERSION 1
typedef struct {
    uint8_t version;
    uint8_t count;
    alarm_t list[ALARM_MAX];
} store_t;

static store_t  store;
static int32_t  handled_min = 0;       // minute stamp of the last handled alarm (rung or skipped)
static int32_t  skip_min    = 0;       // alarms due at or before this minute are skipped; 0 = none
static bool     ringing     = false;
static uint32_t ring_start  = 0;
static uint32_t last_check  = 0;
static Preferences prefs;

static const char* const DAY[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

static void save_list(void) { prefs.putBytes("list", &store, sizeof(store)); }

static void save_handled(void) { prefs.putInt("handled_m", handled_min); }

static void save_skip(void) { prefs.putInt("skip_m", skip_min); }

static int32_t now_min(void) { return (int32_t)(time(NULL) / 60); }

// Minute stamp of `minutes` after local midnight, `day_offset` days after the date in `today`.
// Goes through mktime so a DST change between now and then is handled.
static int32_t occurrence_min(const struct tm* today, int day_offset, int minutes) {
    struct tm t = *today;
    t.tm_mday += day_offset;
    t.tm_hour = minutes / 60;
    t.tm_min = minutes % 60;
    t.tm_sec = 0;
    t.tm_isdst = -1;
    return (int32_t)(mktime(&t) / 60);
}

static bool valid(const alarm_t* a) {
    return a->minutes >= 0 && a->minutes < 24 * 60 && (a->days & 0x80) == 0;
}

// The old format (step 5) was int16_t[7], one time per weekday, -1 = off.
// Convert it to one alarm per distinct time.
static void migrate_old_schedule(void) {
    int16_t old[7];
    if (prefs.getBytesLength("sched") != sizeof(old)) return;
    prefs.getBytes("sched", old, sizeof(old));
    for (int d = 0; d < 7; d++) {
        if (old[d] < 0 || old[d] >= 24 * 60) continue;
        int i = 0;
        while (i < store.count && store.list[i].minutes != old[d]) i++;
        if (i == store.count) {
            memset(&store.list[i], 0, sizeof(alarm_t));
            store.list[i].enabled = 1;
            store.list[i].minutes = old[d];
            store.count++;
        }
        store.list[i].days |= 1 << d;
    }
    // The old "handled" was year * 1000 + yday. If today's alarm was already handled,
    // treat everything up to now as handled.
    int32_t old_handled = prefs.getInt("handled", -1);
    if (time_is_valid()) {
        time_t now = time(NULL);
        struct tm t;
        localtime_r(&now, &t);
        if (old_handled == (t.tm_year + 1900) * 1000 + t.tm_yday) handled_min = now_min();
    }
    prefs.remove("sched");
    prefs.remove("handled");
    Serial.printf("alarm: converted old weekday schedule to %d alarm(s)\n", store.count);
}

void alarm_init(void) {
    prefs.begin("alarm", false);
    memset(&store, 0, sizeof(store));
    handled_min = 0;
    skip_min = 0;
    if (prefs.getBytesLength("list") == sizeof(store)) {
        prefs.getBytes("list", &store, sizeof(store));
        if (store.version != STORE_VERSION || store.count > ALARM_MAX) {
            Serial.println("alarm: stored list has an unknown format, starting empty");
            memset(&store, 0, sizeof(store));
        }
        handled_min = prefs.getInt("handled_m", 0);
        skip_min = prefs.getInt("skip_m", 0);
    } else if (prefs.isKey("sched")) {
        migrate_old_schedule();
    } else {
        // First boot: 06:45 Sunday to Friday.
        store.count = 1;
        store.list[0].enabled = 1;
        store.list[0].days = 0x3F;
        store.list[0].minutes = 6 * 60 + 45;
        strcpy(store.list[0].label, "School");
    }
    store.version = STORE_VERSION;
    for (int i = 0; i < store.count; i++) store.list[i].label[ALARM_LABEL_LEN - 1] = 0;
    save_list();
    save_handled();
    save_skip();
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
    int mins = t.tm_hour * 60 + t.tm_min;

    // Of the alarms due now (on time or up to ALARM_LATE_MIN late) and not yet handled,
    // take the latest. Several due at once (board was off) ring once.
    int32_t due = 0;
    int idx = -1;
    for (int i = 0; i < store.count; i++) {
        const alarm_t* a = &store.list[i];
        if (!a->enabled || !(a->days & (1 << t.tm_wday))) continue;
        if (mins < a->minutes || mins >= a->minutes + ALARM_LATE_MIN) continue;
        int32_t occ = occurrence_min(&t, 0, a->minutes);
        if (occ > handled_min && occ > due) {
            due = occ;
            idx = i;
        }
    }
    if (idx < 0) return;

    handled_min = due;
    save_handled();                                 // a reboot now will not ring again
    const alarm_t* a = &store.list[idx];
    if (due <= skip_min) {
        skip_min = 0;
        save_skip();
        Serial.printf("alarm: %02d:%02d \"%s\" skipped (skip next)\n", a->minutes / 60,
                      a->minutes % 60, a->label);
        return;
    }
    char why[48];
    snprintf(why, sizeof(why), "%s, %02d:%02d \"%s\"", mins == a->minutes ? "on time" : "late",
             a->minutes / 60, a->minutes % 60, a->label);
    start_ringing(why);
}

bool alarm_is_ringing(void) { return ringing; }
void alarm_dismiss(void)    { stop_ringing("dismissed"); }
void alarm_test(void)       { start_ringing("test"); }

// After any change to the list, alarms already past their minute must not fire late
// (e.g. adding 06:50 at 07:00). An alarm set for the current minute still rings.
static void list_changed(void) {
    save_list();
    if (!time_is_valid()) return;
    int32_t floor_min = now_min() - 1;
    if (handled_min < floor_min) {
        handled_min = floor_min;
        save_handled();
    }
}

int alarm_count(void) { return store.count; }

bool alarm_get(int i, alarm_t* out) {
    if (i < 0 || i >= store.count) return false;
    *out = store.list[i];
    return true;
}

int alarm_add(const alarm_t* a) {
    if (store.count >= ALARM_MAX || !valid(a)) return -1;
    int i = store.count++;
    store.list[i] = *a;
    store.list[i].label[ALARM_LABEL_LEN - 1] = 0;
    list_changed();
    return i;
}

bool alarm_update(int i, const alarm_t* a) {
    if (i < 0 || i >= store.count || !valid(a)) return false;
    store.list[i] = *a;
    store.list[i].label[ALARM_LABEL_LEN - 1] = 0;
    list_changed();
    return true;
}

bool alarm_remove(int i) {
    if (i < 0 || i >= store.count) return false;
    memmove(&store.list[i], &store.list[i + 1], (store.count - i - 1) * sizeof(alarm_t));
    store.count--;
    memset(&store.list[store.count], 0, sizeof(alarm_t));
    list_changed();
    return true;
}

bool alarm_next(int32_t* when_min, int* index) {
    if (!time_is_valid()) return false;
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    int32_t from = now_min();
    if (from <= handled_min) from = handled_min + 1;
    int32_t best = 0;
    int best_i = -1;
    for (int d = 0; d <= 7; d++) {
        int wday = (t.tm_wday + d) % 7;
        for (int i = 0; i < store.count; i++) {
            const alarm_t* a = &store.list[i];
            if (!a->enabled || !(a->days & (1 << wday))) continue;
            int32_t occ = occurrence_min(&t, d, a->minutes);
            if (occ >= from && (best_i < 0 || occ < best)) {
                best = occ;
                best_i = i;
            }
        }
        if (best_i >= 0) break;     // later days can only be later
    }
    if (best_i < 0) return false;
    if (when_min) *when_min = best;
    if (index) *index = best_i;
    return true;
}

bool alarm_skip_next(bool on) {
    if (!on) {
        skip_min = 0;
        save_skip();
        return true;
    }
    int32_t when;
    if (!alarm_next(&when, NULL)) return false;
    skip_min = when;
    save_skip();
    return true;
}

bool alarm_skip_is_set(void) {
    // Still pending until the skipped alarm's late window has passed.
    return skip_min != 0 && skip_min > handled_min && skip_min + ALARM_LATE_MIN > now_min();
}

void alarm_print(void) {
    Serial.printf("alarm: %d of %d%s\n", store.count, ALARM_MAX, ringing ? "  [RINGING]" : "");
    for (int i = 0; i < store.count; i++) {
        const alarm_t* a = &store.list[i];
        char days[8];
        for (int d = 0; d < 7; d++) days[d] = (a->days & (1 << d)) ? "SMTWTFS"[d] : '-';
        days[7] = 0;
        Serial.printf("  %d: %s %02d:%02d %s \"%s\"\n", i, a->enabled ? "on " : "off",
                      a->minutes / 60, a->minutes % 60, days, a->label);
    }
    int32_t when;
    int idx;
    if (alarm_next(&when, &idx)) {
        time_t ts = (time_t)when * 60;
        struct tm t;
        localtime_r(&ts, &t);
        Serial.printf("  next: %s %02d:%02d (#%d)%s\n", DAY[t.tm_wday], t.tm_hour, t.tm_min, idx,
                      alarm_skip_is_set() && when <= skip_min ? " SKIPPED" : "");
    } else {
        Serial.println(time_is_valid() ? "  next: none" : "  next: unknown (time not set)");
    }
}
