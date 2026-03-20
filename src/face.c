#include "face.h"
#include <math.h>

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
