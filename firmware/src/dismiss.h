#pragma once
// Math dismiss: while the alarm rings, a full-screen multiplication problem with a
// keypad covers the face (on LVGL's top layer); it stays during a snooze so the
// alarm can be stopped early. One correct answer stops the alarm.
// A wrong answer clears the entry and keeps the same problem.
#include <lvgl.h>

#define DISMISS_MIN 2                  // factors are drawn from DISMISS_MIN..DISMISS_MAX
#define DISMISS_MAX 9

void dismiss_tick(void);               // call every loop, after alarm_tick() and gesture_tick()
lv_obj_t* dismiss_overlay(void);       // the overlay while shown, else NULL (for screenshots)
