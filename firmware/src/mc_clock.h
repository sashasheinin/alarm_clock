#pragma once
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Call once after LVGL + display + touch are up. All functions here touch LVGL,
 * so call them only from the thread that runs lv_timer_handler().
 * Shows a random face, tap anywhere = next random face, auto-shuffle every
 * MC_SHUFFLE_MS. Time comes from time()/localtime_r(), so set TZ + SNTP elsewhere. */
void mc_clock_init(lv_obj_t *screen);
void mc_clock_next(void);          /* random face other than the current one */
void mc_clock_show(int idx);       /* specific face, 0..mc_clock_face_count()-1 */
int  mc_clock_face_count(void);

#ifdef __cplusplus
}
#endif
