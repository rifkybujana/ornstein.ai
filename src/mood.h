#ifndef MOOD_H
#define MOOD_H

#include "face.h"

typedef struct {
    /* Mouse tracking */
    double last_mouse_x, last_mouse_y;
    float  cursor_speed;         /* smoothed pixels/sec */
    float  idle_time;            /* seconds since last significant movement */
    int    cursor_in_window;
    int    mouse_clicked;        /* set to 1 for one frame on click */
    /* Override */
    int     override_emotion;    /* current override (Emotion value), or -1 if none */
    float   override_timer;      /* seconds remaining */
    /* Random */
    float random_shift_timer;
    Emotion random_emotion;
    /* System (polled) */
    float  system_poll_timer;
    float  battery_level;        /* 0.0-1.0, -1 if unknown */
    int    wifi_connected;
    float  cpu_usage;            /* 0.0-1.0 */
    /* Previous state (for transition detection) */
    int    prev_low_battery;
    int    prev_high_cpu;
    int    prev_wifi_connected;
} MoodState;

void mood_init(MoodState *ms);

/* Call each frame. Returns the emotion the face should target. */
Emotion mood_update(MoodState *ms, double mouse_x, double mouse_y, float dt);

/* Call from mouse button callback. */
void mood_on_click(MoodState *ms);

/* Call from cursor enter/leave callback. */
void mood_on_cursor_enter(MoodState *ms, int entered);

/* Set a mood override from LLM sentiment detection. */
void mood_set_sentiment_override(MoodState *ms, Emotion e, float duration);

#endif
