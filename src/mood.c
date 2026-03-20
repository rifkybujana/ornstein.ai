#include "mood.h"
#include "platform.h"
#include <math.h>
#include <stdlib.h>
#include <time.h>

static float randf(float lo, float hi) {
    return lo + (float)rand() / (float)RAND_MAX * (hi - lo);
}

void mood_init(MoodState *ms) {
    ms->last_mouse_x = 0;
    ms->last_mouse_y = 0;
    ms->cursor_speed = 0.0f;
    ms->idle_time = 0.0f;
    ms->cursor_in_window = 1;
    ms->mouse_clicked = 0;
    ms->override_emotion = -1;
    ms->override_timer = 0.0f;
    ms->random_shift_timer = randf(15.0f, 30.0f);
    ms->random_emotion = EMOTION_NEUTRAL;
    ms->system_poll_timer = 0.0f;
    ms->battery_level = -1.0f;
    ms->wifi_connected = 1;
    ms->cpu_usage = 0.0f;
    ms->prev_low_battery = 0;
    ms->prev_high_cpu = 0;
    ms->prev_wifi_connected = 1;
    srand((unsigned)time(NULL));
}

void mood_on_click(MoodState *ms) {
    ms->override_emotion = EMOTION_SURPRISED;
    ms->override_timer = 1.0f;
}

void mood_on_cursor_enter(MoodState *ms, int entered) {
    ms->cursor_in_window = entered;
}

static Emotion time_of_day_mood(void) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    int hour = t->tm_hour;
    if (hour >= 6 && hour < 12)  return EMOTION_HAPPY;
    if (hour >= 12 && hour < 18) return EMOTION_NEUTRAL;
    if (hour >= 18 && hour < 22) return EMOTION_BORED;
    return EMOTION_SLEEPY;
}

Emotion mood_update(MoodState *ms, double mouse_x, double mouse_y, float dt) {
    /* Compute cursor speed */
    double dx = mouse_x - ms->last_mouse_x;
    double dy = mouse_y - ms->last_mouse_y;
    float raw_speed = (float)sqrt(dx * dx + dy * dy) / fmaxf(dt, 0.001f);
    float smooth = 1.0f - expf(-5.0f * dt);
    ms->cursor_speed = ms->cursor_speed + (raw_speed - ms->cursor_speed) * smooth;
    ms->last_mouse_x = mouse_x;
    ms->last_mouse_y = mouse_y;

    /* Idle tracking */
    if (ms->cursor_speed > 20.0f) {
        ms->idle_time = 0.0f;
    } else {
        ms->idle_time += dt;
    }

    /* Handle click (consume after one frame) */
    if (ms->mouse_clicked) {
        ms->mouse_clicked = 0;
    }

    /* Poll system state once per second */
    ms->system_poll_timer -= dt;
    if (ms->system_poll_timer <= 0.0f) {
        ms->system_poll_timer = 1.0f;
        ms->battery_level = platform_battery_level();
        ms->wifi_connected = platform_wifi_connected();
        ms->cpu_usage = platform_cpu_usage();

        /* Detect transitions and trigger overrides */
        int low_bat = (ms->battery_level >= 0.0f && ms->battery_level < 0.2f);
        if (low_bat && !ms->prev_low_battery) {
            ms->override_emotion = EMOTION_SAD;
            ms->override_timer = 5.0f;
        }
        ms->prev_low_battery = low_bat;

        int high_cpu = (ms->cpu_usage > 0.8f);
        if (high_cpu && !ms->prev_high_cpu) {
            ms->override_emotion = EMOTION_EXCITED;
            ms->override_timer = 3.0f;
        }
        ms->prev_high_cpu = high_cpu;

        if (!ms->wifi_connected && ms->prev_wifi_connected) {
            ms->override_emotion = EMOTION_SURPRISED;
            ms->override_timer = 2.0f;
        }
        ms->prev_wifi_connected = ms->wifi_connected;
    }

    /* Priority 1: Override (click surprise, system events) */
    if (ms->override_timer > 0.0f) {
        ms->override_timer -= dt;
        if (ms->override_timer > 0.0f)
            return ms->override_emotion;
        ms->override_emotion = -1;
    }

    /* Priority 2: Mouse behavior */
    if (ms->cursor_speed > 500.0f)
        return EMOTION_EXCITED;

    if (ms->cursor_in_window && ms->cursor_speed > 20.0f)
        return EMOTION_HAPPY;

    /* Priority 3: Background mood */

    /* Random shifts */
    ms->random_shift_timer -= dt;
    if (ms->random_shift_timer <= 0.0f) {
        ms->random_shift_timer = randf(15.0f, 30.0f);
        if (randf(0.0f, 1.0f) < 0.3f) {
            ms->override_emotion = EMOTION_CURIOUS;
            ms->override_timer = randf(3.0f, 5.0f);
            return EMOTION_CURIOUS;
        }
    }

    /* Idle escalation */
    if (ms->idle_time > 60.0f) return EMOTION_SLEEPY;
    if (ms->idle_time > 15.0f) return EMOTION_BORED;

    /* Time of day baseline */
    return time_of_day_mood();
}

void mood_set_sentiment_override(MoodState *ms, Emotion e, float duration) {
    if (!ms) return;
    ms->override_emotion = e;
    ms->override_timer = duration;
}
