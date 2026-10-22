#pragma once
// Waveshare ESP32-S3-Touch-AMOLED-2.16: pins and the hardware API used by main.cpp.
// Pins and driver setup come from Clawdmeter's boards/waveshare_amoled_216/,
// verified on this board.
#include <stdint.h>
#include <stdbool.h>

#define LCD_WIDTH        480
#define LCD_HEIGHT       480

// QSPI display (CO5300)
#define LCD_CS           12
#define LCD_SCLK         38
#define LCD_SDIO0        4
#define LCD_SDIO1        5
#define LCD_SDIO2        6
#define LCD_SDIO3        7
#define LCD_RESET        2

// I2C bus shared by touch, PMU, IMU and audio codec
#define IIC_SDA          15
#define IIC_SCL          14

// Touch (CST9220)
#define TP_INT           11
#define TP_RST           2      // shared with LCD_RESET
#define CST9220_ADDR     0x5A

// PMU (AXP2101)
#define AXP2101_ADDR     0x34

// RTC (PCF85063), runs from the main battery via AXP2101 RTCLDO
#define PCF85063_ADDR    0x51
#define RTC_INT_GPIO     13

// Buttons, active LOW. PWR is read through the AXP2101, see power_*.
#define BTN_BOOT_GPIO    0
#define BTN_EXTRA_GPIO   18

// Audio: ES8311 codec + onboard speaker over I2S, amp enable on a GPIO
#define SND_I2S_MCLK     42
#define SND_I2S_BCLK     9
#define SND_I2S_WS       45
#define SND_I2S_DOUT     8
#define SND_I2S_DIN      10
#define SND_PA_PIN       46     // HIGH = amp on
#define SND_SAMPLE_RATE  44100
#define SND_ES8311_ADDR  0x18

// ---- display.cpp ----
void display_init(void);                       // bus + panel, cleared, default brightness
void display_set_brightness(uint8_t level);    // 0..255, 0 = panel dark
void display_power(bool on);                   // panel sleep in/out (~240 ms, blocks)
void display_draw_bitmap(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t* pixels);
void display_round_area(int32_t* x1, int32_t* y1, int32_t* x2, int32_t* y2);  // CO5300 needs even-aligned areas

// ---- touch.cpp ----
void touch_init(void);
void touch_read(uint16_t* x, uint16_t* y, bool* pressed);  // call from the LVGL indev read callback only

// ---- power.cpp ----
void power_init(void);
void power_tick(void);                // call every loop
int  power_battery_pct(void);         // -1 if unknown / no battery
bool power_is_charging(void);
bool power_is_vbus_in(void);
bool power_pwr_pressed(void);         // PWR short press edge, cleared on read

// ---- rtc.cpp ---- (UTC; I2C, so call from the loop thread only)
#include <time.h>
bool rtc_present(void);
bool rtc_read(struct tm* out, bool* time_lost);   // time_lost = oscillator stopped since last write
bool rtc_write(const struct tm* t);

// ---- buttons ----
void buttons_init(void);
bool button_boot_held(void);
bool button_extra_held(void);

// ---- imu.cpp ---- (QMI8658 accelerometer; I2C, so call from the loop thread only)
bool imu_init(void);
bool imu_read(float* ax, float* ay, float* az);   // in g

// ---- sound.cpp ----
bool sound_init(void);
void sound_set_volume(uint8_t vol);   // 0..100, codec digital volume
uint8_t sound_get_volume(void);
void sound_beep(uint16_t freq_hz, uint16_t ms);  // non-blocking; ignored while playing
bool sound_is_playing(void);
bool sound_alarm_start(void);         // repeating pattern, loudness ramps up (sound_set_ramp_ms); false if busy
void sound_set_ramp_ms(uint32_t ms);  // 15% -> 100% over this long; default 60 s, 0 = full at once
void sound_alarm_stop(void);          // returns at once; playback ends within ~10 ms
