# Minecraft Clock

Bedside clock + alarm for a 9-year-old, on a Waveshare ESP32-S3-Touch-AMOLED-2.16 (480x480
AMOLED). Themed pixel-art watch faces that shuffle; alarm with math-problem dismiss.

**Read `DESIGN.md` first.** It is the source of truth for requirements, hardware pins, build
settings, known issues in `mc_clock.c`, risks and build order. Update it when a decision changes.

## Current state

- Faces (`mc_clock_project/mc_clock/mc_clock.c`) are written but never compiled. The 7 review
  fixes in DESIGN.md "Known issues in mc_clock.c" are not applied yet.
- No PlatformIO project yet. Next step is build order step 1 (scaffold from Clawdmeter).
- `mc_clock_project/DESIGN.md` and `mc_clock_project.zip` are the old handoff; the root
  `DESIGN.md` replaces them.

## Stack

PlatformIO + Arduino core 3.x (pinned pioarduino platform) + LVGL 9, C for faces.
Board code is reused from `~/Documents/Clawdmeter` (`firmware/src/boards/waveshare_amoled_216/`),
which is known to build and run on this board. Do not write new display/touch/PMU drivers.
Copy its code, never its fonts or art (no license, Anthropic assets).

`pio` is at `/opt/homebrew/bin/pio` and may not be on PATH in non-interactive shells.

## Rules

- **Alarm reliability comes first.** The alarm fires from the local clock only, never depends on
  Wi-Fi, and must survive power loss (settings in NVS). Test the offline path before features.
- **PWR key must never dismiss or snooze the alarm**: holding it powers the board off.
- **Original art only.** No Mojang/Minecraft assets, textures, fonts or characters. All face art
  is plain `lv_obj` rectangles; no image files.
- Adding a face = `create()` + `update()` + one row in `FACES[]`. Faces must free their state on
  `LV_EVENT_DELETE` and not assume they were created at boot (`lv_tick_get()` is time since boot).
- LVGL runs on one thread in `loop()`; only touch LVGL objects from there.
- Build one layer at a time (DESIGN.md "Build order") and confirm each on hardware before the next.

## Checking UI changes

Once the project exists, copy Clawdmeter's `screenshot` serial command and `screenshot.sh` so UI
changes can be checked from a PNG of the real screen instead of asking the user. The browser
preview `mc_clock_project/mc_clock_preview.html` only approximates the C code.
