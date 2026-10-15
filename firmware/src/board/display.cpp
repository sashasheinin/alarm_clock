// CO5300 AMOLED over QSPI. Adapted from Clawdmeter boards/waveshare_amoled_216/display.cpp,
// without the IMU-driven software rotation (the clock stands in one orientation).
#include "board.h"
#include <Arduino.h>
#include <Arduino_GFX_Library.h>

static Arduino_DataBus* bus = nullptr;
static Arduino_CO5300*  gfx = nullptr;

void display_init(void) {
    bus = new Arduino_ESP32QSPI(LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);
    // (bus, rst, rotation, w, h, col_offset1, row_offset1, col_offset2, row_offset2)
    gfx = new Arduino_CO5300(bus, LCD_RESET, 0, LCD_WIDTH, LCD_HEIGHT, 0, 0, 0, 0);
    if (!gfx->begin()) Serial.println("display: begin failed");
    gfx->fillScreen(0x0000);
    gfx->setBrightness(200);
}

void display_set_brightness(uint8_t level) {
    if (gfx) gfx->setBrightness(level);
}

void display_draw_bitmap(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t* pixels) {
    if (gfx) gfx->draw16bitRGBBitmap(x, y, (uint16_t*)pixels, w, h);
}

void display_round_area(int32_t* x1, int32_t* y1, int32_t* x2, int32_t* y2) {
    *x1 = *x1 & ~1;
    *y1 = *y1 & ~1;
    *x2 = *x2 | 1;
    *y2 = *y2 | 1;
}
