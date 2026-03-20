# UI Improvements Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add expressiveness (thinking emotion, double-blink, breathing glow), ambient life (particles, bobbing, reflection), improved chat UX (bubbles, animations, typing dots), stats system (Bond/Joy/Energy with persistence), and mood detection from LLM responses.

**Architecture:** Modular — new `ambient.c`, `stats.c`, `sentiment.c` modules alongside existing code. Surgical changes to `face.c`, `chat.c`, `mood.c`, `llm.c`, `main.c`. All rendering uses OpenGL 4.1 Core with the existing shader patterns.

**Tech Stack:** C11, OpenGL 4.1, GLFW 3, GLAD, libcurl, pthreads, CMake

**Design doc:** `docs/plans/2026-03-20-ui-improvements-design.md`

---

### Task 1: Add EMOTION_THINKING to face module

**Files:**
- Modify: `src/face.h:16-26` (Emotion enum)
- Modify: `src/face.c:7-16` (presets array)

**Step 1: Add EMOTION_THINKING to the enum**

In `src/face.h`, add `EMOTION_THINKING` before `EMOTION_COUNT`:

```c
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
```

**Step 2: Add the thinking preset**

In `src/face.c`, add to the `presets` array after the SAD entry:

```c
[EMOTION_THINKING]  = {0.8f, 1.0f, 0.8f, 0.15f, 0.1f, 0.05f, 0.9f},
```

**Step 3: Build to verify**

Run: `cmake --build build`
Expected: Clean compile, no errors.

**Step 4: Update emotion_name in chat.c**

In `src/chat.c:159-166`, update the `names` array to include "thinking":

```c
static const char *emotion_name(Emotion e) {
    static const char *names[] = {
        "neutral", "happy", "excited", "surprised",
        "sleepy", "bored", "curious", "sad", "thinking"
    };
    if (e >= 0 && e < EMOTION_COUNT) return names[e];
    return "neutral";
}
```

**Step 5: Build and commit**

Run: `cmake --build build`

```bash
git add src/face.h src/face.c src/chat.c
git commit -m "feat: add EMOTION_THINKING as 9th emotion"
```

---

### Task 2: Add double-blink to face.c

**Files:**
- Modify: `src/face.h:34-43` (FaceState struct)
- Modify: `src/face.c:42-49,60-78` (face_init, face_update)

**Step 1: Add double_blink_pending field to FaceState**

In `src/face.h`, add after `float blink_phase;`:

```c
int   double_blink_pending; /* 1 = second blink queued */
```

**Step 2: Initialize in face_init**

In `src/face.c` `face_init()`, add:

```c
fs->double_blink_pending = 0;
```

**Step 3: Add double-blink logic in face_update**

Replace the blink timer block in `face_update()` (lines 63-78) with:

```c
/* Blink timer */
fs->blink_timer -= dt;
if (fs->blink_timer <= 0.0f && fs->blink_phase <= 0.0f) {
    fs->blink_phase = BLINK_DURATION;
    if (fs->double_blink_pending) {
        fs->double_blink_pending = 0;
        fs->blink_timer = randf(2.0f, 6.0f);
    } else {
        /* 20% chance of double blink */
        if (randf(0.0f, 1.0f) < 0.2f) {
            fs->double_blink_pending = 1;
            fs->blink_timer = 0.15f; /* short gap before second blink */
        } else {
            fs->blink_timer = randf(2.0f, 6.0f);
        }
    }
}
if (fs->blink_phase > 0.0f) {
    fs->blink_phase -= dt;
    float half = BLINK_DURATION * 0.5f;
    float elapsed = BLINK_DURATION - fs->blink_phase;
    float blink_t = (elapsed < half)
        ? elapsed / half
        : 1.0f - (elapsed - half) / half;
    result.eye_openness *= (1.0f - blink_t);
    if (fs->blink_phase < 0.0f) fs->blink_phase = 0.0f;
}
```

**Step 4: Build and commit**

Run: `cmake --build build`

```bash
git add src/face.h src/face.c
git commit -m "feat: add double-blink with 20% chance"
```

---

### Task 3: Create sentiment module

**Files:**
- Create: `src/sentiment.h`
- Create: `src/sentiment.c`

**Step 1: Create sentiment.h**

```c
#ifndef SENTIMENT_H
#define SENTIMENT_H

#include "face.h"

/* Parse a [MOOD:xxx] tag from the start of text.
   Returns 1 if found, sets *out_emotion and *tag_end (index past the tag).
   Returns 0 if no tag found. */
int sentiment_parse(const char *text, Emotion *out_emotion, int *tag_end);

#endif
```

**Step 2: Create sentiment.c**

