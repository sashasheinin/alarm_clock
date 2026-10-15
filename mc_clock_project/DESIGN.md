# Minecraft Clock — Design Doc

Bedside clock + alarm for a 9-year-old who loves Minecraft, running on a Waveshare
ESP32-S3-Touch-AMOLED-2.16. Several themed watch faces that shuffle.

This doc is the handoff to a local Claude Code session. Read it fully before changing code.

## Status

- Faces: 3 written in LVGL 9 (`mc_clock.c` / `mc_clock.h`). **Not yet compiled or run on hardware.**
  Expect small API/compile fixes on first build.
- Browser preview of the same faces: `mc_clock_preview.html` (open locally, no server needed).
- Not started: time sync, alarm, settings page, burn-in protection, project scaffolding.

## Hardware (from the Waveshare product page)

- Board: ESP32-S3-Touch-AMOLED-2.16 (buy the battery version for portability, "-EN" is without battery)
- SoC: ESP32-S3R8, dual-core LX7 up to 240 MHz, 8 MB PSRAM, 16 MB flash, 2.4 GHz Wi-Fi + BLE 5
- Display: 2.16" AMOLED, 480x480, CO5300 driver over QSPI
- Touch: CST9220 capacitive, I2C
- Audio: dual mics with echo cancellation, audio codec, speaker output
- Other: QMI8658 6-axis IMU, RTC chip, TF card slot, AXP2101 power management, battery header,
  PWR + BOOT buttons, one extra GPIO18 button, USB-C (ESP32-S3 native USB, programming/logs)
- No camera.
- Docs, schematics, ESP-IDF and Arduino demos: http://docs.waveshare.com/ESP32-S3-Touch-AMOLED-2.16

## Stack

- ESP-IDF + LVGL 9. Start from Waveshare's ESP-IDF LVGL demo for this board (it already has the
  CO5300 display, CST9220 touch and LVGL port code). Do not rewrite drivers.
- Call `mc_clock_init(lv_screen_active())` once display + touch + LVGL are running, while holding
  the LVGL port lock (`lvgl_port_lock` or the demo's equivalent).
- Language: C. Keep faces as plain C modules, no C++ needed.

## Face architecture

- Each face = `create(parent)` + `update(root, struct tm*, tick_ms)` in a table (`FACES[]`).
  Adding a face = write two functions + one table row.
- Manager creates a 200 ms LVGL timer that calls the current face's `update()`.
- A face owns a heap struct stored in the root object's user_data; freed by an
  `LV_EVENT_DELETE` callback. Switching faces deletes the root (this also removes its animations).
- Tap anywhere = next random face. Auto-shuffle every `MC_SHUFFLE_MS` (10 min).
- All art is drawn from plain squares (`lv_obj` rectangles). No image assets.
- Shared 5x7 pixel-digit grid (`pixel_clock_t`): 4 digits + blinking colon, only changed digits
  are redrawn. 24-hour format.

### Faces implemented

1. **Sky** — square sun/moon moves along an arc with the real time (sun 06:00-18:00), sky color
   by phase (night / twilight / day), stars at night, 3 drifting clouds (lv_anim), grass+dirt ground.
2. **Lamp** — orange "lamp" digits on a dark wall, red signal pulse travelling along a wire at the bottom.
3. **Buddy** — mottled green block face with cheeks and smile, blinks about every 4 s, time below.

### IP rule

Original art only. No Mojang/Minecraft assets, textures, fonts or characters (no Creeper, Steve etc.).
Generic blocky/pixel style is fine. The child may design his own pixel face to replace Buddy later.

## Time

- Wi-Fi + SNTP at boot, RTC (onboard chip) as the offline source of truth and for the alarm.
- Timezone: `setenv("TZ", "IST-2IDT,M3.4.4/26,M10.5.0", 1); tzset();` (Israel, with DST)
- Write system time to the RTC after each successful SNTP sync; at boot, load system time from
  the RTC first, so the clock is right even with no Wi-Fi.

## Next up: alarm (not implemented)

Requirements:
- Fires from the RTC/system clock, never from the network. Must work fully offline.
- Alarms stored in flash (NVS), survive power loss. Weekday and weekend schedules.
- Gradual volume ramp-up. Sounds loaded from the TF card (WAV, a few options).
- Snooze: shake or flip (IMU).
- Dismiss: solve a multiplication problem on the touchscreen (difficulty configurable). Wake-up
  check and quiet practice at once.
- Bedtime mode: minimal dark face + low brightness.

## Later

- Settings web page served by the board on home Wi-Fi: alarms, enabled faces, volume, brightness.
  No reflashing to change settings.
- More faces (space, robot, arcade scoreboard, lava lamp for night).
- Hebrew day/date names: needs a custom LVGL font and RTL handling. Do numerals and icons first.

## Known risks / decisions to verify early

1. **AMOLED burn-in.** Static digits for months can leave ghosting. Mitigations to implement:
   face shuffling (done), slow pixel shift of the whole face, low brightness cap at night,
   screen-off window with tap/alarm to wake.
2. **Speaker loudness.** The onboard speaker is small. Test max volume on the real board before
   building alarm logic. If too quiet, add an external amp + speaker.
3. **Alarm reliability beats cool factor.** One missed alarm = the clock gets unplugged. Test the
   offline path (Wi-Fi off, power cycle) before anything else about the alarm.
4. **Sky face contrast.** White digits on the daytime blue may look washed out on real hardware.
   If so, add a dark outline/shadow behind the digits.
5. Compile-level checks to expect on first build: LVGL 9 API names (`lv_obj_remove_flag`,
   `lv_zalloc`, `lv_anim_set_exec_cb` cast), and that `lv_conf.h` has what is used.

## Build order (one layer at a time)

1. Scaffold from the Waveshare ESP-IDF LVGL demo; confirm display + touch work.
2. Drop in `mc_clock.c/.h`, call `mc_clock_init()`, fix compile issues, see the 3 faces on device.
3. Wi-Fi + SNTP + TZ + RTC sync.
4. Burn-in protection (pixel shift, night dim, screen-off window).
5. Alarm core (RTC trigger, NVS storage, sound from TF card, volume ramp).
6. Dismiss/snooze (math problem UI, IMU shake/flip).
7. Settings web page.
8. More faces.

## Files in this zip

- `mc_clock/mc_clock.c`, `mc_clock/mc_clock.h` — the face manager and 3 faces
- `mc_clock_preview.html` — browser preview of the same faces (tap = random face, slider = time of day)
- `DESIGN.md` — this file
