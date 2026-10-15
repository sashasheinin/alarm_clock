#pragma once
// System time: Israel timezone, loaded from the RTC at boot, corrected by SNTP
// over Wi-Fi when available, written back to the RTC after each sync.
// The clock and alarm read time()/localtime_r() only; they never wait for the network.
#include <stdint.h>

#define TIME_VALID_YEAR 2025      // system time before this year = time unknown

enum TimeSource { TIME_NONE, TIME_FROM_RTC, TIME_FROM_NTP, TIME_FROM_SERIAL };

void timekeeping_init(void);      // after Wire.begin(); sets TZ, loads RTC, starts Wi-Fi + SNTP
void timekeeping_tick(void);      // call every loop: writes RTC after a sync
bool time_is_valid(void);
TimeSource time_source(void);
const char* time_source_name(void);
void timekeeping_print_status(void);
void timekeeping_set_manual(int hh, int mm, int ss);   // serial "time" command: sets system time + RTC
