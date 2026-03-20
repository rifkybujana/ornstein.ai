# Expressive Desktop Pet — Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Transform the current cursor-tracking eyes into a full expressive desktop pet with emotions, a mouth, blinking, and reactive moods.

**Architecture:** State machine of named emotions, each mapping to a set of continuous face parameters (eye openness, pupil scale, mouth curve, etc). Parameters lerp smoothly toward targets each frame. A priority-based mood system evaluates triggers (mouse, time, system events, randomness) and sets the active emotion. Rendering is all procedural OpenGL (ellipses + bezier curves).

**Tech Stack:** C11, OpenGL 4.1 Core, GLFW 3, GLAD, Cocoa (macOS platform layer)

---

### Task 1: Extract face module — FaceParams struct and emotion presets

**Files:**
- Create: `src/face.h`
- Create: `src/face.c`
- Modify: `CMakeLists.txt:15` (add face.c to sources)

**Step 1: Create `src/face.h`**

```c
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
```

**Step 2: Create `src/face.c`**

```c
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
```

**Step 3: Add `src/face.c` to CMakeLists.txt**

Change line 15 from:
```cmake
add_executable(ornstein src/main.c src/platform_macos.m)
```
to:
```cmake
add_executable(ornstein src/main.c src/face.c src/platform_macos.m)
```

**Step 4: Build to verify compilation**

Run: `cmake -B build && cmake --build build`
Expected: compiles with no errors

**Step 5: Commit**

```bash
git add src/face.h src/face.c CMakeLists.txt
git commit -m "feat: add FaceParams struct and emotion presets"
```

---

### Task 2: Add blink animation to face module

**Files:**
- Modify: `src/face.h` (add blink state and face_update function)
- Modify: `src/face.c` (implement blink timer logic)

**Step 1: Add blink state to `src/face.h`**

Add after the `face_params_lerp` declaration:

```c
typedef struct {
    FaceParams current;       /* the live interpolated params */
    Emotion    target_emotion;
    /* blink */
    float blink_timer;        /* countdown to next blink */
    float blink_phase;        /* 0 = not blinking, >0 = in blink */
    /* yawn */
    float yawn_timer;         /* countdown to next yawn (only in SLEEPY) */
    float yawn_phase;         /* 0 = not yawning, >0 = in yawn */
} FaceState;

/* Initialize face state to NEUTRAL. */
void face_init(FaceState *fs);

/* Update face: lerp toward target emotion, run blink/yawn timers.
   Returns the effective FaceParams to render (with blink/yawn overlaid). */
FaceParams face_update(FaceState *fs, float dt);
```

**Step 2: Implement in `src/face.c`**

Add at the bottom of face.c:

```c
#include <stdlib.h>

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
            ? elapsed / half            /* closing: 0→1 */
            : 1.0f - (elapsed - half) / half;  /* opening: 1→0 */
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
        float norm = elapsed / YAWN_DURATION; /* 0→1 */
        float intensity = sinf(norm * 3.14159f); /* 0→1→0 */
        result.mouth_openness = fmaxf(result.mouth_openness, intensity);
        result.eye_openness *= (1.0f - 0.7f * intensity);
        if (fs->yawn_phase < 0.0f) fs->yawn_phase = 0.0f;
    }

    return result;
}
```

**Step 3: Build to verify**

Run: `cmake --build build`
Expected: compiles with no errors

**Step 4: Commit**

```bash
git add src/face.h src/face.c
git commit -m "feat: add blink and yawn animations to face module"
```

---

### Task 3: Add face rendering — expressive eyes with ellipse sclera

**Files:**
- Modify: `src/face.h` (add render functions)
- Modify: `src/face.c` (implement rendering)

**Step 1: Add render declarations to `src/face.h`**

Add after the `face_update` declaration:

```c
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
```

**Step 2: Implement rendering in `src/face.c`**

Move the existing ellipse geometry, shader, and draw_circle code from main.c into face.c. Adapt `draw_circle` into `draw_ellipse` that takes separate rx/ry. Add the eye rendering logic that uses FaceParams to control eye height, pupil size, and squint. Keep the cursor-tracking pupil orbit from the existing code.

Key rendering logic:
- Sclera: `draw_ellipse(ex, ey, eye_rx * eye_scale * scale, eye_ry * eye_openness * eye_scale * scale)`
- When `eye_squint > 0`: render a background-colored rect over the bottom of the sclera to simulate squint
- Pupil: orbit toward cursor, radius = `base_pupil_r * pupil_scale * scale`
- Pupil dampening: multiply orbit distance by `eye_openness` so pupil doesn't float when eyes are closed

