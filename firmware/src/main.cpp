// Clock firmware: faces (mc_clock.c) + timekeeping (RTC, Wi-Fi + SNTP).
//
// Buttons:   PWR short press = next day brightness step; at night it wakes the screen.
//            PWR never stops the alarm (holding it powers the board off).
//            A ringing alarm stops only by solving the math problem (dismiss.cpp),
//            serial "stop", or the 15 min timeout. Flip/shake snoozes (gesture.cpp).
// Serial:    "screenshot" (used by tools/screenshot.sh), "time HH:MM[:SS]",
//            "face N" (0 sky, 1 lamp, 2 buddy), "next", "heap", "i2cscan", "status", "wake", "beep",
//            "alarm" (list), "alarm add HH:MM DAYS [label]", "alarm set N HH:MM DAYS [label]",
//            "alarm on N", "alarm off N", "alarm del N", "alarm skip [off]", "alarm test", "stop",
//            (DAYS = digits 0-6, 0 = Sunday, e.g. 012345 = Sun-Fri)
//            "vol <0-100>" (until reboot; the phone page saves it), "pin <4-8 digits>|off", "bright <0-255>", "snooze", "touchlog", "imulog", "gesturelog" (toggles).
#include <Arduino.h>
#include <Wire.h>
#include <lvgl.h>
#include <esp_heap_caps.h>
#include <sys/time.h>
#include "board/board.h"
#include "mc_clock.h"
#include "timekeeping.h"
#include "screen.h"
#include "alarm.h"
#include "dismiss.h"
#include "gesture.h"
#include "settings.h"
#include "web.h"

#define BUF_LINES 40   // LVGL partial render strips: 480 x 40 RGB565, two buffers in PSRAM

static const uint8_t BRIGHTNESS[] = {40, 120, 200, 255};
static int bright_idx = 2;

// ------------------------------------------------------------- LVGL glue

static uint32_t my_tick(void) { return millis(); }

static void flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    int32_t w = area->x2 - area->x1 + 1;
    int32_t h = area->y2 - area->y1 + 1;
    display_draw_bitmap(area->x1, area->y1, w, h, (uint16_t*)px_map);
    lv_display_flush_ready(disp);
}

static void rounder_cb(lv_event_t* e) {
    lv_area_t* a = (lv_area_t*)lv_event_get_param(e);
    display_round_area(&a->x1, &a->y1, &a->x2, &a->y2);
}

static bool gesture_log = false; // serial "gesturelog": print shake score while the alarm is on
static bool imu_log = false;     // serial "imulog": print acceleration 20x per second
static bool touch_log = false;   // serial "touchlog": print each press position

static void touch_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    (void)indev;
    static bool was_pressed = false;
    static bool swallow = false;   // the touch that woke a dark screen must not also switch faces
    uint16_t x, y;
    bool pressed;
    touch_read(&x, &y, &pressed);
    if (touch_log && pressed && !was_pressed) Serial.printf("touch %u,%u\n", x, y);
    was_pressed = pressed;
    if (pressed && screen_is_dark()) {
        screen_wake();
        swallow = true;
    }
    if (swallow) {
        if (!pressed) swallow = false;
        pressed = false;
    }
    data->point.x = x;
    data->point.y = y;
    data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static void lvgl_init(void) {
    lv_init();
    lv_tick_set_cb(my_tick);

    const size_t buf_bytes = LCD_WIDTH * BUF_LINES * 2;
    void* buf1 = heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM);
    void* buf2 = heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM);
    if (!buf1 || !buf2) Serial.println("lvgl: draw buffer alloc failed (PSRAM missing?)");

    lv_display_t* disp = lv_display_create(LCD_WIDTH, LCD_HEIGHT);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_display_set_buffers(disp, buf1, buf2, buf_bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_add_event_cb(disp, rounder_cb, LV_EVENT_INVALIDATE_AREA, NULL);

    lv_indev_t* indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_cb);
}

// ------------------------------------------------------------- serial commands

