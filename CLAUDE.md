# Minecraft Clock

Bedside clock + alarm for a 9-year-old, on a Waveshare ESP32-S3-Touch-AMOLED-2.16 (480x480
AMOLED). Themed pixel-art watch faces that shuffle; alarm with math-problem dismiss.

**Read `DESIGN.md` first.** It is the source of truth for requirements, hardware pins, build
settings, known issues in `mc_clock.c`, risks and build order. Update it when a decision changes.

## Current state

- Steps 1-2 done (2026-10-15): the 3 faces (`firmware/src/mc_clock.c`) run on the board with
  the review fixes applied. Open face issues are in DESIGN.md "mc_clock.c review".
- Step 3 done (2026-10-15): time loads from the RTC at boot, Wi-Fi + SNTP corrects it and
  writes the RTC. Offline path verified: forced power-off, no Wi-Fi, time correct on restart.
  Wi-Fi details in `firmware/src/secrets.h` (git-ignored; copy `secrets.example.h`).
  `--:--` for unknown time is not yet seen on the board.
- Step 4 done (2026-10-15, `screen.cpp`): panel off 22:00-06:00, tap/PWR wakes it dim for 15 s
  (the waking tap is swallowed), pixel shift up to 4 px every 2 min, face deleted while dark.
  Never dark while time is unknown. Next: step 5 (alarm core).
- Testing night mode with `time HH:MM` also writes the RTC; restore the real time afterwards
  (`time $(TZ=Asia/Jerusalem date +%H:%M:%S)`), or wait for the hourly NTP sync.
- Offline test build: `PLATFORMIO_BUILD_FLAGS=-DCLOCK_NO_WIFI platformio run -e clock -t upload`.
- `mc_clock_project/` holds only the browser preview and the old handoff `DESIGN.md`; the root
  `DESIGN.md` replaces it.

## Stack

PlatformIO + Arduino core 3.x (pinned pioarduino platform) + LVGL 9, C for faces.
Board code is reused from `~/Documents/Clawdmeter` (`firmware/src/boards/waveshare_amoled_216/`),
which is known to build and run on this board. Do not write new display/touch/PMU drivers.
Copy its code, never its fonts or art (no license, Anthropic assets).

## Build / flash

```bash
/opt/homebrew/bin/platformio run -d firmware -e clock                      # build
/opt/homebrew/bin/platformio run -d firmware -e clock -t upload            # flash (USB-C)
/opt/homebrew/bin/platformio device monitor -d firmware                    # serial log
tools/screenshot.sh out.png                                                # PNG of the screen
```

Use `platformio`, not `pio`: `/opt/homebrew/bin/pio` is a broken link. If the build fails with
missing `scons.py`, `idf_tools.py` or `xtensa-esp32s3-elf-g++`, the toolchain cache under
`~/.platformio/packages` and `~/.platformio/tools` is damaged; deleting the affected folders makes
PlatformIO download them again.

Board code is in `firmware/src/board/` (`board.h` = pins + API). In `main.cpp`: PWR short press =
next brightness; serial commands `screenshot`, `time HH:MM[:SS]` (also writes the RTC), `face N`, `next`, `heap`, `i2cscan`, `status`, `wake`,
`beep`, `vol N`, `bright N`. The step-1 hardware test screen is in commit 01ad675.

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