```c
#include "sentiment.h"
#include <string.h>
#include <ctype.h>

static const struct {
    const char *name;
    Emotion emotion;
} mood_map[] = {
    {"neutral",   EMOTION_NEUTRAL},
    {"happy",     EMOTION_HAPPY},
    {"excited",   EMOTION_EXCITED},
    {"surprised", EMOTION_SURPRISED},
    {"sleepy",    EMOTION_SLEEPY},
    {"bored",     EMOTION_BORED},
    {"curious",   EMOTION_CURIOUS},
    {"sad",       EMOTION_SAD},
    {"thinking",  EMOTION_THINKING},
};

#define MOOD_MAP_COUNT (sizeof(mood_map) / sizeof(mood_map[0]))

int sentiment_parse(const char *text, Emotion *out_emotion, int *tag_end) {
    if (!text) return 0;

    const char *p = text;
    while (*p == ' ' || *p == '\n' || *p == '\t') p++;

    if (strncmp(p, "[MOOD:", 6) != 0) return 0;
    p += 6;

    const char *close = strchr(p, ']');
    if (!close) return 0;

    int name_len = (int)(close - p);
    if (name_len <= 0 || name_len > 20) return 0;

    char name[21];
    for (int i = 0; i < name_len; i++)
        name[i] = (char)tolower((unsigned char)p[i]);
    name[name_len] = '\0';

    for (int i = 0; i < (int)MOOD_MAP_COUNT; i++) {
        if (strcmp(name, mood_map[i].name) == 0) {
            if (out_emotion) *out_emotion = mood_map[i].emotion;
            int end = (int)(close - text) + 1;
            /* Skip whitespace after tag */
            while (text[end] == ' ') end++;
            if (tag_end) *tag_end = end;
            return 1;
        }
    }

    return 0;
}
```

**Step 3: Build to verify**

Update `CMakeLists.txt` line 28 to include `src/sentiment.c`:

```cmake
add_executable(ornstein src/main.c src/face.c src/mood.c src/platform_macos.m
                        src/text.c src/llm.c src/chat.c src/sentiment.c)
```

Run: `cmake --build build`
Expected: Clean compile.

**Step 4: Commit**

```bash
git add src/sentiment.h src/sentiment.c CMakeLists.txt
git commit -m "feat: add sentiment parser for [MOOD:xxx] tags"
```

---

### Task 4: Add sentiment override to mood module

**Files:**
- Modify: `src/mood.h:31-39`
- Modify: `src/mood.c`

**Step 1: Add mood_set_sentiment_override declaration**

In `src/mood.h`, add after `mood_on_cursor_enter`:

```c
/* Set a mood override from LLM sentiment detection. */
void mood_set_sentiment_override(MoodState *ms, Emotion e, float duration);
```

**Step 2: Implement in mood.c**

Add at the end of `src/mood.c`, before the closing (after `mood_update`):

```c
void mood_set_sentiment_override(MoodState *ms, Emotion e, float duration) {
    if (!ms) return;
    ms->override_emotion = e;
    ms->override_timer = duration;
}
```

**Step 3: Build and commit**

Run: `cmake --build build`

```bash
git add src/mood.h src/mood.c
git commit -m "feat: add mood_set_sentiment_override for LLM mood tagging"
```

---

### Task 5: Update LLM system prompt with mood tag instruction

**Files:**
- Modify: `src/llm.c:494-501` (build_request function)

**Step 1: Update the system prompt**

In `src/llm.c`, replace the `build_request` function's `system_prompt` construction:

```c
static void build_request(const char *mood, const char *user_msg) {
    char system_prompt[512];
    snprintf(system_prompt, sizeof(system_prompt),
             "You are a cute desktop pet named Ornstein. "
             "You speak in short, expressive sentences. "
             "You're currently feeling %s. "
             "Keep responses under 2 sentences. "
             "Before your response, tag your mood: [MOOD:emotion]. "
             "Valid emotions: neutral, happy, excited, surprised, "
             "sleepy, bored, curious, sad, thinking.", mood);
```

**Step 2: Build and commit**

Run: `cmake --build build`

```bash
git add src/llm.c
git commit -m "feat: add mood tag instruction to LLM system prompt"
```

---

### Task 6: Integrate sentiment parsing in chat.c

**Files:**
- Modify: `src/chat.c` (add include, modify on_token callback)

**Step 1: Add include**

At the top of `src/chat.c`, after `#include "llm.h"`:

```c
#include "sentiment.h"
#include "mood.h"
```

**Step 2: Add external mood state pointer**

After the includes, add:

```c
/* External mood state for sentiment overrides */
static MoodState *chat_mood_state = NULL;

void chat_set_mood_state(MoodState *ms) {
    chat_mood_state = ms;
}
```

**Step 3: Add declaration to chat.h**

In `src/chat.h`, add:

```c
/* Set mood state pointer for sentiment-driven mood overrides. */
void chat_set_mood_state(MoodState *ms);
```

Also add at the top of chat.h, before the ChatState forward decl:

```c
#include "mood.h"
```

**Step 4: Modify on_token callback to parse sentiment on completion**

In the `on_token` function in `src/chat.c`, replace the `done` block (lines 199-222) with:

```c
    } else {
        /* Generation complete — commit thinking (last portion) then response */
        if (cs->thinking_len > 0 && cs->msg_count < CHAT_MAX_MESSAGES) {
            ChatMessage *msg = &cs->messages[cs->msg_count++];
            int copy_len = cs->thinking_len;
            const char *src = cs->pending_thinking;
            if (copy_len >= CHAT_MAX_MSG_LEN) {
                src += copy_len - (CHAT_MAX_MSG_LEN - 1);
                copy_len = CHAT_MAX_MSG_LEN - 1;
            }
            memcpy(msg->text, src, copy_len);
            msg->text[copy_len] = '\0';
            msg->is_user = 0;
            msg->is_thinking = 1;
        }
        if (cs->pending_len > 0 && cs->msg_count < CHAT_MAX_MESSAGES) {
            /* Parse mood tag from response */
            Emotion detected;
            int tag_end = 0;
            const char *response_text = cs->pending_response;
            if (sentiment_parse(response_text, &detected, &tag_end)) {
                response_text += tag_end; /* strip tag from display */
                if (chat_mood_state) {
                    mood_set_sentiment_override(chat_mood_state, detected, 5.0f);
                }
            }

            ChatMessage *msg = &cs->messages[cs->msg_count++];
            int text_len = (int)strlen(response_text);
            if (text_len >= CHAT_MAX_MSG_LEN) text_len = CHAT_MAX_MSG_LEN - 1;
            memcpy(msg->text, response_text, text_len);
            msg->text[text_len] = '\0';
            msg->is_user = 0;
            msg->is_thinking = 0;
        }
        cs->generating = 0;
    }
```

