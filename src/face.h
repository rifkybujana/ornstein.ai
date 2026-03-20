#ifndef FACE_H
#define FACE_H

#include <glad/gl.h>

typedef struct {
    float eye_openness;   /* 0.0 (closed) to 1.4 (wide) */
    float eye_scale;      /* 0.8 to 1.2, overall size multiplier */
    float pupil_scale;    /* 0.7 to 1.4 */
    float eye_squint;     /* 0.0 (normal) to 1.0 (half-closed from bottom) */
    float mouth_curve;    /* -1.0 (frown) to 1.0 (smile) */
    float mouth_openness; /* 0.0 (line) to 1.0 (open O) */
    float mouth_width;    /* 0.8 to 1.2 */
} FaceParams;

typedef enum {
    EMOTION_NEUTRAL,
    EMOTION_HAPPY,
    EMOTION_EXCITED,
    EMOTION_SURPRISED,
    EMOTION_SLEEPY,
    EMOTION_BORED,
    EMOTION_CURIOUS,
    EMOTION_SAD,
    EMOTION_COUNT
} Emotion;

/* Get the target parameter preset for a given emotion. */
const FaceParams *face_emotion_preset(Emotion e);

/* Lerp current params toward target by exponential smoothing. */
void face_params_lerp(FaceParams *current, const FaceParams *target, float speed, float dt);

#endif
