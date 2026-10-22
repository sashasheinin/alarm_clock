// Host test of alarm.cpp: fake clock (time() and millis()), fake NVS, Israel time zone.
// Each case drives alarm_tick() every 20 s of fake time. Run with firmware/host_test/run.sh.
#include "faketime.h"
#include <Arduino.h>
#include <Preferences.h>
#include <stdlib.h>
#include "alarm.h"
std::map<std::string, std::vector<uint8_t>> g_nvs;
FakeSerial Serial;
static time_t T; static uint32_t MS; static bool playing, valid = true; static int rings;
time_t fake_time(time_t* p) { if (p) *p = T; return T; }
uint32_t millis(void) { return MS; }
bool sound_alarm_start(void) { playing = true; rings++; return true; }
void sound_alarm_stop(void) { playing = false; }
bool sound_is_playing(void) { return playing; }
void screen_wake(void) {}
bool time_is_valid(void) { return valid; }
static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
static time_t at(int y, int mo, int d, int h, int mi) { struct tm t = {}; t.tm_year = y - 1900; t.tm_mon = mo - 1; t.tm_mday = d; t.tm_hour = h; t.tm_min = mi; t.tm_isdst = -1; return mktime(&t); }
// advance the fake clock minute by minute up to `to`, ticking each second-ish
static void run_to(time_t to) { while (T < to) { T += 20; MS += 20000; alarm_tick(); } }
static void reboot() { playing = false; MS += 5000; alarm_init(); }
int main() {
    setenv("TZ", "IST-2IDT,M3.4.4/26,M10.5.0", 1); tzset();
    // 2026-10-25 is a Sunday.
    puts("-- 1. fresh NVS: default 06:45 Sun-Fri");
    T = at(2026, 10, 25, 6, 0); alarm_init(); CHECK(alarm_count() == 1);
    rings = 0; run_to(at(2026, 10, 25, 6, 46)); CHECK(alarm_is_ringing() && rings == 1);
    alarm_dismiss(); run_to(at(2026, 10, 25, 7, 30)); CHECK(!alarm_is_ringing() && rings == 1);
    puts("-- 2. reboot inside the late window does not ring again");
    T = at(2026, 10, 25, 6, 50); reboot(); rings = 0; run_to(at(2026, 10, 25, 7, 10)); CHECK(rings == 0);
    puts("-- 3. board off at 06:45, on at 07:05: rings late; at 07:20: nothing");
    T = at(2026, 10, 26, 7, 5); reboot(); rings = 0; run_to(at(2026, 10, 26, 7, 6)); CHECK(rings == 1);
    alarm_dismiss();
    T = at(2026, 10, 27, 7, 20); reboot(); rings = 0; run_to(at(2026, 10, 27, 7, 30)); CHECK(rings == 0);
    puts("-- 4. Saturday off");
    T = at(2026, 10, 31, 6, 0); rings = 0; run_to(at(2026, 10, 31, 8, 0)); CHECK(rings == 0);
    puts("-- 5. two alarms 06:45 + 06:50, board off until 07:00: one ring");
    alarm_t b = {1, 0x7F, 6 * 60 + 50, "Second"}; CHECK(alarm_add(&b) == 1);
    T = at(2026, 11, 2, 7, 0); reboot(); rings = 0; run_to(at(2026, 11, 2, 7, 40)); CHECK(rings == 1);
    alarm_dismiss();
    puts("-- 6. both ring separately when the board is on");
    T = at(2026, 11, 3, 6, 0); rings = 0; run_to(at(2026, 11, 3, 6, 46)); CHECK(rings == 1); alarm_dismiss();
    run_to(at(2026, 11, 3, 6, 51)); CHECK(rings == 2); alarm_dismiss();
    puts("-- 7. adding an alarm already past its minute does not fire it late");
    alarm_remove(1); T = at(2026, 11, 3, 8, 0);
    alarm_t c = {1, 0x7F, 7 * 60 + 50, "Late"}; alarm_add(&c); rings = 0; run_to(at(2026, 11, 3, 8, 30)); CHECK(rings == 0);
    alarm_remove(1);
    puts("-- 8. alarm set for the current minute rings");
    alarm_t e = {1, 0x7F, 9 * 60, "Now"}; T = at(2026, 11, 3, 9, 0) + 10; alarm_add(&e); rings = 0; run_to(T + 30); CHECK(rings == 1); alarm_dismiss(); alarm_remove(1);
    puts("-- 9. skip next: skips Wed 06:45 only, survives reboot, Thu rings");
    T = at(2026, 11, 3, 21, 0); CHECK(alarm_skip_next(true)); CHECK(alarm_skip_is_set()); alarm_print();
    reboot(); CHECK(alarm_skip_is_set());
    rings = 0; run_to(at(2026, 11, 4, 8, 0)); CHECK(rings == 0); CHECK(!alarm_skip_is_set());
    run_to(at(2026, 11, 5, 6, 46)); CHECK(rings == 1); alarm_dismiss();
    puts("-- 10. unknown time never fires");
    valid = false; rings = 0; T = at(2026, 11, 6, 6, 40); run_to(at(2026, 11, 6, 7, 0)); CHECK(rings == 0); valid = true;
    puts("-- 11. 15 min auto-stop");
    T = at(2026, 11, 8, 6, 44); rings = 0; run_to(at(2026, 11, 8, 6, 46)); CHECK(alarm_is_ringing());
    run_to(at(2026, 11, 8, 7, 2)); CHECK(!alarm_is_ringing() && rings == 1);
    puts("-- 12. migration from the old int16_t[7] schedule");
    g_nvs.clear(); int16_t old[7] = {405, 405, 405, 405, 405, 420, -1};
    g_nvs["sched"].assign((uint8_t*)old, (uint8_t*)old + sizeof(old));
    T = at(2026, 11, 8, 5, 0); alarm_init(); CHECK(alarm_count() == 2);
    alarm_t g; alarm_get(0, &g); CHECK(g.minutes == 405 && g.days == 0x1F);
    alarm_get(1, &g); CHECK(g.minutes == 420 && g.days == 0x20); CHECK(!g_nvs.count("sched"));
    puts("-- 13. migration of all-off schedule (current board state) gives empty list");
    g_nvs.clear(); int16_t off[7] = {-1, -1, -1, -1, -1, -1, -1};
    g_nvs["sched"].assign((uint8_t*)off, (uint8_t*)off + sizeof(off)); alarm_init(); CHECK(alarm_count() == 0);
    puts("-- 14. next alarm across the DST change (2026-10-25 02:00 back to standard)");
    g_nvs.clear(); T = at(2026, 10, 24, 22, 0); alarm_init(); int32_t w; CHECK(alarm_next(&w, NULL));
    { time_t ts = (time_t)w * 60; struct tm t; localtime_r(&ts, &t); CHECK(t.tm_hour == 6 && t.tm_min == 45 && t.tm_mday == 25); }
    puts("-- 15. snooze: 5 min, at most 2, then only dismiss ends it");
    g_nvs.clear(); T = at(2026, 11, 9, 6, 0); alarm_init(); rings = 0;
    run_to(at(2026, 11, 9, 6, 46)); CHECK(alarm_is_ringing());
    CHECK(alarm_snooze()); CHECK(!alarm_is_ringing() && alarm_is_active() && !playing);
    run_to(at(2026, 11, 9, 6, 50)); CHECK(!alarm_is_ringing());
    run_to(at(2026, 11, 9, 6, 52)); CHECK(alarm_is_ringing() && rings == 2);
    CHECK(alarm_snooze()); run_to(at(2026, 11, 9, 6, 58)); CHECK(alarm_is_ringing() && rings == 3);
    CHECK(alarm_snoozes_left() == 0); CHECK(!alarm_snooze()); CHECK(alarm_is_ringing());
    alarm_dismiss(); CHECK(!alarm_is_active()); run_to(at(2026, 11, 9, 7, 40)); CHECK(rings == 3);
    puts("-- 16. reboot during a snooze still rings again");
    T = at(2026, 11, 10, 6, 44); rings = 0; run_to(at(2026, 11, 10, 6, 46)); CHECK(alarm_snooze());
    CHECK(alarm_snoozes_left() == 1);   /* a new alarm starts with both snoozes */
    T = at(2026, 11, 10, 6, 48); reboot(); CHECK(alarm_is_active() && !alarm_is_ringing());
    run_to(at(2026, 11, 10, 6, 52)); CHECK(alarm_is_ringing() && alarm_snoozes_left() == 1);
    alarm_dismiss();
    puts("-- 17. dismiss during a snooze cancels it");
    T = at(2026, 11, 11, 6, 44); rings = 0; run_to(at(2026, 11, 11, 6, 46)); CHECK(alarm_snooze());
    alarm_dismiss(); CHECK(!alarm_is_active()); run_to(at(2026, 11, 11, 7, 30)); CHECK(rings == 1);
    puts("-- 18. board off across the snooze end, on 40 min later: dropped");
    T = at(2026, 11, 12, 6, 44); rings = 0; run_to(at(2026, 11, 12, 6, 46)); CHECK(alarm_snooze());
    T = at(2026, 11, 12, 7, 30); reboot(); run_to(at(2026, 11, 12, 7, 32)); CHECK(!alarm_is_active() && rings == 1);
    puts("-- 19. timeout ends a snoozed-then-ringing alarm completely");
    T = at(2026, 11, 13, 6, 44); rings = 0; run_to(at(2026, 11, 13, 6, 46)); CHECK(alarm_snooze());
    run_to(at(2026, 11, 13, 6, 52)); CHECK(alarm_is_ringing()); run_to(at(2026, 11, 13, 7, 8));
    CHECK(!alarm_is_active() && rings == 2);
    printf("\n%s (%d failures)\n", fails ? "FAILED" : "ALL PASSED", fails);
    return fails != 0;
}
