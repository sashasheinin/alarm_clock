// AXP2101 PMU: battery, USB power, PWR key. Adapted from Clawdmeter
// boards/waveshare_amoled_216/power.cpp (long-press pairing gesture removed),
// plus the two plain GPIO buttons.
#include "board.h"
#include <Arduino.h>
#include <Wire.h>
#include <XPowersLib.h>

#define BATTERY_POLL_MS  2000
#define CHARGING_POLL_MS 500
#define PWR_POLL_MS      50

static XPowersPMU pmu;
static bool     pmu_ok           = false;
static int      cached_pct       = -1;
static bool     cached_charging  = false;
static bool     cached_vbus      = false;
static bool     pwr_pressed_flag = false;
static uint32_t last_battery_ms  = 0;
static uint32_t last_charging_ms = 0;
static uint32_t last_pwr_ms      = 0;

void power_init(void) {
    if (!pmu.begin(Wire, AXP2101_ADDR, IIC_SDA, IIC_SCL)) {
        Serial.println("power: AXP2101 init failed");
        return;
    }
    pmu_ok = true;
    Serial.println("power: AXP2101 init OK");

    pmu.enableBattDetection();
    pmu.enableBattVoltageMeasure();

    pmu.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
    pmu.clearIrqStatus();
    pmu.enableIRQ(XPOWERS_AXP2101_PKEY_SHORT_IRQ);

    // Holding PWR forces a hardware power-off (6 s default). Raise it to 8 s so
    // a long press is less likely to switch the clock off. PWR must still never
    // be used to dismiss the alarm.
    pmu.setPowerKeyPressOffTime(XPOWERS_POWEROFF_8S);

    cached_charging = pmu.isCharging();
    cached_vbus     = pmu.isVbusIn();
    cached_pct      = pmu.isBatteryConnect() ? pmu.getBatteryPercent() : -1;
}

void power_tick(void) {
    if (!pmu_ok) return;
    uint32_t now = millis();

    if (now - last_charging_ms >= CHARGING_POLL_MS) {
        last_charging_ms = now;
        cached_charging = pmu.isCharging();
        cached_vbus     = pmu.isVbusIn();
    }
    if (now - last_battery_ms >= BATTERY_POLL_MS) {
        last_battery_ms = now;
        cached_pct = pmu.isBatteryConnect() ? pmu.getBatteryPercent() : -1;
    }
    if (now - last_pwr_ms >= PWR_POLL_MS) {
        last_pwr_ms = now;
        pmu.getIrqStatus();
        if (pmu.isPekeyShortPressIrq()) pwr_pressed_flag = true;
        pmu.clearIrqStatus();
    }
}

int  power_battery_pct(void) { return cached_pct; }
bool power_is_charging(void) { return cached_charging; }
bool power_is_vbus_in(void)  { return cached_vbus; }

bool power_pwr_pressed(void) {
    if (pwr_pressed_flag) { pwr_pressed_flag = false; return true; }
    return false;
}

void buttons_init(void) {
    pinMode(BTN_BOOT_GPIO, INPUT_PULLUP);
    pinMode(BTN_EXTRA_GPIO, INPUT_PULLUP);
}

bool button_boot_held(void)  { return digitalRead(BTN_BOOT_GPIO) == LOW; }
bool button_extra_held(void) { return digitalRead(BTN_EXTRA_GPIO) == LOW; }