static void send_screenshot(void) {
    const uint32_t row_bytes = LCD_WIDTH * 2;
    const uint32_t buf_size = row_bytes * LCD_HEIGHT;
    uint8_t* sbuf = (uint8_t*)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    if (!sbuf) { Serial.println("SCREENSHOT_ERR"); return; }

    lv_draw_buf_t draw_buf;
    lv_draw_buf_init(&draw_buf, LCD_WIDTH, LCD_HEIGHT, LV_COLOR_FORMAT_RGB565, row_bytes, sbuf, buf_size);
    if (lv_snapshot_take_to_draw_buf(dismiss_overlay() ? dismiss_overlay() : lv_screen_active(), LV_COLOR_FORMAT_RGB565, &draw_buf) != LV_RESULT_OK) {
        heap_caps_free(sbuf);
        Serial.println("SCREENSHOT_ERR");
        return;
    }
    Serial.printf("SCREENSHOT_START %u %u %lu\n", LCD_WIDTH, LCD_HEIGHT, (unsigned long)buf_size);
    Serial.flush();
    Serial.write(sbuf, buf_size);
    Serial.flush();
    Serial.println();
    Serial.println("SCREENSHOT_END");
    heap_caps_free(sbuf);
}

static void print_heap(void) {
    Serial.printf("heap: internal free %u (min %u, largest block %u), psram free %u\n",
                  heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
                  heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                  heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

// "time HH:MM[:SS]": sets local time (today's date, or 2026-01-01 if unknown) and the RTC.
static void set_time_cmd(const char* arg) {
    int h = 0, m = 0, sec = 0;
    if (sscanf(arg, "%d:%d:%d", &h, &m, &sec) < 2 || h < 0 || h > 23 || m < 0 || m > 59 ||
        sec < 0 || sec > 59) {
        Serial.println("usage: time HH:MM[:SS]");
        return;
    }
    timekeeping_set_manual(h, m, sec);
}

// "HH:MM DAYS [label]" -> alarm (enabled). DAYS = digits 0-6, 0 = Sunday.
static bool parse_alarm(const char* args, alarm_t* a) {
    int h, m, n = 0;
    char days[16];
    if (sscanf(args, "%d:%d %15s %n", &h, &m, days, &n) < 3 || h < 0 || h > 23 || m < 0 || m > 59)
        return false;
    memset(a, 0, sizeof(*a));
    for (const char* c = days; *c; c++) {
        if (*c < '0' || *c > '6') return false;
        a->days |= 1 << (*c - '0');
    }
    a->enabled = 1;
    a->minutes = h * 60 + m;
    if (n > 0) strlcpy(a->label, args + n, sizeof(a->label));
    return true;
}

static void alarm_command(const char* args) {
    alarm_t a;
    int i, n = 0;
    bool ok;
    if (strncmp(args, "add ", 4) == 0) {
        ok = parse_alarm(args + 4, &a) && alarm_add(&a) >= 0;
    } else if (sscanf(args, "set %d %n", &i, &n) == 1 && n > 0) {
        ok = parse_alarm(args + n, &a) && alarm_update(i, &a);
    } else if (sscanf(args, "on %d", &i) == 1 || sscanf(args, "off %d", &i) == 1) {
        ok = alarm_get(i, &a);
        a.enabled = args[1] == 'n';
        ok = ok && alarm_update(i, &a);
    } else if (sscanf(args, "del %d", &i) == 1) {
        ok = alarm_remove(i);
    } else if (strcmp(args, "skip") == 0) {
        ok = alarm_skip_next(true);
    } else if (strcmp(args, "skip off") == 0) {
        ok = alarm_skip_next(false);
    } else {
        Serial.println("usage: alarm add HH:MM DAYS [label] | set N HH:MM DAYS [label] | on N | off N"
                       " | del N | skip [off] | test   (DAYS: 0-6, 0=Sun)");
        return;
    }
    if (!ok) Serial.println("alarm: rejected (bad index, bad time/days, list full, or no next alarm)");
    alarm_print();
}

static void handle_command(const char* cmd) {
    if (strcmp(cmd, "screenshot") == 0) {
        send_screenshot();
    } else if (strncmp(cmd, "time ", 5) == 0) {
        set_time_cmd(cmd + 5);
    } else if (strncmp(cmd, "face ", 5) == 0) {
        int idx = atoi(cmd + 5);
        if (idx < 0 || idx >= mc_clock_face_count()) {
            Serial.println("no such face");
        } else {
            mc_clock_show(idx);
            Serial.printf("face %d\n", idx);
            print_heap();
        }
    } else if (strcmp(cmd, "next") == 0) {
        mc_clock_next();
        print_heap();
    } else if (strcmp(cmd, "status") == 0) {
        timekeeping_print_status();
        alarm_print();
        web_print_status();
        settings_print();
        screen_print_status();
        Serial.printf("power: battery %d%%%s, usb %s\n", power_battery_pct(),
                      power_is_charging() ? " charging" : "", power_is_vbus_in() ? "yes" : "no");
    } else if (strcmp(cmd, "alarm") == 0) {
        alarm_print();
    } else if (strcmp(cmd, "alarm test") == 0) {
        alarm_test();
    } else if (strncmp(cmd, "alarm ", 6) == 0) {
        alarm_command(cmd + 6);
    } else if (strncmp(cmd, "pin ", 4) == 0) {
        const char* p = strcmp(cmd + 4, "off") == 0 ? "" : cmd + 4;
        if (settings_set_pin(p)) Serial.printf("pin %s\n", p[0] ? "set" : "cleared");
        else Serial.println("usage: pin <4-8 digits> | pin off");
    } else if (strcmp(cmd, "snooze") == 0) {
        if (!alarm_snooze()) Serial.println("snooze: not ringing or no snoozes left");
    } else if (strcmp(cmd, "gesturelog") == 0) {
        gesture_log = !gesture_log;
        gesture_set_log(gesture_log);
        Serial.printf("gesturelog %s\n", gesture_log ? "on" : "off");
    } else if (strcmp(cmd, "imulog") == 0) {
        imu_log = !imu_log;
        Serial.printf("imulog %s\n", imu_log ? "on" : "off");
    } else if (strcmp(cmd, "touchlog") == 0) {
        touch_log = !touch_log;
        Serial.printf("touchlog %s\n", touch_log ? "on" : "off");
    } else if (strcmp(cmd, "stop") == 0) {
        alarm_dismiss();
    } else if (strcmp(cmd, "wake") == 0) {
        screen_wake();
    } else if (strcmp(cmd, "heap") == 0) {
        print_heap();
    } else if (strcmp(cmd, "i2cscan") == 0) {
        for (uint8_t a = 1; a < 127; a++) {
            Wire.beginTransmission(a);
            if (Wire.endTransmission() == 0) Serial.printf("i2c: device at 0x%02X\n", a);
        }
        Serial.println("i2c: scan done");
    } else if (strcmp(cmd, "beep") == 0) {
        sound_beep(880, 1000);
    } else if (strncmp(cmd, "vol ", 4) == 0) {
        sound_set_volume((uint8_t)atoi(cmd + 4));
        Serial.printf("volume %u\n", sound_get_volume());
    } else if (strncmp(cmd, "bright ", 7) == 0) {
        int b = constrain(atoi(cmd + 7), 0, 255);
        display_set_brightness((uint8_t)b);
        Serial.printf("brightness %d\n", b);
    } else if (cmd[0]) {
        Serial.printf("unknown command: %s\n", cmd);
    }
}

static void check_serial(void) {
    static char buf[64];
    static int pos = 0;
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            buf[pos] = '\0';
            handle_command(buf);
            pos = 0;
        } else if (pos < (int)sizeof(buf) - 1) {
            buf[pos++] = c;
        }
    }
}

