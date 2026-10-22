#include "dismiss.h"
#include <Arduino.h>
#include <esp_random.h>
#include "alarm.h"
#include "board/board.h"

#define MAX_DIGITS 3
#define WRONG_MS   1200                // how long "try again" stays red

static lv_obj_t* overlay  = NULL;
static lv_obj_t* question = NULL;
static lv_obj_t* hint     = NULL;
static int       a, b;
static char      entry[MAX_DIGITS + 1];
static uint32_t  wrong_at = 0;

static const char* const KEYS[] = {"1", "2", "3", "\n",
                                   "4", "5", "6", "\n",
                                   "7", "8", "9", "\n",
                                   LV_SYMBOL_BACKSPACE, "0", LV_SYMBOL_OK, ""};

static int pick(void) { return DISMISS_MIN + esp_random() % (DISMISS_MAX - DISMISS_MIN + 1); }

static void show_question(void) {
    lv_label_set_text_fmt(question, "%d x %d = %s", a, b, entry[0] ? entry : "?");
}

static void key_cb(lv_event_t* e) {
    lv_obj_t* m = (lv_obj_t*)lv_event_get_target(e);
    const char* k = lv_buttonmatrix_get_button_text(m, lv_buttonmatrix_get_selected_button(m));
    if (!k) return;
    size_t n = strlen(entry);
    if (strcmp(k, LV_SYMBOL_BACKSPACE) == 0) {
        if (n) entry[n - 1] = 0;
    } else if (strcmp(k, LV_SYMBOL_OK) == 0) {
        if (!n) return;
        if (atoi(entry) == a * b) {
            Serial.printf("dismiss: %d x %d = %s correct\n", a, b, entry);
            alarm_dismiss();            // dismiss_tick() removes the overlay
            return;
        }
        Serial.printf("dismiss: %d x %d = %s wrong\n", a, b, entry);
        entry[0] = 0;
        wrong_at = millis();
        lv_label_set_text(hint, "Try again");
        lv_obj_set_style_text_color(hint, lv_color_hex(0xFF5A4A), 0);
    } else if (n < MAX_DIGITS) {
        entry[n] = k[0];
        entry[n + 1] = 0;
    }
    show_question();
}

static void create(void) {
    a = pick();
    b = pick();
    entry[0] = 0;
    wrong_at = 0;

    overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x101820), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);     // taps never reach the face below
    lv_obj_remove_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);

    hint = lv_label_create(overlay);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_24, 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 24);

    question = lv_label_create(overlay);
    lv_obj_set_style_text_font(question, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(question, lv_color_white(), 0);
    lv_obj_align(question, LV_ALIGN_TOP_MID, 0, 62);

    lv_obj_t* m = lv_buttonmatrix_create(overlay);
    lv_buttonmatrix_set_map(m, KEYS);
    lv_obj_set_size(m, 396, 330);
    lv_obj_align(m, LV_ALIGN_BOTTOM_MID, 0, -22);
    lv_obj_set_style_bg_opa(m, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(m, 0, 0);
    lv_obj_set_style_pad_all(m, 0, 0);
    lv_obj_set_style_pad_gap(m, 12, 0);
    lv_obj_set_style_text_font(m, &lv_font_montserrat_48, LV_PART_ITEMS);
    lv_obj_set_style_text_color(m, lv_color_white(), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(m, lv_color_hex(0x3C8527), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(m, lv_color_hex(0x6CC349), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_radius(m, 6, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(m, 0, LV_PART_ITEMS);
    lv_obj_add_event_cb(m, key_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_label_set_text(hint, "Solve it to stop the alarm");
    lv_obj_set_style_text_color(hint, lv_color_hex(0xB0B8C0), 0);
    show_question();
    Serial.printf("dismiss: showing %d x %d\n", a, b);
}

void dismiss_tick(void) {
    bool ringing = alarm_is_ringing();
    if (ringing && !overlay) create();
    if (!ringing && overlay) {
        lv_obj_delete(overlay);
        overlay = question = hint = NULL;
        return;
    }
    if (overlay && wrong_at && millis() - wrong_at >= WRONG_MS) {
        wrong_at = 0;
        lv_label_set_text(hint, "Solve it to stop the alarm");
        lv_obj_set_style_text_color(hint, lv_color_hex(0xB0B8C0), 0);
    }
}

lv_obj_t* dismiss_overlay(void) { return overlay; }
