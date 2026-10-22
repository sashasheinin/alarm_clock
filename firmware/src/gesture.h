#pragma once
// Snooze gestures from the accelerometer while the alarm rings: turn the clock face
// down, or shake it. Calibrated 2026-10-22 on the board: standing = +1 g on X,
// face down = -1 g on Z, lying on its back = +1 g on Z. Thresholds in gesture.cpp.

void gesture_tick(void);              // call every loop, after alarm_tick()
void gesture_set_log(bool on);        // serial "gesturelog": print the shake score
