// ES8311 codec + speaker. Codec/I2S setup copied from Clawdmeter chime.cpp.
// Beeps are a generated sine tone; the alarm plays a WAV from flash (LittleFS) when there is one.
#include "board.h"
#include <Arduino.h>
#include <LittleFS.h>
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

// Alarm sound: the first .wav (by name) in /sounds on the LittleFS partition, looped.
// 16-bit PCM (mono or stereo) or IMA ADPCM (mono), any sample rate, resampled to 44.1 kHz.
// ADPCM at 44.1 kHz needs no resampling and is a quarter of the size: best quality. If there is no
// usable file, or reading it fails during the alarm, the generated "beep-beep ... pause"
// pattern plays instead, so the alarm always makes a sound.
// Loudness ramps from ALARM_START_LEVEL to full over ramp_ms by scaling the samples here,
// so nothing touches the I2C codec while it plays.
static volatile uint32_t ramp_ms = 60000;   // read by the alarm task; 0 = full at once
#define ALARM_START_LEVEL  0.15f
#define ALARM_FREQ_HZ      1000
#define WAV_READ_FRAMES    256
#define WAV_PCM            1
#define WAV_IMA_ADPCM      0x11
#define ADPCM_MAX_BLOCK    2048        // bytes; ffmpeg writes 1024

// pattern in 10 ms chunks: on, off, on, off(long)
static const uint8_t ALARM_PATTERN[] = {15, 8, 15, 40};

static uint32_t rd32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t* p) { return p[0] | p[1] << 8; }

static char     wav_path[64] = "";          // empty = no usable file: beep
static uint32_t wav_off, wav_len, wav_rate;
static uint16_t wav_channels, wav_format, wav_align;

// IMA ADPCM tables (public standard)
static const int8_t ADPCM_INDEX[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};
static const int16_t ADPCM_STEP[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66,
    73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408,
    449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
    2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630,
    9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};

// Decodes one mono IMA ADPCM WAV block (4-byte header, then two 4-bit codes per byte,
// low nibble first). Returns the number of samples written to out.
static size_t adpcm_block(const uint8_t* blk, size_t len, int16_t* out) {
    if (len < 4) return 0;
    int32_t pred = (int16_t)rd16(blk);
    int idx = blk[2] > 88 ? 88 : blk[2];
    size_t n = 0;
    out[n++] = (int16_t)pred;
    for (size_t i = 4; i < len; i++)
        for (int half = 0; half < 2; half++) {
            int code = half ? blk[i] >> 4 : blk[i] & 0x0F;
            int32_t diff = ((2 * (code & 7) + 1) * ADPCM_STEP[idx]) >> 3;   // as ffmpeg encodes
            pred += (code & 8) ? -diff : diff;
            pred = pred > 32767 ? 32767 : pred < -32768 ? -32768 : pred;
            idx += ADPCM_INDEX[code];
            idx = idx < 0 ? 0 : idx > 88 ? 88 : idx;
            out[n++] = (int16_t)pred;
        }
    return n;
}

void sound_set_ramp_ms(uint32_t ms) { ramp_ms = ms; }

static float ramp_level(uint32_t start) {
    float ramp = ramp_ms ? (float)(millis() - start) / ramp_ms : 1.0f;
    return ALARM_START_LEVEL + (1.0f - ALARM_START_LEVEL) * (ramp > 1.0f ? 1.0f : ramp);
}


// Finds the fmt and data chunks; true if the file is 16-bit PCM or mono IMA ADPCM.
static bool wav_parse(File& f) {
    uint8_t h[16];
    if (f.read(h, 12) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) return false;
    bool fmt_ok = false;
    while (f.read(h, 8) == 8) {
        uint32_t size = rd32(h + 4);
        if (!memcmp(h, "fmt ", 4) && size >= 16) {
            if (f.read(h, 16) != 16) return false;
            wav_format = rd16(h);
            wav_channels = rd16(h + 2);
            wav_rate = rd32(h + 4);
            wav_align = rd16(h + 12);
            bool pcm = wav_format == WAV_PCM && rd16(h + 14) == 16 && (wav_channels == 1 || wav_channels == 2);
            bool ima = wav_format == WAV_IMA_ADPCM && rd16(h + 14) == 4 && wav_channels == 1 &&
                       wav_align > 4 && wav_align <= ADPCM_MAX_BLOCK;
            fmt_ok = (pcm || ima) && wav_rate >= 8000 && wav_rate <= 48000;
            size -= 16;
        } else if (!memcmp(h, "data", 4)) {
            wav_off = f.position();
            wav_len = wav_format == WAV_PCM ? size - size % (2 * wav_channels) : size;
            return fmt_ok && wav_len > 0 && wav_off + wav_len <= f.size();
        }
        if (!f.seek(f.position() + size + (size & 1))) return false;
    }
    return false;
}

