#ifndef MOOD_COLORS_H
#define MOOD_COLORS_H

#include "face.h"

typedef struct { float r, g, b; } MoodColor;

static const MoodColor mood_colors[EMOTION_COUNT] = {
    [EMOTION_NEUTRAL]   = {0.40f, 0.40f, 0.45f},
    [EMOTION_HAPPY]     = {0.90f, 0.80f, 0.30f},
    [EMOTION_EXCITED]   = {1.00f, 0.60f, 0.20f},
    [EMOTION_SURPRISED] = {0.90f, 0.90f, 1.00f},
    [EMOTION_SLEEPY]    = {0.20f, 0.30f, 0.70f},
    [EMOTION_BORED]     = {0.30f, 0.30f, 0.35f},
    [EMOTION_CURIOUS]   = {0.20f, 0.70f, 0.70f},
    [EMOTION_SAD]       = {0.30f, 0.40f, 0.80f},
    [EMOTION_THINKING]  = {0.50f, 0.30f, 0.80f},
};

static inline void mood_color_get(Emotion e, float *r, float *g, float *b) {
    int idx = ((int)e >= 0 && (int)e < EMOTION_COUNT) ? (int)e : 0;
    *r = mood_colors[idx].r;
    *g = mood_colors[idx].g;
    *b = mood_colors[idx].b;
}

#endif
