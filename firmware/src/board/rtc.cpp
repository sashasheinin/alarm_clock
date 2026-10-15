// PCF85063 real-time clock (I2C 0x51). Powered from the AXP2101 RTCLDO, so it
// keeps time while the main battery has charge; there is no coin cell.
// Stores UTC. Registers 0x04..0x0A hold seconds..years in BCD; bit 7 of the
// seconds register (OS) is set when the oscillator stopped, i.e. time was lost.
#include "board.h"
#include <Arduino.h>
#include <Wire.h>

#define REG_CONTROL_1 0x00
#define REG_SECONDS   0x04
#define OS_FLAG       0x80

static uint8_t bcd2bin(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
static uint8_t bin2bcd(uint8_t v) { return ((v / 10) << 4) | (v % 10); }

bool rtc_present(void) {
    Wire.beginTransmission(PCF85063_ADDR);
    return Wire.endTransmission() == 0;
}

bool rtc_read(struct tm* out, bool* time_lost) {
    Wire.beginTransmission(PCF85063_ADDR);
    Wire.write(REG_SECONDS);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((uint8_t)PCF85063_ADDR, (uint8_t)7) != 7) return false;

    uint8_t r[7];
    for (int i = 0; i < 7; i++) r[i] = Wire.read();

    *time_lost = (r[0] & OS_FLAG) != 0;
    memset(out, 0, sizeof(*out));
    out->tm_sec  = bcd2bin(r[0] & 0x7F);
    out->tm_min  = bcd2bin(r[1] & 0x7F);
    out->tm_hour = bcd2bin(r[2] & 0x3F);          // 24-hour mode (Control_1 12_24 bit = 0)
    out->tm_mday = bcd2bin(r[3] & 0x3F);
    out->tm_wday = r[4] & 0x07;
    out->tm_mon  = bcd2bin(r[5] & 0x1F) - 1;
    out->tm_year = bcd2bin(r[6]) + 100;           // years since 2000 -> since 1900
    return true;
}

bool rtc_write(const struct tm* t) {
    if (t->tm_year < 100 || t->tm_year > 199) return false;   // RTC holds 2000-2099
    // Make sure the clock runs in 24-hour mode.
    Wire.beginTransmission(PCF85063_ADDR);
    Wire.write(REG_CONTROL_1);
    Wire.write(0x00);
    if (Wire.endTransmission() != 0) return false;

    Wire.beginTransmission(PCF85063_ADDR);
    Wire.write(REG_SECONDS);
    Wire.write(bin2bcd(t->tm_sec));               // writing seconds with OS = 0 clears the flag
    Wire.write(bin2bcd(t->tm_min));
    Wire.write(bin2bcd(t->tm_hour));
    Wire.write(bin2bcd(t->tm_mday));
    Wire.write(t->tm_wday & 0x07);
    Wire.write(bin2bcd(t->tm_mon + 1));
    Wire.write(bin2bcd(t->tm_year - 100));
    return Wire.endTransmission() == 0;
}