**Step 5: Wire in main.c**

In `src/main.c`, after `g_chat = chat_create();` add:

```c
chat_set_mood_state(&mood);
```

Note: This line goes after `mood_init(&mood);` — reorder so mood_init comes before chat_create if needed. Currently mood_init is after chat_create (line 143 vs 125). Move `mood_init` and `g_mood = &mood;` to before `g_chat = chat_create();`.

**Step 6: Build and commit**

Run: `cmake --build build`

```bash
git add src/chat.h src/chat.c src/main.c
git commit -m "feat: integrate sentiment parsing into chat flow"
```

---

### Task 7: Create stats module

**Files:**
- Create: `src/stats.h`
- Create: `src/stats.c`

**Step 1: Create stats.h**

```c
#ifndef STATS_H
#define STATS_H

#include "face.h"

void  stats_init(void);
void  stats_update(float dt);
void  stats_on_message(void);
void  stats_on_chat_open(float dt);
void  stats_on_mood(Emotion e);
float stats_bond(void);
float stats_joy(void);
float stats_energy(void);
void  stats_save(void);
void  stats_shutdown(void);

#endif
```

**Step 2: Create stats.c**

```c
#include "stats.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

static float s_bond   = 0.0f;
static float s_joy    = 0.5f;
static float s_energy = 1.0f;
static float s_save_timer = 0.0f;

#define SAVE_INTERVAL 60.0f

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* Simple JSON read — extract float value for key */
static float json_get_float(const char *json, const char *key, float def) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    const char *p = strstr(json, pattern);
    if (!p) return def;
    p += strlen(pattern);
    while (*p == ' ') p++;
    return (float)atof(p);
}

static long json_get_long(const char *json, const char *key, long def) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    const char *p = strstr(json, pattern);
    if (!p) return def;
    p += strlen(pattern);
    while (*p == ' ') p++;
    return atol(p);
}

static char stats_path[1024];

static void ensure_path(void) {
    if (stats_path[0]) return;
    const char *home = getenv("HOME");
    snprintf(stats_path, sizeof(stats_path), "%s/.ornstein/stats.json",
             home ? home : ".");
}

void stats_init(void) {
    ensure_path();

    FILE *fp = fopen(stats_path, "r");
    if (!fp) return;

    char buf[512];
    size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
    buf[n] = '\0';
    fclose(fp);

    s_bond   = json_get_float(buf, "bond",   0.0f);
    s_joy    = json_get_float(buf, "joy",    0.5f);
    s_energy = json_get_float(buf, "energy", 1.0f);
    long last_save = json_get_long(buf, "last_save", 0);

    /* Apply decay based on elapsed time */
    if (last_save > 0) {
        long now = (long)time(NULL);
        float hours = (float)(now - last_save) / 3600.0f;
        if (hours > 0.0f) {
            s_bond   -= 0.01f * hours;
            s_joy    -= 0.02f * hours;
            s_energy -= 0.05f * hours;
            s_bond   = clampf(s_bond,   0.0f, 1.0f);
            s_joy    = clampf(s_joy,    0.0f, 1.0f);
            s_energy = clampf(s_energy, 0.0f, 1.0f);
        }
    }

    s_save_timer = SAVE_INTERVAL;
}

void stats_update(float dt) {
    /* Regenerate energy when idle */
    s_energy += 0.003f * dt;
    s_energy = clampf(s_energy, 0.0f, 1.0f);

    /* Auto-save timer */
    s_save_timer -= dt;
    if (s_save_timer <= 0.0f) {
        s_save_timer = SAVE_INTERVAL;
        stats_save();
    }
}

void stats_on_message(void) {
    s_bond   += 0.02f;
    s_energy -= 0.01f;
    s_bond   = clampf(s_bond,   0.0f, 1.0f);
    s_energy = clampf(s_energy, 0.0f, 1.0f);
}

void stats_on_chat_open(float dt) {
    s_bond += 0.005f * dt / 60.0f;
    s_bond = clampf(s_bond, 0.0f, 1.0f);
}

void stats_on_mood(Emotion e) {
    switch (e) {
        case EMOTION_HAPPY:
        case EMOTION_EXCITED:
            s_joy += 0.05f;
            break;
        case EMOTION_SAD:
            s_joy -= 0.03f;
            break;
        default:
            break;
    }
    s_joy = clampf(s_joy, 0.0f, 1.0f);
}

float stats_bond(void)   { return s_bond; }
float stats_joy(void)    { return s_joy; }
float stats_energy(void) { return s_energy; }

void stats_save(void) {
    ensure_path();

    /* Ensure directory exists */
    char dir[1024];
    strncpy(dir, stats_path, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';
    char *slash = strrchr(dir, '/');
    if (slash) {
        *slash = '\0';
        mkdir(dir, 0755);
    }

    FILE *fp = fopen(stats_path, "w");
    if (!fp) return;
    fprintf(fp, "{\"bond\": %.4f, \"joy\": %.4f, \"energy\": %.4f, \"last_save\": %ld}\n",
            s_bond, s_joy, s_energy, (long)time(NULL));
    fclose(fp);
}

void stats_shutdown(void) {
    stats_save();
}
```