static void wav_find(void) {
    if (!LittleFS.begin(false)) {
        Serial.println("sound: no file system, the alarm will beep");
        return;
    }
    char best[64] = "";
    File dir = LittleFS.open("/sounds");
    if (dir && dir.isDirectory()) {
        for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
            const char* n = f.name();
            size_t len = strlen(n);
            if (len > 4 && len < sizeof(best) - 8 && strcasecmp(n + len - 4, ".wav") == 0 &&
                (!best[0] || strcmp(n, best) < 0))
                strcpy(best, n);
        }
    }
    if (!best[0]) {
        Serial.println("sound: no .wav in /sounds, the alarm will beep");
        return;
    }
    char path[64];
    snprintf(path, sizeof(path), "/sounds/%s", best);
    File f = LittleFS.open(path, "r");
    if (!f || !wav_parse(f)) {
        Serial.printf("sound: %s is not 16-bit PCM or mono IMA ADPCM WAV, the alarm will beep\n", path);
        return;
    }
    strcpy(wav_path, path);
    uint32_t frames = wav_format == WAV_PCM ? wav_len / (2 * wav_channels)
                                            : wav_len / wav_align * ((wav_align - 4) * 2 + 1);
    Serial.printf("sound: alarm sound %s, %s, %lu Hz, %u ch, %lu s\n", wav_path,
                  wav_format == WAV_PCM ? "PCM" : "IMA ADPCM", (unsigned long)wav_rate, wav_channels,
                  (unsigned long)(frames / wav_rate));
}

// Plays the WAV in a loop until stop is requested. False if it cannot be read (the caller beeps).
static bool play_wav(uint32_t start, int16_t* buf) {
    static int16_t in[(ADPCM_MAX_BLOCK - 4) * 2 + 1];   // decoded ADPCM block, or raw PCM
    static uint8_t blk[ADPCM_MAX_BLOCK];
    File f = LittleFS.open(wav_path, "r");
    if (!f || !f.seek(wav_off)) return false;
    uint32_t left = wav_len;                    // bytes left before looping
    size_t in_n = 0, in_i = 0;
    const uint32_t step = (uint32_t)((uint64_t)wav_rate * 65536 / SND_SAMPLE_RATE);   // 16.16
    uint32_t frac = 0;

    auto next = [&](int16_t* out) -> bool {    // next input sample, mono
        if (in_i >= in_n) {
            if (left == 0) {
                if (!f.seek(wav_off)) return false;
                left = wav_len;
            }
            if (wav_format == WAV_PCM) {
                size_t want = left < WAV_READ_FRAMES * 4 ? left : WAV_READ_FRAMES * 4;
                if (f.read((uint8_t*)in, want) != want) return false;
                left -= want;
                in_n = want / (2 * wav_channels);
            } else {
                size_t want = left < wav_align ? left : wav_align;   // the last block may be short
                if (f.read(blk, want) != want) return false;
                left -= want;
                in_n = adpcm_block(blk, want, in);
                if (in_n == 0) return false;
            }
            in_i = 0;
        }
        *out = wav_channels == 2 ? (int16_t)((in[2 * in_i] + in[2 * in_i + 1]) / 2) : in[in_i];
        in_i++;
        return true;
    };

    int16_t s0, s1;
    if (!next(&s0) || !next(&s1)) return false;
    while (!alarm_stop_req) {
        float level = ramp_level(start);
        for (int i = 0; i < TONE_CHUNK_FRAMES; i++) {
            int32_t v = s0 + (int32_t)(((int64_t)(s1 - s0) * (frac & 0xFFFF)) >> 16);
            buf[2 * i] = buf[2 * i + 1] = (int16_t)(v * level);
            frac += step;
            while (frac >= 0x10000) {
                frac -= 0x10000;
                s0 = s1;
                if (!next(&s1)) return false;
            }
        }
        i2s.write((uint8_t*)buf, TONE_CHUNK_FRAMES * 4);
    }
    return true;
}

static void play_beeps(uint32_t start, int16_t* buf) {
    const float step = 2.0f * (float)M_PI * ALARM_FREQ_HZ / SND_SAMPLE_RATE;
    float phase = 0.0f;
    while (!alarm_stop_req) {
        for (int seg = 0; seg < (int)sizeof(ALARM_PATTERN) && !alarm_stop_req; seg++) {
            bool on = (seg % 2) == 0;
            for (int c = 0; c < ALARM_PATTERN[seg] && !alarm_stop_req; c++) {
                float level = ramp_level(start);
                for (int i = 0; i < TONE_CHUNK_FRAMES; i++) {
                    int16_t v = on ? (int16_t)(sinf(phase) * 30000.0f * level) : 0;
                    buf[2 * i] = buf[2 * i + 1] = v;
                    phase += step;
                    if (phase > 2.0f * (float)M_PI) phase -= 2.0f * (float)M_PI;
                }
                i2s.write((uint8_t*)buf, TONE_CHUNK_FRAMES * 4);
            }
        }
    }
}

static void alarm_task(void* arg) {
    (void)arg;
    static int16_t buf[TONE_CHUNK_FRAMES * 2];
    uint32_t start = millis();

    amp_enable(true);
    delay(8);
    if (!wav_path[0] || !play_wav(start, buf)) {     // no file, or reading it failed
        if (wav_path[0]) Serial.printf("sound: reading %s failed, beeping instead\n", wav_path);
        play_beeps(start, buf);
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
    if (xTaskCreatePinnedToCore(alarm_task, "alarm", 6144, nullptr, 2, nullptr, 0) != pdPASS) {
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
    wav_find();
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
