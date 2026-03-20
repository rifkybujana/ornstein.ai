#include "face.h"
#include <math.h>
#include <stdlib.h>

static const FaceParams presets[EMOTION_COUNT] = {
    [EMOTION_NEUTRAL]   = {1.0f, 1.0f,  1.0f, 0.0f,  0.0f, 0.0f, 1.0f},
    [EMOTION_HAPPY]     = {1.0f, 1.0f,  1.1f, 0.2f,  1.0f, 0.0f, 1.2f},
    [EMOTION_EXCITED]   = {1.2f, 1.15f, 1.3f, 0.0f,  0.8f, 0.3f, 1.1f},
    [EMOTION_SURPRISED] = {1.4f, 1.2f,  1.4f, 0.0f,  0.0f, 1.0f, 0.8f},
    [EMOTION_SLEEPY]    = {0.3f, 0.9f,  0.7f, 0.5f,  0.1f, 0.0f, 0.9f},
    [EMOTION_BORED]     = {0.7f, 0.95f, 0.8f, 0.3f, -0.2f, 0.0f, 1.0f},
    [EMOTION_CURIOUS]   = {1.1f, 1.05f, 1.2f, 0.0f,  0.2f, 0.1f, 0.9f},
    [EMOTION_SAD]       = {0.8f, 0.95f, 0.9f, 0.0f, -0.7f, 0.0f, 0.8f},
};

const FaceParams *face_emotion_preset(Emotion e) {
    if (e < 0 || e >= EMOTION_COUNT) e = EMOTION_NEUTRAL;
    return &presets[e];
}

static float lerpf(float a, float b, float t) {
    return a + (b - a) * t;
}

void face_params_lerp(FaceParams *cur, const FaceParams *tgt, float speed, float dt) {
    float t = 1.0f - expf(-speed * dt);
    cur->eye_openness   = lerpf(cur->eye_openness,   tgt->eye_openness,   t);
    cur->eye_scale      = lerpf(cur->eye_scale,      tgt->eye_scale,      t);
    cur->pupil_scale    = lerpf(cur->pupil_scale,     tgt->pupil_scale,    t);
    cur->eye_squint     = lerpf(cur->eye_squint,      tgt->eye_squint,     t);
    cur->mouth_curve    = lerpf(cur->mouth_curve,     tgt->mouth_curve,    t);
    cur->mouth_openness = lerpf(cur->mouth_openness,  tgt->mouth_openness, t);
    cur->mouth_width    = lerpf(cur->mouth_width,     tgt->mouth_width,    t);
}

static float randf(float lo, float hi) {
    return lo + (float)rand() / (float)RAND_MAX * (hi - lo);
}

void face_init(FaceState *fs) {
    fs->current = presets[EMOTION_NEUTRAL];
    fs->target_emotion = EMOTION_NEUTRAL;
    fs->blink_timer = randf(2.0f, 6.0f);
    fs->blink_phase = 0.0f;
    fs->yawn_timer = randf(8.0f, 15.0f);
    fs->yawn_phase = 0.0f;
}

FaceParams face_update(FaceState *fs, float dt) {
    const float LERP_SPEED = 8.0f;
    const float BLINK_DURATION = 0.15f;
    const float YAWN_DURATION = 2.0f;

    /* Lerp toward target emotion */
    const FaceParams *target = &presets[fs->target_emotion];
    face_params_lerp(&fs->current, target, LERP_SPEED, dt);

    FaceParams result = fs->current;

    /* Blink timer */
    fs->blink_timer -= dt;
    if (fs->blink_timer <= 0.0f && fs->blink_phase <= 0.0f) {
        fs->blink_phase = BLINK_DURATION;
        fs->blink_timer = randf(2.0f, 6.0f);
    }
    if (fs->blink_phase > 0.0f) {
        fs->blink_phase -= dt;
        /* Triangle wave: closes then opens */
        float half = BLINK_DURATION * 0.5f;
        float elapsed = BLINK_DURATION - fs->blink_phase;
        float blink_t = (elapsed < half)
            ? elapsed / half            /* closing: 0->1 */
            : 1.0f - (elapsed - half) / half;  /* opening: 1->0 */
        result.eye_openness *= (1.0f - blink_t);
        if (fs->blink_phase < 0.0f) fs->blink_phase = 0.0f;
    }

    /* Yawn (only when sleepy) */
    if (fs->target_emotion == EMOTION_SLEEPY) {
        fs->yawn_timer -= dt;
        if (fs->yawn_timer <= 0.0f && fs->yawn_phase <= 0.0f) {
            fs->yawn_phase = YAWN_DURATION;
            fs->yawn_timer = randf(8.0f, 15.0f);
        }
    }
    if (fs->yawn_phase > 0.0f) {
        fs->yawn_phase -= dt;
        /* Bell curve: ramp up, hold, ramp down */
        float elapsed = YAWN_DURATION - fs->yawn_phase;
        float norm = elapsed / YAWN_DURATION; /* 0->1 */
        float intensity = sinf(norm * 3.14159f); /* 0->1->0 */
        result.mouth_openness = fmaxf(result.mouth_openness, intensity);
        result.eye_openness *= (1.0f - 0.7f * intensity);
        if (fs->yawn_phase < 0.0f) fs->yawn_phase = 0.0f;
    }

    return result;
}