**Step 3: Add to CMakeLists.txt**

Update the `add_executable` line to include `src/stats.c`:

```cmake
add_executable(ornstein src/main.c src/face.c src/mood.c src/platform_macos.m
                        src/text.c src/llm.c src/chat.c src/sentiment.c
                        src/stats.c)
```

**Step 4: Build and commit**

Run: `cmake --build build`

```bash
git add src/stats.h src/stats.c CMakeLists.txt
git commit -m "feat: add stats module with bond/joy/energy and persistence"
```

---

### Task 8: Create ambient module (glow + bobbing + reflection)

**Files:**
- Create: `src/ambient.h`
- Create: `src/ambient.c`

**Step 1: Create ambient.h**

```c
#ifndef AMBIENT_H
#define AMBIENT_H

#include "face.h"

void  ambient_init(void);
void  ambient_update(Emotion emotion, float dt);
float ambient_bob_offset(float time, float scale);
void  ambient_render(float cx, float cy, Emotion emotion, float scale,
                     float fb_w, float fb_h, float time);
void  ambient_cleanup(void);

#endif
```

**Step 2: Create ambient.c**

This is the largest new file. It contains:
1. A radial gradient shader (for breathing glow and reflection)
2. Mood color lookup
3. Bobbing offset calculation
4. Particle system (Task 9 will add the particle rendering)

