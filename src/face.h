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
    EMOTION_THINKING,
    EMOTION_COUNT
} Emotion;

/* Get the target parameter preset for a given emotion. */
const FaceParams *face_emotion_preset(Emotion e);

/* Lerp current params toward target by exponential smoothing. */
void face_params_lerp(FaceParams *current, const FaceParams *target, float speed, float dt);

typedef struct {
    FaceParams current;       /* the live interpolated params */
    Emotion    target_emotion;
    /* blink */
    float blink_timer;        /* countdown to next blink */
    float blink_phase;        /* 0 = not blinking, >0 = in blink */
    int   double_blink_pending; /* 1 = second blink queued */
    /* yawn */
    float yawn_timer;         /* countdown to next yawn (only in SLEEPY) */
    float yawn_phase;         /* 0 = not yawning, >0 = in yawn */
} FaceState;

/* Initialize face state to NEUTRAL. */
void face_init(FaceState *fs);

/* Update face: lerp toward target emotion, run blink/yawn timers.
   Returns the effective FaceParams to render (with blink/yawn overlaid). */
FaceParams face_update(FaceState *fs, float dt);

/* Must be called once after OpenGL context is ready. */
void face_render_init(void);

/* Draw the face centered at (cx, cy) in framebuffer pixels.
   mouse_x/mouse_y are cursor position in fb pixels.
   scale is fb_w / win_w for HiDPI. */
void face_render(const FaceParams *params, float cx, float cy,
                 float mouse_x, float mouse_y, float scale,
                 float fb_w, float fb_h);

/* Cleanup GL resources. */
void face_render_cleanup(void);

#endif
