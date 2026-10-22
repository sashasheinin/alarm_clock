#pragma once
// Math dismiss: while the alarm rings, a full-screen multiplication problem with a
// keypad covers the face (on LVGL's top layer); it stays during a snooze so the
// alarm can be stopped early. One correct answer stops the alarm.
// A wrong answer clears the entry and keeps the same problem. dismiss_set_problems() sets
// the largest factor and how many problems in a row must be solved.
#include <lvgl.h>

#define DISMISS_MIN 2                  // factors are drawn from DISMISS_MIN..max (default 9)

void dismiss_set_problems(int max_factor, int count);   // max 5-12, count 1-5

void dismiss_tick(void);               // call every loop, after alarm_tick() and gesture_tick()
lv_obj_t* dismiss_overlay(void);       // the overlay while shown, else NULL (for screenshots)