```c
#include "ambient.h"
#include "text.h"

#include <glad/gl.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define A_PI 3.14159265358979323846f
#define A_SEGMENTS 64

/* ── Mood color table ───────────────────────────────────────────── */

typedef struct { float r, g, b; } Color3;

static const Color3 mood_colors[EMOTION_COUNT] = {
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

/* Smooth current color for transitions */
static Color3 current_color = {0.40f, 0.40f, 0.45f};

static float lerpf(float a, float b, float t) {
    return a + (b - a) * t;
}

/* ── Radial glow shader ────────────────────────────────────────── */

static GLuint a_glow_vao, a_glow_vbo, a_glow_prog;
static GLint  a_u_center, a_u_radius, a_u_resolution, a_u_color, a_u_alpha;

static const char *a_glow_vert_src =
    "#version 410 core\n"
    "layout (location = 0) in vec2 aPos;\n"
    "uniform vec2 uCenter;\n"
    "uniform vec2 uRadius;\n"
    "uniform vec2 uResolution;\n"
    "out vec2 vLocalPos;\n"
    "void main() {\n"
    "    vLocalPos = aPos;\n"
    "    vec2 px = uCenter + aPos * uRadius;\n"
    "    vec2 ndc = (px / uResolution) * 2.0 - 1.0;\n"
    "    ndc.y = -ndc.y;\n"
    "    gl_Position = vec4(ndc, 0.0, 1.0);\n"
    "}\n";

static const char *a_glow_frag_src =
    "#version 410 core\n"
    "in vec2 vLocalPos;\n"
    "uniform vec3 uColor;\n"
    "uniform float uAlpha;\n"
    "out vec4 FragColor;\n"
    "void main() {\n"
    "    float dist = length(vLocalPos);\n"
    "    float a = smoothstep(1.0, 0.0, dist) * uAlpha;\n"
    "    FragColor = vec4(uColor, a);\n"
    "}\n";

static GLuint a_compile(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    int ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        fprintf(stderr, "ambient shader compile: %s\n", log);
    }
    return s;
}

static GLuint a_link(GLuint vert, GLuint frag) {
    GLuint p = glCreateProgram();
    glAttachShader(p, vert);
    glAttachShader(p, frag);
    glLinkProgram(p);
    int ok;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(p, sizeof(log), NULL, log);
        fprintf(stderr, "ambient shader link: %s\n", log);
    }
    return p;
}

/* ── Particles ──────────────────────────────────────────────────── */

#define MAX_PARTICLES 16

typedef struct {
    float x, y;
    float vx, vy;
    float life;
    float max_life;
    float phase;    /* for sine wobble */
    int   active;
} Particle;

static Particle particles[MAX_PARTICLES];
static float    spawn_timer = 0.0f;

static float randf(float lo, float hi) {
    return lo + (float)rand() / (float)RAND_MAX * (hi - lo);
}

/* ── Public API ─────────────────────────────────────────────────── */

void ambient_init(void) {
    /* Build unit circle geometry for glow */
    float verts[(A_SEGMENTS + 2) * 2];
    verts[0] = 0.0f;
    verts[1] = 0.0f;
    for (int i = 0; i <= A_SEGMENTS; i++) {
        float a = (float)i / (float)A_SEGMENTS * 2.0f * A_PI;
        verts[(i + 1) * 2 + 0] = cosf(a);
        verts[(i + 1) * 2 + 1] = sinf(a);
    }

    glGenVertexArrays(1, &a_glow_vao);
    glGenBuffers(1, &a_glow_vbo);
    glBindVertexArray(a_glow_vao);
    glBindBuffer(GL_ARRAY_BUFFER, a_glow_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    /* Compile shaders */
    GLuint vert = a_compile(GL_VERTEX_SHADER, a_glow_vert_src);
    GLuint frag = a_compile(GL_FRAGMENT_SHADER, a_glow_frag_src);
    a_glow_prog = a_link(vert, frag);
    glDeleteShader(vert);
    glDeleteShader(frag);

    a_u_center     = glGetUniformLocation(a_glow_prog, "uCenter");
    a_u_radius     = glGetUniformLocation(a_glow_prog, "uRadius");
    a_u_resolution = glGetUniformLocation(a_glow_prog, "uResolution");
    a_u_color      = glGetUniformLocation(a_glow_prog, "uColor");
    a_u_alpha      = glGetUniformLocation(a_glow_prog, "uAlpha");

    /* Init particles */
    memset(particles, 0, sizeof(particles));
    spawn_timer = 0.5f;
}

static void draw_glow(float cx, float cy, float rx, float ry,
                       float r, float g, float b, float alpha,
                       float fb_w, float fb_h) {
    glUseProgram(a_glow_prog);
    glUniform2f(a_u_center, cx, cy);
    glUniform2f(a_u_radius, rx, ry);
    glUniform2f(a_u_resolution, fb_w, fb_h);
    glUniform3f(a_u_color, r, g, b);
    glUniform1f(a_u_alpha, alpha);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindVertexArray(a_glow_vao);
    glDrawArrays(GL_TRIANGLE_FAN, 0, A_SEGMENTS + 2);
    glBindVertexArray(0);
}

void ambient_update(Emotion emotion, float dt) {
    /* Smoothly transition color */
    if (emotion >= 0 && emotion < EMOTION_COUNT) {
        const Color3 *target = &mood_colors[emotion];
        float t = 1.0f - expf(-3.0f * dt);
        current_color.r = lerpf(current_color.r, target->r, t);
        current_color.g = lerpf(current_color.g, target->g, t);
        current_color.b = lerpf(current_color.b, target->b, t);
    }

    /* Update particles */
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (!particles[i].active) continue;
        particles[i].life -= dt;
        if (particles[i].life <= 0.0f) {
            particles[i].active = 0;
            continue;
        }
        particles[i].x += particles[i].vx * dt;
        particles[i].y += particles[i].vy * dt;
        /* Sine wobble */
        particles[i].x += sinf(particles[i].phase + particles[i].life * 2.0f) * 10.0f * dt;
    }
}

float ambient_bob_offset(float time, float scale) {
    return sinf(time * 0.8f) * 4.0f * scale;
}

static void spawn_particle(float cx, float cy, float scale) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (particles[i].active) continue;
        particles[i].active = 1;
        particles[i].x = cx + randf(-30.0f, 30.0f) * scale;
        particles[i].y = cy + randf(-20.0f, 20.0f) * scale;
        particles[i].vx = randf(-8.0f, 8.0f) * scale;
        particles[i].vy = randf(-25.0f, -15.0f) * scale; /* float upward */
        particles[i].life = randf(2.0f, 4.0f);
        particles[i].max_life = particles[i].life;
        particles[i].phase = randf(0.0f, 2.0f * A_PI);
        break;
    }
}

static const char *particle_text(Emotion e) {
    switch (e) {
        case EMOTION_HAPPY:     return "<3";
        case EMOTION_SLEEPY:    return "Z";
        case EMOTION_CURIOUS:   return "?";
        case EMOTION_EXCITED:   return "*";
        case EMOTION_THINKING:  return ".";
        case EMOTION_SAD:       return ";";
        case EMOTION_SURPRISED: return "!";
        default:                return "o";
    }
}

void ambient_render(float cx, float cy, Emotion emotion, float scale,
                    float fb_w, float fb_h, float time) {
    /* Breathing glow — pulsating radial gradient behind face */
    float breath = sinf(time * 2.1f);
    float glow_r = (80.0f + breath * 15.0f) * scale;
    draw_glow(cx, cy, glow_r, glow_r,
              current_color.r, current_color.g, current_color.b,
              0.15f + breath * 0.05f,
              fb_w, fb_h);

    /* Reflection glow — wide flat ellipse below face */
    float ref_y = cy + 80.0f * scale;
    draw_glow(cx, ref_y, 100.0f * scale, 20.0f * scale,
              current_color.r, current_color.g, current_color.b,
              0.12f,
              fb_w, fb_h);

    /* Spawn particles */
    spawn_timer -= 1.0f / 60.0f; /* approximate, called once per frame */
    if (spawn_timer <= 0.0f) {
        spawn_particle(cx, cy, scale);
        spawn_timer = randf(0.5f, 1.0f);
    }

    /* Render particles */
    const char *ptxt = particle_text(emotion);
    float text_sc = 1.2f * scale;
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (!particles[i].active) continue;
        float age = particles[i].max_life - particles[i].life;
        float alpha;
        if (age < 0.3f) {
            alpha = age / 0.3f; /* fade in */
        } else if (particles[i].life < 0.5f) {
            alpha = particles[i].life / 0.5f; /* fade out */
        } else {
            alpha = 1.0f;
        }
        float pr = current_color.r * alpha;
        float pg = current_color.g * alpha;
        float pb = current_color.b * alpha;
        text_draw(ptxt, particles[i].x, particles[i].y, text_sc,
                  pr, pg, pb, fb_w, fb_h);
    }
}

void ambient_cleanup(void) {
    glDeleteVertexArrays(1, &a_glow_vao);
    glDeleteBuffers(1, &a_glow_vbo);
    glDeleteProgram(a_glow_prog);
}
```

