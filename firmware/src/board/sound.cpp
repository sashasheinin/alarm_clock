// ES8311 codec + speaker. Codec/I2S setup copied from Clawdmeter chime.cpp;
// playback here is a generated sine tone instead of an embedded clip, for the
// step-1 loudness test. WAV playback from the TF card comes with the alarm.
#include "board.h"
#include <Arduino.h>
#include <math.h>
#include "ESP_I2S.h"
#include "es8311.h"

#define TONE_CHUNK_FRAMES 441   // 10 ms at 44.1 kHz

static I2SClass        i2s;
static es8311_handle_t codec   = nullptr;
static bool            ready   = false;
static volatile bool   playing = false;
static uint8_t         volume  = 100;
static uint16_t        tone_hz = 0;
static uint16_t        tone_ms = 0;
static volatile bool   alarm_stop_req = false;

static void amp_enable(bool on) { digitalWrite(SND_PA_PIN, on ? HIGH : LOW); }

static void tone_task(void* arg) {
    (void)arg;
    static int16_t buf[TONE_CHUNK_FRAMES * 2];   // stereo
    const float step = 2.0f * (float)M_PI * tone_hz / SND_SAMPLE_RATE;
    float phase = 0.0f;
    uint32_t chunks = (uint32_t)tone_ms / 10;

    amp_enable(true);
    delay(8);                                    // let the amp settle (avoids turn-on pop)
    for (uint32_t c = 0; c < chunks; c++) {
        for (int i = 0; i < TONE_CHUNK_FRAMES; i++) {
            int16_t s = (int16_t)(sinf(phase) * 30000.0f);
            buf[2 * i] = buf[2 * i + 1] = s;
            phase += step;
            if (phase > 2.0f * (float)M_PI) phase -= 2.0f * (float)M_PI;
        }
        i2s.write((uint8_t*)buf, sizeof(buf));
    }
    memset(buf, 0, sizeof(buf));
    i2s.write((uint8_t*)buf, sizeof(buf));       // flush with silence before the amp goes off
    delay(20);
    amp_enable(false);
    playing = false;
    vTaskDelete(nullptr);
}

// Alarm: repeating "beep-beep ... pause" pattern until sound_alarm_stop().
// Loudness ramps from ALARM_START_LEVEL to full over ramp_ms by scaling
// the samples here, so nothing touches the I2C codec while it plays.
static volatile uint32_t ramp_ms = 60000;   // read by the alarm task; 0 = full at once
#define ALARM_START_LEVEL  0.15f
#define ALARM_FREQ_HZ      1000

// pattern in 10 ms chunks: on, off, on, off(long)
static const uint8_t ALARM_PATTERN[] = {15, 8, 15, 40};

void sound_set_ramp_ms(uint32_t ms) { ramp_ms = ms; }

static void alarm_task(void* arg) {
    (void)arg;
    static int16_t buf[TONE_CHUNK_FRAMES * 2];
    const float step = 2.0f * (float)M_PI * ALARM_FREQ_HZ / SND_SAMPLE_RATE;
    float phase = 0.0f;
    uint32_t start = millis();

    amp_enable(true);
    delay(8);
    while (!alarm_stop_req) {
        for (int seg = 0; seg < (int)sizeof(ALARM_PATTERN) && !alarm_stop_req; seg++) {
            bool on = (seg % 2) == 0;
            for (int c = 0; c < ALARM_PATTERN[seg] && !alarm_stop_req; c++) {
                float ramp = ramp_ms ? (float)(millis() - start) / ramp_ms : 1.0f;
                float level = ALARM_START_LEVEL + (1.0f - ALARM_START_LEVEL) * (ramp > 1.0f ? 1.0f : ramp);
                for (int i = 0; i < TONE_CHUNK_FRAMES; i++) {
                    int16_t v = on ? (int16_t)(sinf(phase) * 30000.0f * level) : 0;
                    buf[2 * i] = buf[2 * i + 1] = v;
                    phase += step;
                    if (phase > 2.0f * (float)M_PI) phase -= 2.0f * (float)M_PI;
                }
                i2s.write((uint8_t*)buf, sizeof(buf));
            }
        }
    }
    memset(buf, 0, sizeof(buf));
    i2s.write((uint8_t*)buf, sizeof(buf));
    delay(20);
    amp_enable(false);
    playing = false;
    vTaskDelete(nullptr);
}

bool sound_alarm_start(void) {
    if (!ready || playing) return false;
    playing = true;
    alarm_stop_req = false;
    if (xTaskCreatePinnedToCore(alarm_task, "alarm", 4096, nullptr, 2, nullptr, 0) != pdPASS) {
        playing = false;
        return false;
    }
    return true;
}

void sound_alarm_stop(void) { alarm_stop_req = true; }

bool sound_init(void) {
    pinMode(SND_PA_PIN, OUTPUT);
    amp_enable(false);

    i2s.setPins(SND_I2S_BCLK, SND_I2S_WS, SND_I2S_DOUT, SND_I2S_DIN, SND_I2S_MCLK);
    if (!i2s.begin(I2S_MODE_STD, SND_SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT,
                   I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH)) {
        Serial.println("sound: I2S init failed");
        return false;
    }
    codec = es8311_create(0, SND_ES8311_ADDR);   // I2C port 0 (shared Wire bus)
    if (!codec) {
        Serial.println("sound: ES8311 create failed");
        return false;
    }
    // mclk_inverted, sclk_inverted, mclk_from_mclk_pin, mclk_frequency, sample_frequency
    const es8311_clock_config_t clk = {
        false, false, true, SND_SAMPLE_RATE * 256, SND_SAMPLE_RATE
    };
    if (es8311_init(codec, &clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16) != ESP_OK) {
        Serial.println("sound: ES8311 init failed");
        return false;
    }
    es8311_sample_frequency_config(codec, clk.mclk_frequency, clk.sample_frequency);
    es8311_microphone_config(codec, false);
    es8311_voice_volume_set(codec, volume, NULL);
    ready = true;
    Serial.println("sound: ES8311 ready");
    return true;
}

void sound_set_volume(uint8_t vol) {
    if (vol > 100) vol = 100;
    volume = vol;
    if (ready) es8311_voice_volume_set(codec, volume, NULL);
}

uint8_t sound_get_volume(void) { return volume; }
bool sound_is_playing(void)    { return playing; }

void sound_beep(uint16_t freq_hz, uint16_t ms) {
    if (!ready || playing) return;
    playing = true;
    tone_hz = freq_hz;
    tone_ms = ms;
    if (xTaskCreatePinnedToCore(tone_task, "tone", 4096, nullptr, 1, nullptr, 0) != pdPASS)
        playing = false;
}
