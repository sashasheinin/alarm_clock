# Minecraft Clock — Design Doc

Bedside clock + alarm for a 9-year-old who loves Minecraft, running on a Waveshare
ESP32-S3-Touch-AMOLED-2.16. Several themed watch faces that shuffle.

This doc is the handoff to a local Claude Code session. Read it fully before changing code.

## Status

- Faces: 3 written in LVGL 9 (`mc_clock.c` / `mc_clock.h`). **Not yet compiled or run on hardware.**
  Reviewed 2026-10-15; fixes listed under "Known issues in mc_clock.c" are not applied yet.
- Browser preview of the same faces: `mc_clock_preview.html` (open locally, no server needed).
- Not started: project scaffolding, time sync, alarm, settings page, burn-in protection.
- Stack changed 2026-10-15 from ESP-IDF + Waveshare demo to PlatformIO + Arduino, reusing
  Clawdmeter's board code (see "Reference project").

## Hardware (from the Waveshare product page)

- Board: ESP32-S3-Touch-AMOLED-2.16 (buy the battery version for portability, "-EN" is without battery)
- SoC: ESP32-S3R8, dual-core LX7 up to 240 MHz, 8 MB PSRAM, 16 MB flash, 2.4 GHz Wi-Fi + BLE 5
- Display: 2.16" AMOLED, 480x480, CO5300 driver over QSPI
- Touch: CST9220 capacitive, I2C
- Audio: dual mics with echo cancellation, audio codec, speaker output
- Other: QMI8658 6-axis IMU, TF card slot, AXP2101 power management, battery header,
  PWR + BOOT buttons, one extra GPIO18 button, USB-C (ESP32-S3 native USB, programming/logs)
- RTC chip: **unverified.** The product page was read as listing one, but Clawdmeter's 2.16 port
  lists no RTC (only its 2.06 port has a PCF85063). Check the Waveshare schematic. See risk 1.
- No camera.
- Docs, schematics, ESP-IDF and Arduino demos: http://docs.waveshare.com/ESP32-S3-Touch-AMOLED-2.16

### Pin map (from Clawdmeter, verified working on this board)

| Part | Detail |
|---|---|
| Display CO5300 (QSPI) | CS 12, SCLK 38, SDIO0..3 = 4..7, RST 2 |
| I2C bus | SDA 15, SCL 14 (touch, PMU, IMU, codec share it) |
| Touch CST9220 | addr 0x5A, INT 11, RST 2 (shared with display reset) |
| PMU AXP2101 | addr 0x34; PWR key read through its IRQs |
| IMU QMI8658 | addr 0x6B |
| Audio ES8311 | addr 0x18; I2S MCLK 42, BCLK 9, WS 45, DOUT 8, DIN 10; amp enable GPIO 46 (HIGH = on) |
| Buttons | BOOT = GPIO 0, extra = GPIO 18 (both active LOW), PWR = AXP2101 PKEY |

## Reference project: Clawdmeter

`~/Documents/Clawdmeter` is an open-source project (local fork) that runs on this exact board. It
was built and flashed from this Mac on 2026-09-28, so its toolchain is known to work. Reuse its
board code; do not write new drivers.

- Board code: `firmware/src/boards/waveshare_amoled_216/` (display, touch, power, IMU, buttons,
  sound). LVGL setup: `firmware/src/main.cpp` `setup()`. Build config: `firmware/platformio.ini`
  env `waveshare_amoled_216`.
- Useful patterns: `idle.cpp` (fade to black, first touch only wakes), `brightness.cpp`
  (brightness levels saved with `Preferences`), `chime.cpp` (ES8311 + I2S playback in a task).
- Its `CLAUDE.md` section "Critical gotchas" lists the build traps.
- No license file (the repo contains Anthropic fonts and art). Copying board code for this
  personal project is fine; do not copy its fonts, icons or Clawd art.

## Stack

