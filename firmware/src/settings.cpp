#include "settings.h"
#include <Arduino.h>
#include <Preferences.h>
#include <string.h>
#include "board/board.h"

static Preferences prefs;
static uint8_t volume = 100;
static char    pin[PIN_MAX_LEN + 1] = "";

void settings_init(void) {
    prefs.begin("settings", false);
    volume = prefs.getUChar("vol", 100);
    prefs.getString("pin", pin, sizeof(pin));
    sound_set_volume(volume);
    Serial.printf("settings: volume %u, PIN %s\n", volume, pin[0] ? "set" : "NOT SET (serial: pin NNNN)");
}

uint8_t settings_volume(void) { return volume; }

void settings_set_volume(uint8_t vol) {
    if (vol > 100) vol = 100;
    volume = vol;
    prefs.putUChar("vol", volume);
    sound_set_volume(volume);
}

bool settings_pin_is_set(void) { return pin[0] != 0; }

bool settings_set_pin(const char* p) {
    size_t n = strlen(p);
    if (n != 0 && (n < PIN_MIN_LEN || n > PIN_MAX_LEN)) return false;
    for (size_t i = 0; i < n; i++)
        if (p[i] < '0' || p[i] > '9') return false;
    strcpy(pin, p);
    prefs.putString("pin", pin);
    return true;
}

bool settings_check_pin(const char* p) { return pin[0] && strcmp(p, pin) == 0; }