**Step 3: Add to CMakeLists.txt**

```cmake
add_executable(ornstein src/main.c src/face.c src/mood.c src/platform_macos.m
                        src/text.c src/llm.c src/chat.c src/sentiment.c
                        src/stats.c src/ambient.c)
```

**Step 4: Build and commit**

Run: `cmake --build build`

```bash
git add src/ambient.h src/ambient.c CMakeLists.txt
git commit -m "feat: add ambient module with breathing glow, particles, and reflection"
```

---

### Task 9: Wire ambient and stats into main.c

**Files:**
- Modify: `src/main.c`

**Step 1: Add includes**

After the existing includes at the top of `src/main.c`:

```c
#include "ambient.h"
#include "stats.h"
```

**Step 2: Reorder initialization and add new module inits**

Replace the initialization section (after `platform_style_window(window)` through the mood_init) with this order:

After `text_init();` add:
```c
ambient_init();
stats_init();
```

Move `MoodState mood; mood_init(&mood); g_mood = &mood;` to **before** `g_chat = chat_create();`.

After `g_chat = chat_create();` add:
```c
chat_set_mood_state(&mood);
```

**Step 3: Update the main loop**

In the main loop, replace the face-center and update section with:

```c
/* Face always centered on full screen, with bobbing */
float cx = (float)fb_w * 0.5f;
float cy = (float)fb_h * 0.5f + ambient_bob_offset((float)now, scale);

/* Update mood → face → ambient → chat → stats → LLM */
Emotion target = mood_update(&mood, mouse_x, mouse_y, dt);
face.target_emotion = target;
FaceParams params = face_update(&face, dt);
ambient_update(target, dt);
if (g_chat) chat_update(g_chat, target, dt);
stats_update(dt);
if (g_chat && chat_visible(g_chat)) {
    stats_on_chat_open(dt);
}
llm_poll();

/* Render ambient (behind face) */
ambient_render(cx, cy, target, scale, (float)fb_w, (float)fb_h, (float)now);

/* Render face */
face_render(&params, cx, cy, mx, my, scale, (float)fb_w, (float)fb_h);
```

**Step 4: Add stats bar rendering**

After the chat render block, before the download/startup status block, add:

```c
/* Stats bar (only when chat is open) */
if (g_chat && chat_visible(g_chat)) {
    float bar_w = 80.0f * scale;
    float bar_h = 6.0f * scale;
    float bar_y = 12.0f * scale;
    float label_sc = 1.0f * scale;
    float lh = text_line_height(label_sc);
    float total_w = bar_w * 3 + 20.0f * scale * 2;
    float start_x = ((float)fb_w - total_w) * 0.5f;

    struct {
        const char *label; float value;
        float r, g, b;
    } bars[] = {
        {"Bond",   stats_bond(),   0.9f, 0.4f, 0.5f},
        {"Joy",    stats_joy(),    0.9f, 0.8f, 0.3f},
        {"Energy", stats_energy(), 0.3f, 0.8f, 0.5f},
    };

    for (int i = 0; i < 3; i++) {
        float bx = start_x + (float)i * (bar_w + 20.0f * scale);
        /* Label */
        text_draw(bars[i].label, bx, bar_y, label_sc,
                  0.5f, 0.5f, 0.55f, (float)fb_w, (float)fb_h);
        /* Background */
        float by = bar_y + lh + 4.0f * scale;
        text_draw_rect(bx, by, bar_w, bar_h,
                       0.2f, 0.2f, 0.25f, 1.0f,
                       (float)fb_w, (float)fb_h);
        /* Fill */
        if (bars[i].value > 0.0f)
            text_draw_rect(bx, by, bar_w * bars[i].value, bar_h,
                           bars[i].r, bars[i].g, bars[i].b, 1.0f,
                           (float)fb_w, (float)fb_h);
    }
}
```

**Step 5: Add shutdown calls**

Before `chat_destroy(g_chat);`:

```c
stats_shutdown();
ambient_cleanup();
```

**Step 6: Build, test visually, and commit**

Run: `cmake --build build && ./build/ornstein`
Expected: See breathing glow behind face, gentle bobbing, particles floating up, stats bars when Tab opens chat.

```bash
git add src/main.c
git commit -m "feat: wire ambient, stats, and bobbing into main loop"
```

---

### Task 10: Chat bubbles with directional alignment

**Files:**
- Modify: `src/chat.c` (chat_render function, ChatMessage struct)

**Step 1: Add age field to ChatMessage**

In `src/chat.c`, add to the `ChatMessage` struct:

```c
typedef struct {
    char text[CHAT_MAX_MSG_LEN];
    int is_user;
    int is_thinking;
    float age;      /* seconds since message appeared */
} ChatMessage;
```

**Step 2: Initialize age in all message creation sites**

In each place where a new message is added (`msg->is_user = ...`), add:
```c
msg->age = 0.0f;
```

There are 5 message creation sites in chat.c:
1. `chat_toggle` help message (~line 256)
2. `chat_on_key` LLM not available message (~line 303)
3. `chat_on_key` user message (~line 315)
4. `on_token` thinking message (~line 202)
5. `on_token` response message (~line 214)

**Step 3: Increment ages in chat_update**

In `chat_update()`, after `cs->cursor_blink += dt;`:

```c
for (int i = 0; i < cs->msg_count; i++) {
    cs->messages[i].age += dt;
}
```

**Step 4: Rewrite chat_render for bubble layout**

Replace the historical messages rendering section (from "/* Historical messages (newest at bottom) */" to the glDisable(GL_SCISSOR_TEST)) with the bubble-based rendering. Key changes:

- Each message gets a background rect
- User messages right-aligned, pet messages left-aligned
- Bubble padding: 8px horizontal, 6px vertical
- Max bubble width: 70% of chat area
- Entry animation: slide + fade for first 0.3s

```c
/* Historical messages (newest at bottom) */
float bubble_pad_x = 8.0f * scale;
float bubble_pad_y = 6.0f * scale;
float max_bubble_w = msgs_w * 0.7f;

for (int i = cs->msg_count - 1; i >= 0; i--) {
    ChatMessage *msg = &cs->messages[i];
    float content_w = max_bubble_w - bubble_pad_x * 2;
    float h = text_draw_wrapped(msg->text,
                                msgs_x, fb_h * 2, content_w, text_scale,
                                0, 0, 0, fb_w, fb_h);
    float bubble_h = h + bubble_pad_y * 2;
    y -= bubble_h;

    if (y < msgs_bottom && y + bubble_h > msgs_top) {
        /* Entry animation */
        float anim_t = 1.0f;
        float anim_x_off = 0.0f;
        float anim_alpha = 1.0f;
        if (msg->age < 0.3f) {
            float t = msg->age / 0.3f;
            float ease = 1.0f - (1.0f - t) * (1.0f - t); /* ease out quad */
            anim_t = ease;
            anim_x_off = (1.0f - ease) * 30.0f * scale;
            anim_alpha = t < 0.2f / 0.3f ? t / (0.2f / 0.3f) : 1.0f;
        }

        float bubble_x;
        if (msg->is_user) {
            bubble_x = msgs_x + msgs_w - max_bubble_w + anim_x_off;
        } else {
            bubble_x = msgs_x - anim_x_off;
        }

        /* Bubble background */
        float bg_r, bg_g, bg_b, bg_a;
        if (msg->is_user) {
            bg_r = 0.20f; bg_g = 0.35f; bg_b = 0.60f; bg_a = 0.85f * anim_alpha;
        } else if (msg->is_thinking) {
            bg_r = 0.15f; bg_g = 0.15f; bg_b = 0.17f; bg_a = 0.70f * anim_alpha;
        } else {
            bg_r = 0.18f; bg_g = 0.18f; bg_b = 0.20f; bg_a = 0.85f * anim_alpha;
        }
        c_draw_rect(bubble_x, y, max_bubble_w, bubble_h,
                    bg_r, bg_g, bg_b, bg_a,
                    fb_w, fb_h);

        /* Text */
        float r, g, b;
        if (msg->is_user) {
            r = 0.9f; g = 0.9f; b = 1.0f;
        } else if (msg->is_thinking) {
            r = 0.45f; g = 0.45f; b = 0.5f;
        } else {
            r = 0.9f; g = 0.9f; b = 0.85f;
        }
        text_draw_wrapped(msg->text,
                          bubble_x + bubble_pad_x, y + bubble_pad_y,
                          content_w, text_scale,
                          r * anim_alpha, g * anim_alpha, b * anim_alpha,
                          fb_w, fb_h);
    }

    y -= msg_spacing;
    if (y + cs->scroll_offset < msgs_top - fb_h) break;
}
```

Also update the pending streaming content section similarly — wrap in bubbles at left alignment.

**Step 5: Build, test visually, and commit**

Run: `cmake --build build && ./build/ornstein`
Expected: Messages appear in colored bubbles, user right-aligned, pet left-aligned, with slide-in animation.

```bash
git add src/chat.c
git commit -m "feat: add directional chat bubbles with entry animations"
```

---

### Task 11: Typing indicator with pulsing dots

**Files:**
- Modify: `src/chat.c` (chat_render, replace "..." with animated dots)

**Step 1: Replace the static "..." with pulsing dots**

In `chat_render()`, replace the `else if (cs->generating)` block that draws "...":

```c
} else if (cs->generating) {
    /* Pulsing dots typing indicator */
    float dot_r = 3.0f * scale;
    float dot_gap = 12.0f * scale;
    float dot_base_y = y - line_h * 0.5f;
    float dot_base_x = msgs_x + 16.0f * scale;
    float time = cs->cursor_blink * 6.28f; /* reuse cursor_blink as timer */
    for (int d = 0; d < 3; d++) {
        float dot_scale = 0.5f + 0.5f * sinf(time * 4.0f + (float)d * 1.0f);
        float dr = dot_r * dot_scale;
        float dx = dot_base_x + (float)d * dot_gap;
        /* Draw as a small rect (square approximation of circle) */
        c_draw_rect(dx - dr, dot_base_y - dr, dr * 2, dr * 2,
                    0.5f, 0.5f, 0.55f, 0.8f,
                    fb_w, fb_h);
    }
    y -= line_h + msg_spacing;
}
```

Note: `cursor_blink` wraps at 1.0 which gives us a continuous timer. Multiply by 2*pi for the sine wave input. Actually, `cursor_blink` resets at 1.0 causing a discontinuity. Add a separate `float anim_time;` field to ChatState that monotonically increases, and use that instead.

Add to ChatState struct:
```c
float anim_time;   /* monotonic timer for animations */
```