**Step 3: Build and run**

Run: `cmake --build build && ./build/ornstein`
Expected: eyes render as before but now driven by FaceParams

**Step 4: Commit**

```bash
git add src/face.h src/face.c src/main.c
git commit -m "feat: move eye rendering into face module with param-driven sizing"
```

---

### Task 4: Add mouth rendering — bezier curve

**Files:**
- Modify: `src/face.c` (add bezier curve renderer, mouth drawing)

**Step 1: Add bezier line-strip geometry**

Create a new VBO for the mouth. Generate a quadratic bezier from 3 control points as a triangle strip with configurable thickness. The 3 points:
- Left corner: `(cx - mouth_half_w, mouth_y)`
- Center: `(cx, mouth_y + curve_offset)` where `curve_offset = -mouth_curve * curve_amplitude`
- Right corner: `(cx + mouth_half_w, mouth_y)`

For `mouth_openness > 0`: split into top arc and bottom arc. Top arc curves up slightly, bottom arc curves down by `openness * open_amplitude`. Fill the area between them with triangles to make an open mouth shape.

**Step 2: Integrate into `face_render`**

Draw mouth after eyes. Position it below eye center by a fixed offset (e.g. `90 * scale` pixels below center).

**Step 3: Build and run**

Run: `cmake --build build && ./build/ornstein`
Expected: a small curved line (neutral = straight) appears below the eyes

**Step 4: Commit**

```bash
git add src/face.c
git commit -m "feat: add bezier mouth rendering with curve and openness"
```

---

### Task 5: Wire face module into main loop

**Files:**
- Modify: `src/main.c` (replace inline eye code with face module calls)

**Step 1: Replace the render loop in main.c**

Remove all inline eye rendering code (the ellipse geometry, shader, draw_circle, lerpf, lerp_angle, and the eye drawing loop). Replace with:

```c
#include "face.h"

/* In main(), before the loop: */
FaceState face;
face_init(&face);
face_render_init();

/* In the loop: */
face.target_emotion = EMOTION_NEUTRAL; /* will be set by mood system later */
FaceParams params = face_update(&face, dt);
face_render(&params, center_x, center_y, mx, my, scale, (float)fb_w, (float)fb_h);

/* Cleanup: */
face_render_cleanup();
```

main.c should now only contain: window setup, callbacks, main loop orchestration, and cleanup. All face logic lives in face.c.

**Step 2: Build and run**

Run: `cmake --build build && ./build/ornstein`
Expected: eyes + mouth render, blink periodically, expression is NEUTRAL

**Step 3: Manually test emotions**

Temporarily add key bindings to cycle emotions:
- Press 1-8 to set `face.target_emotion` to each emotion
- Verify each expression transitions smoothly
- Verify blink and yawn (set to SLEEPY and wait)

**Step 4: Commit**

```bash
git add src/main.c
git commit -m "feat: wire face module into main loop with keyboard emotion testing"
```

---

### Task 6: Create mood module — mouse and idle triggers

**Files:**
- Create: `src/mood.h`
- Create: `src/mood.c`
- Modify: `CMakeLists.txt` (add mood.c)

**Step 1: Create `src/mood.h`**

```c
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
    /* System (polled) */
    float  battery_level;        /* 0.0–1.0, -1 if unknown */
    int    wifi_connected;
    float  cpu_usage;            /* 0.0–1.0 */
    float  system_poll_timer;
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
```

**Step 2: Create `src/mood.c`**

Implement the priority system:
1. If `override_timer > 0`, return `override_emotion` (system events and click-surprise)
2. Else evaluate mouse: fast cursor → EXCITED, hovering in window → HAPPY
3. Else background mood: time-of-day + idle escalation + random shifts

The mouse speed is computed as a smoothed delta: `cursor_speed = lerp(cursor_speed, raw_speed, 1 - exp(-5 * dt))`. Thresholds: speed > 800px/s = EXCITED.

Idle escalation: track `idle_time` (reset when speed > 20px/s). 30s → BORED, 120s → SLEEPY.

Random shifts: every 15-30s, 30% chance to briefly go CURIOUS for 3-5s.

Time of day: `localtime()` to get hour. 6-12=HAPPY, 12-18=NEUTRAL, 18-22=BORED, 22-6=SLEEPY. Only used as the base when no other trigger fires.

**Step 3: Add `src/mood.c` to CMakeLists.txt**

```cmake
add_executable(ornstein src/main.c src/face.c src/mood.c src/platform_macos.m)
```

**Step 4: Build to verify**

