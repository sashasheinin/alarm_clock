#pragma once
#include <time.h>
time_t fake_time(time_t*);
#define time(x) fake_time(x)
