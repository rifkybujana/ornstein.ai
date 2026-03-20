#ifndef UTIL_H
#define UTIL_H

#include <stdlib.h>

static inline float lerpf(float a, float b, float t) {
    return a + (b - a) * t;
}

static inline float randf(float lo, float hi) {
    return lo + (float)rand() / (float)RAND_MAX * (hi - lo);
}

static inline float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

#endif