Run: `cmake -B build && cmake --build build`
Expected: compiles with no errors

**Step 5: Commit**

```bash
git add src/mood.h src/mood.c CMakeLists.txt
git commit -m "feat: add mood module with mouse, idle, time-of-day, and random triggers"
```

---

### Task 7: Wire mood into main loop and add input callbacks

**Files:**
- Modify: `src/main.c` (integrate mood, add click and cursor-enter callbacks)

**Step 1: Wire MoodState into main loop**

```c
#include "mood.h"

/* Before loop: */
MoodState mood;
mood_init(&mood);

/* In loop: */
Emotion target = mood_update(&mood, mouse_x, mouse_y, dt);
face.target_emotion = target;
```

**Step 2: Add GLFW callbacks for click and cursor enter/leave**

```c
static MoodState *g_mood; /* set before registering callbacks */

static void mouse_button_cb(GLFWwindow *w, int button, int action, int mods) {
    (void)w; (void)mods;
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS)
        mood_on_click(g_mood);
}

static void cursor_enter_cb(GLFWwindow *w, int entered) {
    (void)w;
    mood_on_cursor_enter(g_mood, entered);
}
```

Register with `glfwSetMouseButtonCallback` and `glfwSetCursorEnterCallback`.

**Step 3: Remove the temporary keyboard emotion shortcuts** (from Task 5)

**Step 4: Build and run**

Run: `cmake --build build && ./build/ornstein`
Expected: pet reacts to mouse speed, goes bored/sleepy when idle, surprised on click, happy on hover

**Step 5: Commit**

```bash
git add src/main.c
git commit -m "feat: wire mood system into main loop with mouse callbacks"
```

---

### Task 8: Add platform system queries (battery, CPU, WiFi)

**Files:**
- Modify: `include/platform.h` (add system query declarations)
- Modify: `src/platform_macos.m` (implement with Cocoa/IOKit)

**Step 1: Add declarations to `include/platform.h`**

```c
/* Returns battery level 0.0–1.0, or -1.0 if not available. */
float platform_battery_level(void);

/* Returns 1 if WiFi is connected, 0 otherwise. */
int platform_wifi_connected(void);

/* Returns CPU usage 0.0–1.0 (system-wide). */
float platform_cpu_usage(void);
```

**Step 2: Implement in `src/platform_macos.m`**

- Battery: use `IOPSCopyPowerSourcesInfo` / `IOPSCopyPowerSourcesList` from IOKit
- WiFi: use `CWWiFiClient` from CoreWLAN framework
- CPU: use `host_statistics` with `HOST_CPU_LOAD_INFO` from mach/mach.h

**Step 3: Add CoreWLAN framework to CMakeLists.txt**

```cmake
"-framework CoreWLAN"
```

**Step 4: Build to verify**

Run: `cmake -B build && cmake --build build`
Expected: compiles, links with no errors

**Step 5: Commit**

```bash
git add include/platform.h src/platform_macos.m CMakeLists.txt
git commit -m "feat: add macOS platform queries for battery, WiFi, CPU"
```

---

### Task 9: Wire system events into mood module

**Files:**
- Modify: `src/mood.c` (poll platform queries, trigger overrides)

**Step 1: Add system event polling to `mood_update`**

Poll once per second (`system_poll_timer`). Check:
- `platform_battery_level() < 0.2` → set override to SAD for 5s
- `platform_cpu_usage() > 0.8` → set override to EXCITED for 3s
- `!platform_wifi_connected()` → set override to SURPRISED for 2s, then SAD for 3s

Only trigger each event once (not every poll). Track previous state to detect transitions.

**Step 2: Build and run**

Run: `cmake --build build && ./build/ornstein`
Expected: pet reacts to system state changes

**Step 3: Commit**

```bash
git add src/mood.c
git commit -m "feat: system event triggers for battery, CPU, WiFi in mood module"
```

---

### Task 10: Polish and tune

**Files:**
- Modify: `src/face.c` (tune preset values, transition speeds)
- Modify: `src/mood.c` (tune thresholds, timing)

**Step 1: Playtest each emotion**

Run the app and cycle through scenarios:
- Move mouse fast → should feel snappy excited
- Leave idle for 30s → gradual bored, then sleepy
- Click → quick surprised flash
- Hover → warm happy

**Step 2: Tune values**

Adjust lerp speeds, preset values, and thresholds based on feel. The design doc values are starting points — real tuning happens by looking at it.

**Step 3: Final commit**

```bash
git add src/face.c src/mood.c
git commit -m "polish: tune expression presets and mood trigger thresholds"
```
