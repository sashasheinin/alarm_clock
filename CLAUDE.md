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
  Never dark while time is unknown.
- Step 5 done (2026-10-15, `alarm.cpp`): per-weekday alarm in NVS (default 06:45 Sun-Fri, Sat
  off), fires only on valid time, up to 30 min late, no double ring after reboot, generated
  beep pattern ramping 15% -> 100% over 60 s, auto-stop after 15 min. BOOT or serial `stop`
  dismisses for now. Not yet tested on the board: the late-fire path and the 15 min timeout.
- Step 5b done (2026-10-22): alarm list (up to 8: time, weekday mask, on/off, label) + "skip
  next", replacing the per-weekday schedule (old NVS key converted on first boot). Phone control
  decided (DESIGN.md "Phone control").
- Step 6 part 1 done (2026-10-22, `dismiss.cpp`): while ringing, a full-screen keypad with an
  N x M problem (2-9) covers the face; one correct answer stops the alarm. BOOT no longer stops
  it; serial `stop` still does. Touch was rotated 90 degrees (never noticed: faces only use "tap
  anywhere"); fixed in `board/touch.cpp`, check with serial `touchlog`.
- Step 6 done (2026-10-22, `gesture.cpp`, `board/imu.cpp`): turn face down (0.7 s, near flat)
  or shake to snooze 5 min, at most 2 per alarm; snooze end saved in NVS (survives a reboot).
  The math screen stays up during a snooze. Thresholds come from 20 Hz recordings (pickups and
  tilting toward the face must not snooze); numbers in `gesture.cpp`. QMI8658 low-pass filter
  must stay LPF_MODE_3: MODE_0 smoothed shakes away. To test snooze quickly, set the snooze to
  1 min on the phone page (saved settings override `-DALARM_SNOOZE_MIN`). Serial
  `imulog`/`gesturelog` show the data.
- Step 7a done (2026-10-22, `web.cpp`, `settings.cpp`): phone page at http://clock.local or
  http://192.168.0.103 (the IP changed after a reflash on 2026-10-23; the user should reserve it in the router): alarm list, skip next, volume (saved in
  NVS; serial `vol` is temporary). Changes need the parent PIN, set over serial `pin NNNN`;
  5 wrong PINs lock changes for 1 min. API: GET /api/state, POST /api/alarm|delete|skip|volume|beep
  (form fields, `pin` in each). Checked with curl + Playwright (Pixel 7); not yet on the user's phone.
- Step 7b done (2026-10-22): the page also sets ramp length, snooze length and count, problem
  range and how many in a row, enabled faces, day brightness, night-off window (or none).
  `settings.cpp` keeps them in NVS and applies them through module setters (`alarm_set_snooze`,
  `dismiss_set_problems`, `mc_clock_set_enabled`, `screen_set_night`, `sound_set_ramp_ms`), so the
  modules don't depend on settings. PWR brightness steps and serial `vol` last until reboot.
- Step 8 (2026-10-22): faces 4-6 added, Cave, Torch, Nether (DESIGN.md "Faces implemented"),
  each checked by screenshot. Faces added by a firmware update start enabled (`settings.cpp`
  stores the face count with the mask). Keep a face under ~300 objects: Cave leaves 112 KB free.
- WAV alarm (2026-10-23, `board/sound.cpp`): the alarm plays the first `.wav` (by name) in
  `/sounds` on the LittleFS "spiffs" partition (3.4 MB), looped, resampled to 44.1 kHz; beeps if
  there is none or a read fails. No TF card. Sounds live in `firmware/data/sounds/` (git-ignored:
  may be copyrighted) and go to the board with `platformio run -d firmware -e clock -t uploadfs`
  (does not touch NVS; a firmware upload does not touch the sounds). Plays 16-bit PCM or mono
  IMA ADPCM. Best: 44.1 kHz ADPCM (no resampling, 4 bits/sample; 84 s = 1.86 MB). Make one with
  `ffmpeg -i in -t 84 -af "aformat=channel_layouts=mono,aresample=44100,highpass=f=200,`
  `alimiter=limit=0.89:level=false" -c:a adpcm_ima_wav out.wav`. The decoder matches ffmpeg
  bit for bit (checked 2026-10-23). 16 kHz PCM sounded "mechanical" (8 kHz band limit plus
  linear resampling).
- ES8311 volume (2026-10-23): our `es8311.c` maps volume 100 to 0 dB (reg 0xBF), 0.5 dB per step.
  The original driver put 100 at +32 dB: music clipped and sounded harsh; beeps hid it.
  Next: Torch as the night-wake / bedtime face, or more Minecraft faces.
- **Alarm is currently OFF on the board** (2026-10-22): one alarm `0: 06:45 Sun-Fri "School"`,
  switched off until the user decides to turn it on (dismiss and snooze both work). Restore with serial `alarm on 0`.
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
firmware/host_test/run.sh                                                  # alarm logic test on the Mac
```

Use `platformio`, not `pio`: `/opt/homebrew/bin/pio` is a broken link. If the build fails with
missing `scons.py`, `idf_tools.py` or `xtensa-esp32s3-elf-g++`, the toolchain cache under
`~/.platformio/packages` and `~/.platformio/tools` is damaged; deleting the affected folders makes
PlatformIO download them again.

Board code is in `firmware/src/board/` (`board.h` = pins + API). In `main.cpp`: PWR short press =
next brightness; serial commands `screenshot`, `time HH:MM[:SS]` (also writes the RTC), `face N`, `next`, `heap`, `i2cscan`, `status`, `wake`, `alarm` (list), `alarm add HH:MM DAYS [label]`, `alarm set N ...`,
`alarm on|off|del N`, `alarm skip [off]`, `alarm test`, `stop` (DAYS = digits 0-6, 0 = Sunday), `pin NNNN|off`, `touchlog`, `snooze`, `imulog`, `gesturelog`,
`beep`, `vol N`, `bright N`. The step-1 hardware test screen is in commit 01ad675.

## Rules

- Run `firmware/host_test/run.sh` after any change to `alarm.cpp`; add a case for new alarm behavior.
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