Initialize to 0 in chat_create. Increment in chat_update:
```c
cs->anim_time += dt;
```

Then use `cs->anim_time` in the pulsing dots instead of `cs->cursor_blink * 6.28f`.

**Step 2: Build and commit**

Run: `cmake --build build && ./build/ornstein`

```bash
git add src/chat.c
git commit -m "feat: add pulsing dots typing indicator"
```

---

### Task 12: Mood-reactive chat colors

**Files:**
- Modify: `src/chat.c`

**Step 1: Add mood color lookup to chat.c**

Add a helper function near the top of chat.c (after the includes):

```c
static void mood_glow_color(Emotion e, float *r, float *g, float *b) {
    static const float colors[][3] = {
        {0.40f, 0.40f, 0.45f}, /* neutral */
        {0.90f, 0.80f, 0.30f}, /* happy */
        {1.00f, 0.60f, 0.20f}, /* excited */
        {0.90f, 0.90f, 1.00f}, /* surprised */
        {0.20f, 0.30f, 0.70f}, /* sleepy */
        {0.30f, 0.30f, 0.35f}, /* bored */
        {0.20f, 0.70f, 0.70f}, /* curious */
        {0.30f, 0.40f, 0.80f}, /* sad */
        {0.50f, 0.30f, 0.80f}, /* thinking */
    };
    int idx = (e >= 0 && e < EMOTION_COUNT) ? e : 0;
    *r = colors[idx][0];
    *g = colors[idx][1];
    *b = colors[idx][2];
}
```

**Step 2: Draw mood-colored input border when focused**

In `chat_render()`, after drawing the input box background and before drawing input text, add:

```c
/* Mood-reactive focus ring */
if (cs->focused) {
    float mr, mg, mb;
    mood_glow_color(cs->current_mood, &mr, &mg, &mb);
    float border = 1.0f * scale;
    /* Top */
    c_draw_rect(input_box_x, input_box_y, input_box_w, border,
                mr, mg, mb, 0.4f, fb_w, fb_h);
    /* Bottom */
    c_draw_rect(input_box_x, input_box_y + input_box_h - border,
                input_box_w, border,
                mr, mg, mb, 0.4f, fb_w, fb_h);
    /* Left */
    c_draw_rect(input_box_x, input_box_y, border, input_box_h,
                mr, mg, mb, 0.4f, fb_w, fb_h);
    /* Right */
    c_draw_rect(input_box_x + input_box_w - border, input_box_y,
                border, input_box_h,
                mr, mg, mb, 0.4f, fb_w, fb_h);
}
```

**Step 3: Build and commit**

Run: `cmake --build build && ./build/ornstein`
Expected: Input box shows a colored border matching current mood when focused.

```bash
git add src/chat.c
git commit -m "feat: add mood-reactive input border color"
```

---

### Task 13: Wire stats updates from chat

**Files:**
- Modify: `src/chat.c`

**Step 1: Add stats include**

At the top of `src/chat.c`:

```c
#include "stats.h"
```

**Step 2: Call stats_on_message when user sends a message**

In `chat_on_key`, after the user message is added to the array (after `msg->is_user = 1;` and `msg->age = 0.0f;`):

```c
stats_on_message();
```

**Step 3: Call stats_on_mood when sentiment is detected**

In the `on_token` done block, after the `mood_set_sentiment_override` call:

```c
stats_on_mood(detected);
```

**Step 4: Build and commit**

Run: `cmake --build build`

```bash
git add src/chat.c
git commit -m "feat: wire stats updates from chat interactions"
```

---

### Task 14: Final build and visual test

**Files:** None new — this is a verification task.

**Step 1: Full rebuild**

```bash
cd build && cmake .. && cmake --build . --clean-first
```

**Step 2: Run and verify each feature**

Run: `./build/ornstein`

Verify:
1. Face shows gentle bobbing animation
2. Breathing glow visible behind face, color matches mood
3. Reflection glow visible below face
4. Particles float up with mood-appropriate shapes (wait for mood changes)
5. Press Tab — chat opens, stats bars visible at top
6. Type a message, press Enter — user message in blue bubble on right
7. Pet response appears in dark bubble on left with slide-in animation
8. Pulsing dots visible while waiting for response
9. Input box shows colored border matching mood
10. Pet's face changes based on [MOOD:xxx] tag in response
11. Stats bars update after chatting
12. Close and reopen — stats persist with decay

**Step 3: Final commit if any fixes needed**

```bash
git add -A
git commit -m "fix: polish UI improvements after visual testing"
```

---

## Summary of all files

| File | Action | Task |
|------|--------|------|
| `src/face.h` | Modify | 1, 2 |
| `src/face.c` | Modify | 1, 2 |
| `src/sentiment.h` | Create | 3 |
| `src/sentiment.c` | Create | 3 |
| `src/mood.h` | Modify | 4 |
| `src/mood.c` | Modify | 4 |
| `src/llm.c` | Modify | 5 |
| `src/chat.h` | Modify | 6 |
| `src/chat.c` | Modify | 1, 6, 10, 11, 12, 13 |
| `src/stats.h` | Create | 7 |
| `src/stats.c` | Create | 7 |
| `src/ambient.h` | Create | 8 |
| `src/ambient.c` | Create | 8 |
| `src/main.c` | Modify | 6, 9 |
| `CMakeLists.txt` | Modify | 3, 7, 8 |
