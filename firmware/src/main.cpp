// Build step 1: hardware bring-up test. Confirms display, touch, buttons, PMU
// and speaker work before the clock faces go in (step 2).
//
// On screen: color bars + corner markers (panel colors and edges), a dot that
// follows the finger, live touch / battery / volume readout.
// Buttons:   BOOT = 1 s beep at current volume, GPIO18 = next volume step + beep,
//            PWR short press = next brightness step.
// Serial:    "screenshot" (used by tools/screenshot.sh), "beep", "vol <0-100>",
//            "bright <0-255>".
#include <Arduino.h>
#include <Wire.h>
#include <lvgl.h>
#include <esp_heap_caps.h>
#include "board/board.h"

#define BUF_LINES 40   // LVGL partial render strips: 480 x 40 RGB565, two buffers in PSRAM

static const uint8_t VOLUMES[]    = {25, 50, 75, 100};
static const uint8_t BRIGHTNESS[] = {40, 120, 200, 255};
static int vol_idx    = 3;
static int bright_idx = 2;

static lv_obj_t* lbl_touch;
static lv_obj_t* lbl_power;
static lv_obj_t* lbl_sound;
static lv_obj_t* dot;
static uint32_t  tap_count = 0;

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

static void touch_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    (void)indev;
    uint16_t x, y;
    bool pressed;
    touch_read(&x, &y, &pressed);
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

// ------------------------------------------------------------- test screen

static lv_obj_t* rect(lv_obj_t* parent, int x, int y, int w, int h, uint32_t color) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static lv_obj_t* label(lv_obj_t* parent, int y, const lv_font_t* font, uint32_t color) {
    lv_obj_t* l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_width(l, LCD_WIDTH);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(l, 0, y);
    return l;
}

static void screen_touch_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) {
        lv_obj_remove_flag(dot, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(dot, p.x - 15, p.y - 15);
        lv_label_set_text_fmt(lbl_touch, "touch %d, %d   taps %lu", (int)p.x, (int)p.y,
                              (unsigned long)tap_count);
    } else if (code == LV_EVENT_CLICKED) {
        tap_count++;
        lv_label_set_text_fmt(lbl_touch, "touch %d, %d   taps %lu", (int)p.x, (int)p.y,
                              (unsigned long)tap_count);
    } else if (code == LV_EVENT_RELEASED) {
        lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
    }
}

static void update_sound_label(void) {
    lv_label_set_text_fmt(lbl_sound, "volume %u   brightness %u",
                          sound_get_volume(), BRIGHTNESS[bright_idx]);
}

static void update_power_label(void) {
    int pct = power_battery_pct();
    if (pct < 0)
        lv_label_set_text_fmt(lbl_power, "no battery   usb %s", power_is_vbus_in() ? "yes" : "no");
    else
        lv_label_set_text_fmt(lbl_power, "battery %d%%%s   usb %s", pct,
                              power_is_charging() ? " charging" : "",
                              power_is_vbus_in() ? "yes" : "no");
}

