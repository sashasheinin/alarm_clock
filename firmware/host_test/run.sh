#!/bin/sh
# Host test of the alarm logic (alarm.cpp) with a fake clock and fake NVS. Runs on the Mac:
#   firmware/host_test/run.sh
# stubs/ replaces Arduino, Preferences and the board; faketime.h redirects time() to the test clock.
set -e
cd "$(dirname "$0")"
out="${TMPDIR:-/tmp}/alarm_test"
clang++ -std=c++17 -Wall -Wextra -include stubs/faketime.h -Istubs -I../src \
    alarm_test.cpp ../src/alarm.cpp -o "$out"
"$out" | grep -E "^--|FAIL|PASSED"