- PlatformIO + Arduino core 3.x + LVGL 9 (`lvgl/lvgl@^9.2.0`, resolved to 9.6.0 in Clawdmeter).
- Libraries: `GFX Library for Arduino` (CO5300), `SensorLib` (CST9220 touch, QMI8658),
  `XPowersLib` (AXP2101). Same versions as Clawdmeter's `platformio.ini`.
- LVGL runs in the Arduino `loop()` on one thread (`lv_timer_handler()` each pass,
  `lv_tick_set_cb(millis)`), so no LVGL lock is needed. Call `mc_clock_init(lv_screen_active())`
  once at the end of `setup()`.
- Faces stay plain C modules (`mc_clock.c`); the rest can be C++ where Arduino libraries need it.
- Arduino equivalents for later steps: `configTzTime()` for SNTP + timezone, `Preferences` for NVS,
  `SD_MMC` for the TF card, `ESP_I2S` + Clawdmeter's `es8311.c` for audio.

### Build settings that must be copied (each one cost Clawdmeter debugging time)

1. `platform =` the pinned pioarduino release `55.03.38-1` (URL in Clawdmeter's `platformio.ini`).
   The standard `espressif32` platform ships Arduino 2.x, which the GFX library does not support.
2. `board = esp32-s3-devkitc-1` with `board_build.arduino.memory_type = qio_opi`. Without it,
   PSRAM allocations return NULL and the screen stays black.
3. `-DARDUINO_USB_CDC_ON_BOOT=1` so serial logs come out over USB-C.
4. `-DLV_CONF_SKIP` plus LVGL options as `-D` flags. LVGL's default heap is then 64 KB, too small
   for the faces (Sky creates about 243 objects). Add `-DLV_USE_STDLIB_MALLOC=LV_STDLIB_CLIB` so
   LVGL uses the system allocator; confirm on first build.
5. CO5300 needs even-aligned flush areas: register a rounder callback on
   `LV_EVENT_INVALIDATE_AREA` (Clawdmeter's `display_hal_round_area`).
6. LVGL partial render mode, two 480x40 RGB565 buffers allocated in PSRAM.
7. Touch needs `setSwapXY(true)` and `setMirrorXY(true, false)`.
8. `pio` is at `/opt/homebrew/bin/pio`; it may not be on PATH in non-interactive shells.

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

### Known issues in mc_clock.c (review 2026-10-15, not fixed yet)

1. Buddy's eyes are not drawn until the first blink when the face is created after boot
   (the `tick < MC_TICK_MS * 2` check only holds right after boot). Draw eyes in `buddy_create()`.
2. A tap deletes the root object inside the root's own click handler. Make the root
   non-clickable and register `tap_cb` once on the screen in `mc_clock_init()`.
3. No NULL check after `lv_zalloc` in the three `*_create()` functions.
4. Lamp pulse steps every 150 ms but is polled every 200 ms, so it moves unevenly. Use
   `tick / MC_TICK_MS`.
5. A tap does not reset the shuffle timer; call `lv_timer_reset()` in `tap_cb`.
6. `sky_update()` sets the sun color twice (line 195 is redundant).
7. Cloud animation redraws at full frame rate forever; acceptable on USB, wasteful on battery.
   The night/bedtime face should not animate.

### IP rule

Original art only. No Mojang/Minecraft assets, textures, fonts or characters (no Creeper, Steve etc.).
Generic blocky/pixel style is fine. The child may design his own pixel face to replace Buddy later.
Same rule for Clawdmeter: reuse its code, not its art or fonts.

## Time

- Wi-Fi + SNTP at boot via `configTzTime()`.
- Timezone: `IST-2IDT,M3.4.4/26,M10.5.0` (Israel, with DST).
- If an RTC chip exists (see risk 1): write system time to it after each successful SNTP sync,
  and load system time from it at boot before Wi-Fi, so the clock is right with no Wi-Fi.
- If there is no RTC chip: the ESP32 keeps time only while powered. With the battery version and
  the AXP2101 keeping the ESP32 powered, time survives unplugging until the battery runs out.
  After a full power loss the time is unknown until Wi-Fi returns.
- Before the time is known (year < 2025), show a "time not set" state and do not fire alarms.

## Next up: alarm (not implemented)

Requirements:
- Fires from the system clock, never from the network. Must work fully offline.
- Alarms stored in flash (NVS, via `Preferences`), survive power loss. Weekday and weekend schedules.
- Gradual volume ramp-up. Sounds loaded from the TF card (WAV, a few options).
- Snooze: shake or flip (IMU).
- Dismiss: solve a multiplication problem on the touchscreen (difficulty configurable). Wake-up
  check and quiet practice at once.
- **The PWR key must never dismiss or snooze the alarm.** Holding it powers the board off
  (AXP2101 forced shutdown, 6 s by default; Clawdmeter raised it to 8 s). A child holding PWR to
  silence the alarm would turn the clock off. Use BOOT (GPIO 0) or GPIO 18 if a button is needed.
- Bedtime mode: minimal dark face + low brightness.

## Later

- Settings web page served by the board on home Wi-Fi: alarms, enabled faces, volume, brightness.
  No reflashing to change settings.
- More faces (space, robot, arcade scoreboard, lava lamp for night).
- Hebrew day/date names: needs a custom LVGL font and RTL handling. Do numerals and icons first.
  `lv_font_conv` output needs patching for LVGL 9 (see Clawdmeter `docs/fonts.md`).

## Known risks / decisions to verify early

1. **RTC chip may not exist.** Check the schematic before building time sync. Without it, offline
   time after a full power loss is lost, which matters for alarm reliability (risk 4).
2. **AMOLED burn-in.** Static digits for months can leave ghosting. Mitigations to implement:
   face shuffling (done), slow pixel shift of the whole face, low brightness cap at night,
   screen-off window with tap/alarm to wake. Clawdmeter's `idle.cpp` fade/wake logic is a
   starting point; `gfx->setBrightness(0)` turns the panel dark.
3. **Speaker loudness.** The onboard speaker is small. Test before writing alarm logic. Quick test
   with no new code: flash Clawdmeter, set the chime volume in
   `boards/waveshare_amoled_216/sound.cpp` from 65 to 100, send `buzz` over serial. If too quiet,
   add an external amp + speaker.
4. **Alarm reliability beats cool factor.** One missed alarm = the clock gets unplugged. Test the
   offline path (Wi-Fi off, power cycle) before anything else about the alarm.
5. **Sky face contrast.** White digits on the daytime blue may look washed out on real hardware.
   If so, add a dark outline/shadow behind the digits.
6. **LVGL memory.** See build setting 4. If the heap runs out, `lv_obj_create` fails and the
   face code dereferences NULL.

## Tools for checking the UI

- Clawdmeter has a desktop simulator env (`-e sim`, SDL2 window on the Mac, needs
  `brew install sdl2`) that runs the real LVGL code and can save screenshots headless. Worth
  copying so faces can be checked without flashing.
- Clawdmeter's `screenshot` serial command (`screenshot.sh`) dumps the device screen as a PNG.
  Copy it so Claude can check face changes on the real board.

## Build order (one layer at a time)

1. Scaffold a PlatformIO project from Clawdmeter's 2.16 board code and `main.cpp` setup, without
   BLE, splash or usage UI. Confirm display + touch work. Test speaker loudness here (risk 3).
2. Drop in `mc_clock.c/.h`, apply the review fixes, call `mc_clock_init()`, see the 3 faces on device.
3. Wi-Fi + SNTP + TZ (+ RTC sync if the chip exists).
4. Burn-in protection (pixel shift, night dim, screen-off window).
5. Alarm core (clock trigger, NVS storage, sound from TF card, volume ramp).
6. Dismiss/snooze (math problem UI, IMU shake/flip).
7. Settings web page.
8. More faces.

## Files

- `DESIGN.md` — this file (the copy inside the zip and `mc_clock_project/` is the older version)
- `mc_clock_project/mc_clock/mc_clock.c`, `mc_clock.h` — the face manager and 3 faces
- `mc_clock_project/mc_clock_preview.html` — browser preview (tap = random face, slider = time of day)
- `mc_clock_project.zip` — original handoff archive
