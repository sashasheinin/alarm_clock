#include "gesture.h"
#include <Arduino.h>
#include <math.h>
#include "alarm.h"
#include "board/board.h"

#define SAMPLE_MS       50       // 20 Hz
// Thresholds from 20 Hz recordings on the board (2026-10-22):
//   face down: z -1.00, x/y ~0.05. Tilted toward the face from bed: z -0.76, x 0.70.
//   shake score: still ~0.1, pick up / hold / put down 1.0-1.45, gentle shake 6-19, hard up to 40.
#define FACE_DOWN_Z     -0.85f   // g
#define FACE_DOWN_XY    0.45f    // g; x/y combined must stay below this (near flat)
#define FACE_DOWN_MS    700      // must stay face down this long
#define SHAKE_DECAY     0.9f     // score keeps 90% per sample (~0.5 s memory)
#define SHAKE_THRESHOLD 4.0f     // about 3x a pickup; a gentle shake passes it in < 0.5 s

static uint32_t last_ms   = 0;
static float    px, py, pz;      // previous sample
static bool     have_prev = false;
static float    score     = 0;   // decaying sum of sample-to-sample change, in g
static uint32_t down_since = 0;  // when the current face-down period started; 0 = not face down
static bool     armed     = false;   // a flip counts only if it starts while ringing
static bool     was_ringing = false;
static bool     log_on    = false;

void gesture_set_log(bool on) { log_on = on; }

void gesture_tick(void) {
    uint32_t now = millis();
    if (now - last_ms < SAMPLE_MS) return;
    last_ms = now;

    bool active = alarm_is_active();
    if (!active) {
        have_prev = false;
        was_ringing = false;
        return;
    }
    float ax, ay, az;
    if (!imu_read(&ax, &ay, &az)) return;

    bool ringing = alarm_is_ringing();
    bool face_down = az < FACE_DOWN_Z && sqrtf(ax * ax + ay * ay) < FACE_DOWN_XY;
    if (!face_down) down_since = 0;
    else if (!down_since) down_since = now;

    // Ringing just started (or restarted after a snooze): already face down doesn't count,
    // and movement from before doesn't either.
    if (ringing && !was_ringing) {
        armed = !face_down;
        score = 0;
    }
    was_ringing = ringing;
    if (!face_down) armed = true;

    if (have_prev) {
        float d = sqrtf((ax - px) * (ax - px) + (ay - py) * (ay - py) + (az - pz) * (az - pz));
        score = score * SHAKE_DECAY + d;
    }
    px = ax; py = ay; pz = az;
    have_prev = true;

    if (log_on && now % 200 < SAMPLE_MS)
        Serial.printf("gesture: score %.2f z %.2f%s\n", score, az, face_down ? " DOWN" : "");
    if (!ringing) return;

    const char* why = NULL;
    if (armed && down_since && now - down_since >= FACE_DOWN_MS) why = "face down";
    else if (score >= SHAKE_THRESHOLD) why = "shake";
    if (!why) return;

    if (alarm_snooze()) Serial.printf("gesture: snooze by %s\n", why);
    else Serial.printf("gesture: %s, but no snoozes left\n", why);
    armed = false;               // face down again needs a turn up first
    score = 0;
}
