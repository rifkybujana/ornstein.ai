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
    Emotion override_emotion;    /* current override, or -1 if none */
    float   override_timer;      /* seconds remaining */
    /* Random */
    float random_shift_timer;
    Emotion random_emotion;
} MoodState;

void mood_init(MoodState *ms);

/* Call each frame. Returns the emotion the face should target. */
Emotion mood_update(MoodState *ms, double mouse_x, double mouse_y, float dt);

/* Call from mouse button callback. */
void mood_on_click(MoodState *ms);

/* Call from cursor enter/leave callback. */
void mood_on_cursor_enter(MoodState *ms, int entered);

#endif
