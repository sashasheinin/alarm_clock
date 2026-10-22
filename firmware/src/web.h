#pragma once
// Settings page for the phone on home Wi-Fi: http://clock.local (mDNS) or the clock's IP.
// Starts once Wi-Fi connects; requests are handled in loop(), on the LVGL/alarm thread.
// Anyone on the network can view; changes need the parent PIN (settings.h).
// 5 wrong PINs lock changes for a minute.

void web_tick(void);                  // call every loop
void web_print_status(void);