// ------------------------------------------------------------- setup / loop

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("mc_clock: step 3 timekeeping");
    Serial.printf("PSRAM %u bytes, free heap %u bytes\n", ESP.getPsramSize(), ESP.getFreeHeap());

    Wire.begin(IIC_SDA, IIC_SCL);
    display_init();
    power_init();
    timekeeping_init();
    sound_init();
    imu_init();
    touch_init();
    buttons_init();

    lvgl_init();
    mc_clock_init(lv_screen_active());
    screen_init(BRIGHTNESS[bright_idx]);
    settings_init();
    alarm_init();
    print_heap();
    Serial.println("ready");
}

void loop() {
    lv_timer_handler();
    power_tick();
    timekeeping_tick();
    alarm_tick();
    gesture_tick();
    web_tick();
    dismiss_tick();

    static uint32_t imu_log_ms = 0;
    float ax, ay, az;
    if (imu_log && millis() - imu_log_ms >= 50 && imu_read(&ax, &ay, &az)) {
        imu_log_ms = millis();
        Serial.printf("imu %.2f %.2f %.2f\n", ax, ay, az);
    }
    screen_tick();
    check_serial();

    if (power_pwr_pressed()) {
        if (screen_is_dark()) {
            screen_wake();
            Serial.println("PWR pressed, screen woken");
        } else {
            bright_idx = (bright_idx + 1) % (int)sizeof(BRIGHTNESS);
            screen_set_day_brightness(BRIGHTNESS[bright_idx]);
            Serial.printf("PWR pressed, day brightness %u\n", BRIGHTNESS[bright_idx]);
        }
    }

    delay(5);
}
