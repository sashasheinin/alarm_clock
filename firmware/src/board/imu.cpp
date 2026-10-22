// QMI8658 accelerometer (I2C, same bus as touch/PMU/RTC). Init from Clawdmeter's imu.cpp,
// at 62.5 Hz instead of its 21 Hz low-power mode so shakes are not smoothed away.
// Low-pass filter LPF_MODE_3 = 13.37% of ODR (~8 Hz). LPF_MODE_0 (2.66%, ~1.7 Hz) removed
// most of a 3-6 Hz shake: a hard shake took 14 s to snooze (2026-10-22).
#include "board.h"
#include <Arduino.h>
#include <Wire.h>
#include <SensorQMI8658.hpp>

static SensorQMI8658 imu;
static bool imu_ok = false;

bool imu_init(void) {
    if (!imu.begin(Wire, QMI8658_L_SLAVE_ADDRESS, IIC_SDA, IIC_SCL)) {
        Serial.println("imu: QMI8658 init failed");
        return false;
    }
    imu.configAccelerometer(SensorQMI8658::ACC_RANGE_4G, SensorQMI8658::ACC_ODR_62_5Hz,
                            SensorQMI8658::LPF_MODE_3);
    imu.enableAccelerometer();
    imu_ok = true;
    Serial.println("imu: QMI8658 ready");
    return true;
}

bool imu_read(float* ax, float* ay, float* az) {
    if (!imu_ok) return false;
    return imu.getAccelerometer(*ax, *ay, *az);
}