static void build_test_screen(void) {
    lv_obj_t* scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // Color bars: red, green, blue, white, yellow, cyan, magenta, gray.
    // If red shows as blue the panel byte/color order is wrong.
    static const uint32_t BARS[8] = {0xFF0000, 0x00FF00, 0x0000FF, 0xFFFFFF,
                                     0xFFFF00, 0x00FFFF, 0xFF00FF, 0x808080};
    for (int i = 0; i < 8; i++) rect(scr, 40 + i * 50, 60, 50, 80, BARS[i]);

    // Corner markers + 2 px frame: all four edges must be visible, nothing cut off.
    rect(scr, 0, 0, LCD_WIDTH, 2, 0xFFFFFF);
    rect(scr, 0, LCD_HEIGHT - 2, LCD_WIDTH, 2, 0xFFFFFF);
    rect(scr, 0, 0, 2, LCD_HEIGHT, 0xFFFFFF);
    rect(scr, LCD_WIDTH - 2, 0, 2, LCD_HEIGHT, 0xFFFFFF);
    rect(scr, 0, 0, 20, 20, 0xFF0000);                              // top-left red
    rect(scr, LCD_WIDTH - 20, 0, 20, 20, 0x00FF00);                 // top-right green
    rect(scr, 0, LCD_HEIGHT - 20, 20, 20, 0x0000FF);                // bottom-left blue
    rect(scr, LCD_WIDTH - 20, LCD_HEIGHT - 20, 20, 20, 0xFFFF00);   // bottom-right yellow

    lv_obj_t* title = label(scr, 170, &lv_font_montserrat_24, 0xFFFFFF);
    lv_label_set_text(title, "Step 1: hardware test");

    lbl_touch = label(scr, 220, &lv_font_montserrat_24, 0x7CFC00);
    lv_label_set_text(lbl_touch, "touch the screen");
    lbl_power = label(scr, 260, &lv_font_montserrat_24, 0xFFC24A);
    lbl_sound = label(scr, 300, &lv_font_montserrat_24, 0x78B4FF);

    lv_obj_t* help = label(scr, 360, &lv_font_montserrat_14, 0xB0B0B0);
    lv_label_set_text(help, "BOOT = beep    GPIO18 = next volume + beep\nPWR short = next brightness");

    dot = rect(scr, 0, 0, 30, 30, 0xFF2B1F);
    lv_obj_set_style_radius(dot, 15, 0);
    lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);

    lv_obj_add_event_cb(scr, screen_touch_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(scr, screen_touch_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(scr, screen_touch_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(scr, screen_touch_cb, LV_EVENT_CLICKED, NULL);

    update_power_label();
    update_sound_label();
}

// ------------------------------------------------------------- serial commands

static void send_screenshot(void) {
    const uint32_t row_bytes = LCD_WIDTH * 2;
    const uint32_t buf_size = row_bytes * LCD_HEIGHT;
    uint8_t* sbuf = (uint8_t*)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    if (!sbuf) { Serial.println("SCREENSHOT_ERR"); return; }

    lv_draw_buf_t draw_buf;
    lv_draw_buf_init(&draw_buf, LCD_WIDTH, LCD_HEIGHT, LV_COLOR_FORMAT_RGB565, row_bytes, sbuf, buf_size);
    if (lv_snapshot_take_to_draw_buf(lv_screen_active(), LV_COLOR_FORMAT_RGB565, &draw_buf) != LV_RESULT_OK) {
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

static void handle_command(const char* cmd) {
    if (strcmp(cmd, "screenshot") == 0) {
        send_screenshot();
    } else if (strcmp(cmd, "beep") == 0) {
        sound_beep(880, 1000);
    } else if (strncmp(cmd, "vol ", 4) == 0) {
        sound_set_volume((uint8_t)atoi(cmd + 4));
        Serial.printf("volume %u\n", sound_get_volume());
        update_sound_label();
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
    Serial.println("mc_clock: step 1 hardware test");
    Serial.printf("PSRAM %u bytes, free heap %u bytes\n", ESP.getPsramSize(), ESP.getFreeHeap());

    Wire.begin(IIC_SDA, IIC_SCL);
    display_init();
    power_init();
    sound_init();
    sound_set_volume(VOLUMES[vol_idx]);
    touch_init();
    buttons_init();

    lvgl_init();
    build_test_screen();
    display_set_brightness(BRIGHTNESS[bright_idx]);
    Serial.println("ready");
}

void loop() {
    lv_timer_handler();
    power_tick();
    check_serial();

    static bool boot_was = false, extra_was = false;
    bool boot = button_boot_held();
    bool extra = button_extra_held();
    if (boot && !boot_was) {
        Serial.println("BOOT pressed");
        sound_beep(880, 1000);
    }
    if (extra && !extra_was) {
        vol_idx = (vol_idx + 1) % (int)sizeof(VOLUMES);
        sound_set_volume(VOLUMES[vol_idx]);
        Serial.printf("GPIO18 pressed, volume %u\n", sound_get_volume());
        update_sound_label();
        sound_beep(880, 500);
    }
    boot_was = boot;
    extra_was = extra;

    if (power_pwr_pressed()) {
        bright_idx = (bright_idx + 1) % (int)sizeof(BRIGHTNESS);
        display_set_brightness(BRIGHTNESS[bright_idx]);
        Serial.printf("PWR pressed, brightness %u\n", BRIGHTNESS[bright_idx]);
        update_sound_label();
    }

    static uint32_t last_power_ui = 0;
    if (millis() - last_power_ui > 2000) {
        last_power_ui = millis();
        update_power_label();
    }

    delay(5);
}
