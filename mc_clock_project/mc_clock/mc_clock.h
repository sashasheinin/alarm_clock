#pragma once
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Call once after LVGL + display + touch are up (and while holding the LVGL lock).
 * Shows a random face, tap anywhere = next random face, auto-shuffle every
 * MC_SHUFFLE_MS. Time comes from time()/localtime_r(), so set TZ + SNTP elsewhere. */
void mc_clock_init(lv_obj_t *screen);
void mc_clock_next(void);

#ifdef __cplusplus
}
#endif
