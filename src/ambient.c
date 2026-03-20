#include "ambient.h"

#include <glad/gl.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#include "util.h"
#include "gl_util.h"
#include "mood_colors.h"

#define A_PI 3.14159265358979323846f
#define A_SEGMENTS 64

/* -- Mood color table ---------------------------------------------------- */

static MoodColor current_color = {0.40f, 0.40f, 0.45f};

/* -- Radial glow shader -------------------------------------------------- */

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

/* -- Particles ----------------------------------------------------------- */

#define MAX_PARTICLES 32

typedef enum {
    PT_DOT,
    PT_CRACKLE,
    PT_ORBITER
} ParticleType;

typedef struct {
    float x, y;
    float vx, vy;
    float life;
    float max_life;
    float phase;
    int   active;
    float r, g, b;
    float radius;
    ParticleType type;
    /* orbiter fields */
    float orbit_cx, orbit_cy;
    float orbit_radius;
    /* crackle fields */
    float x2, y2;
    /* teardrop y-radius multiplier */
    float ry_mult;
} Particle;

static Particle particles[MAX_PARTICLES];
static float    spawn_timer = 0.0f;
static Emotion  prev_emotion = EMOTION_NEUTRAL;
static float    last_cx, last_cy, last_scale;

/* Per-emotion spawn interval ranges */
static const float spawn_interval_lo[EMOTION_COUNT] = {
    [EMOTION_NEUTRAL]   = 1.0f,
    [EMOTION_HAPPY]     = 0.4f,
    [EMOTION_EXCITED]   = 0.15f,
    [EMOTION_SURPRISED] = 0.3f,
    [EMOTION_SLEEPY]    = 0.8f,
    [EMOTION_BORED]     = 2.0f,
    [EMOTION_CURIOUS]   = 0.5f,
    [EMOTION_SAD]       = 0.6f,
    [EMOTION_THINKING]  = 0.5f,
};

static const float spawn_interval_hi[EMOTION_COUNT] = {
    [EMOTION_NEUTRAL]   = 1.8f,
    [EMOTION_HAPPY]     = 0.8f,
    [EMOTION_EXCITED]   = 0.3f,
    [EMOTION_SURPRISED] = 0.6f,
    [EMOTION_SLEEPY]    = 1.5f,
    [EMOTION_BORED]     = 3.5f,
    [EMOTION_CURIOUS]   = 1.0f,
    [EMOTION_SAD]       = 1.2f,
    [EMOTION_THINKING]  = 1.0f,
};

static void spawn_particle_for_emotion(Emotion e, float cx, float cy, float scale) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (particles[i].active) continue;
        Particle *p = &particles[i];
        memset(p, 0, sizeof(*p));
        p->active = 1;
        p->phase = randf(0.0f, 2.0f * A_PI);
        p->ry_mult = 1.0f;

        switch (e) {
        case EMOTION_HAPPY:
            p->type = PT_DOT;
            p->r = 0.98f; p->g = 0.82f; p->b = 0.25f;
            p->radius = randf(4.0f, 8.0f) * scale;
            p->x = cx + randf(-30.0f, 30.0f) * scale;
            p->y = cy + randf(-20.0f, 20.0f) * scale;
            p->vx = randf(-6.0f, 6.0f) * scale;
            p->vy = randf(-20.0f, -10.0f) * scale;
            p->life = randf(2.0f, 3.5f);
            break;
        case EMOTION_EXCITED:
            p->type = PT_CRACKLE;
            p->r = 1.0f; p->g = 0.5f; p->b = 0.1f;
            p->radius = randf(3.0f, 6.0f) * scale;
            p->x = cx + randf(-40.0f, 40.0f) * scale;
            p->y = cy + randf(-30.0f, 30.0f) * scale;
            p->x2 = p->x + randf(-20.0f, 20.0f) * scale;
            p->y2 = p->y + randf(-20.0f, 20.0f) * scale;
            p->vx = randf(-30.0f, 30.0f) * scale;
            p->vy = randf(-30.0f, 30.0f) * scale;
            p->life = randf(0.2f, 0.5f);
            break;
        case EMOTION_SLEEPY:
            p->type = PT_DOT;
            p->r = 0.3f; p->g = 0.35f; p->b = 0.7f;
            p->radius = randf(8.0f, 14.0f) * scale;
            p->x = cx + randf(-20.0f, 20.0f) * scale;
            p->y = cy + randf(-10.0f, 10.0f) * scale;
            p->vx = randf(-3.0f, 3.0f) * scale;
            p->vy = randf(-12.0f, -6.0f) * scale;
            p->life = randf(3.0f, 5.0f);
            break;
        case EMOTION_SAD:
            p->type = PT_DOT;
            p->r = 0.2f; p->g = 0.3f; p->b = 0.85f;
            p->radius = randf(4.0f, 7.0f) * scale;
            p->ry_mult = 1.8f;
            p->x = cx + randf(-25.0f, 25.0f) * scale;
            p->y = cy + randf(-10.0f, 10.0f) * scale;
            p->vx = randf(-4.0f, 4.0f) * scale;
            p->vy = randf(10.0f, 20.0f) * scale; /* drift DOWN */
            p->life = randf(2.0f, 3.5f);
            break;
        case EMOTION_CURIOUS:
            p->type = PT_ORBITER;
            p->r = 0.15f; p->g = 0.8f; p->b = 0.75f;
            p->radius = randf(3.0f, 6.0f) * scale;
            p->orbit_cx = cx;
            p->orbit_cy = cy;
            p->orbit_radius = randf(60.0f, 90.0f) * scale;
            p->life = randf(3.0f, 5.0f);
            break;
        case EMOTION_THINKING:
            p->type = PT_ORBITER;
            p->r = 0.6f; p->g = 0.2f; p->b = 0.9f;
            p->radius = randf(3.0f, 5.0f) * scale;
            p->orbit_cx = cx;
            p->orbit_cy = cy;
            p->orbit_radius = randf(70.0f, 100.0f) * scale;
            p->life = randf(4.0f, 6.0f);
            break;
        case EMOTION_SURPRISED:
            p->type = PT_DOT;
            p->r = 1.0f; p->g = 1.0f; p->b = 1.0f;
            p->radius = randf(3.0f, 6.0f) * scale;
            { /* radial burst outward */
                float angle = randf(0.0f, 2.0f * A_PI);
                float speed = randf(40.0f, 80.0f) * scale;
                p->x = cx;
                p->y = cy;
                p->vx = cosf(angle) * speed;
                p->vy = sinf(angle) * speed;
            }
            p->life = randf(0.3f, 0.7f);
            break;
        case EMOTION_BORED:
            p->type = PT_DOT;
            p->r = 0.35f; p->g = 0.35f; p->b = 0.4f;
            p->radius = randf(2.0f, 4.0f) * scale;
            p->x = cx + randf(-30.0f, 30.0f) * scale;
            p->y = cy + randf(-20.0f, 20.0f) * scale;
            p->vx = randf(-3.0f, 3.0f) * scale;
            p->vy = randf(-8.0f, -3.0f) * scale;
            p->life = randf(2.0f, 4.0f);
            break;
        default: /* NEUTRAL */
            p->type = PT_DOT;
            p->r = 0.5f; p->g = 0.5f; p->b = 0.58f;
            p->radius = randf(3.0f, 5.0f) * scale;
            p->x = cx + randf(-25.0f, 25.0f) * scale;
            p->y = cy + randf(-15.0f, 15.0f) * scale;
            p->vx = randf(-5.0f, 5.0f) * scale;
            p->vy = randf(-15.0f, -8.0f) * scale;
            p->life = randf(2.0f, 3.0f);
            break;
        }
        p->max_life = p->life;
        break;
    }
}

static void spawn_burst(Emotion e, float cx, float cy, float scale, int count) {
    for (int n = 0; n < count; n++)
        spawn_particle_for_emotion(e, cx, cy, scale);
}

/* -- Time-of-day --------------------------------------------------------- */

typedef struct {
    float tint_r, tint_g, tint_b;
    float intensity;
    float spawn_mult;
} TodParams;

static TodParams tod_current = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
static float     tod_poll_timer = 0.0f;

static void update_time_of_day(void) {
    time_t now = time(NULL);
    struct tm *lt = localtime(&now);
    float hour = (float)lt->tm_hour + (float)lt->tm_min / 60.0f;

    /* Find which period we're in and blend at boundaries (1-hour cosine ramp) */
    float r = 0.55f, g = 0.60f, b = 1.0f; /* night default */
    float intensity = 0.65f, spawn = 0.4f;

    if (hour < 6.0f) {
        /* Night */
        r = 0.55f; g = 0.60f; b = 1.0f;
        intensity = 0.65f; spawn = 0.4f;
    } else if (hour < 7.0f) {
        /* Night -> Morning blend */
        float t = (hour - 6.0f);
        t = 0.5f - 0.5f * cosf(t * A_PI);
        r = lerpf(0.55f, 1.0f, t);  g = lerpf(0.60f, 0.82f, t); b = lerpf(1.0f, 0.55f, t);
        intensity = lerpf(0.65f, 1.15f, t); spawn = lerpf(0.4f, 1.0f, t);
    } else if (hour < 10.0f) {
        r = 1.0f; g = 0.82f; b = 0.55f;
        intensity = 1.15f; spawn = 1.0f;
    } else if (hour < 11.0f) {
        /* Morning -> Midday blend */
        float t = (hour - 10.0f);
        t = 0.5f - 0.5f * cosf(t * A_PI);
        r = lerpf(1.0f, 1.0f, t); g = lerpf(0.82f, 1.0f, t); b = lerpf(0.55f, 1.0f, t);
        intensity = lerpf(1.15f, 1.0f, t); spawn = lerpf(1.0f, 0.9f, t);
    } else if (hour < 18.0f) {
        r = 1.0f; g = 1.0f; b = 1.0f;
        intensity = 1.0f; spawn = 0.9f;
    } else if (hour < 19.0f) {
        /* Midday -> Sunset blend */
        float t = (hour - 18.0f);
        t = 0.5f - 0.5f * cosf(t * A_PI);
        r = lerpf(1.0f, 1.0f, t); g = lerpf(1.0f, 0.58f, t); b = lerpf(1.0f, 0.25f, t);
        intensity = lerpf(1.0f, 1.05f, t); spawn = lerpf(0.9f, 0.75f, t);
    } else if (hour < 20.0f) {
        r = 1.0f; g = 0.58f; b = 0.25f;
        intensity = 1.05f; spawn = 0.75f;
    } else if (hour < 21.0f) {
        /* Sunset -> Night blend */
        float t = (hour - 20.0f);
        t = 0.5f - 0.5f * cosf(t * A_PI);
        r = lerpf(1.0f, 0.55f, t); g = lerpf(0.58f, 0.60f, t); b = lerpf(0.25f, 1.0f, t);
        intensity = lerpf(1.05f, 0.65f, t); spawn = lerpf(0.75f, 0.4f, t);
    } else {
        r = 0.55f; g = 0.60f; b = 1.0f;
        intensity = 0.65f; spawn = 0.4f;
    }

    tod_current.tint_r = r;
    tod_current.tint_g = g;
    tod_current.tint_b = b;
    tod_current.intensity = intensity;
    tod_current.spawn_mult = spawn;
}

void ambient_get_tod_bg_tint(float *r, float *g, float *b, float *intensity) {
    *r = tod_current.tint_r;
    *g = tod_current.tint_g;
    *b = tod_current.tint_b;
    *intensity = tod_current.intensity;
}

/* -- Public API ---------------------------------------------------------- */

void ambient_init(void) {
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

    GLuint vert = gl_compile_shader(GL_VERTEX_SHADER, a_glow_vert_src, "ambient");
    GLuint frag = gl_compile_shader(GL_FRAGMENT_SHADER, a_glow_frag_src, "ambient");
    a_glow_prog = gl_link_program(vert, frag, "ambient");
    glDeleteShader(vert);
    glDeleteShader(frag);

    a_u_center     = glGetUniformLocation(a_glow_prog, "uCenter");
    a_u_radius     = glGetUniformLocation(a_glow_prog, "uRadius");
    a_u_resolution = glGetUniformLocation(a_glow_prog, "uResolution");
    a_u_color      = glGetUniformLocation(a_glow_prog, "uColor");
    a_u_alpha      = glGetUniformLocation(a_glow_prog, "uAlpha");

    memset(particles, 0, sizeof(particles));
    spawn_timer = 0.5f;
    update_time_of_day();
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
    if ((int)emotion >= 0 && (int)emotion < EMOTION_COUNT) {
        const MoodColor *target = &mood_colors[emotion];
        float t = 1.0f - expf(-3.0f * dt);
        current_color.r = lerpf(current_color.r, target->r, t);
        current_color.g = lerpf(current_color.g, target->g, t);
        current_color.b = lerpf(current_color.b, target->b, t);
    }

    /* Time-of-day polling */
    tod_poll_timer -= dt;
    if (tod_poll_timer <= 0.0f) {
        update_time_of_day();
        tod_poll_timer = 60.0f;
    }

    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (!particles[i].active) continue;
        particles[i].life -= dt;
        if (particles[i].life <= 0.0f) {
            particles[i].active = 0;
            continue;
        }

        Particle *p = &particles[i];
        if (p->type == PT_ORBITER) {
            /* Orbital motion */
            float speed = (emotion == EMOTION_CURIOUS) ? 1.2f : 0.4f;
            p->phase += speed * dt;
            p->x = p->orbit_cx + cosf(p->phase) * p->orbit_radius;
            p->y = p->orbit_cy + sinf(p->phase) * p->orbit_radius;
        } else if (p->type == PT_CRACKLE) {
            /* Jittery endpoints */
            p->x += randf(-60.0f, 60.0f) * dt;
            p->y += randf(-60.0f, 60.0f) * dt;
            p->x2 += randf(-60.0f, 60.0f) * dt;
            p->y2 += randf(-60.0f, 60.0f) * dt;
        } else {
            /* Regular dot motion */
            p->x += p->vx * dt;
            p->y += p->vy * dt;
            p->x += sinf(p->phase + p->life * 2.0f) * 10.0f * dt;
        }
    }
}

float ambient_bob_offset(float time, float scale) {
    return sinf(time * 0.8f) * 4.0f * scale;
}

void ambient_render(float cx, float cy, Emotion emotion, float scale,
                    float fb_w, float fb_h, float time) {
    /* Cache for burst spawning */
    last_cx = cx;
    last_cy = cy;
    last_scale = scale;

    /* Tinted mood color */
    float tr = current_color.r * tod_current.tint_r;
    float tg = current_color.g * tod_current.tint_g;
    float tb = current_color.b * tod_current.tint_b;

    float breath = sinf(time * 2.1f);
    float glow_r = (80.0f + breath * 15.0f) * scale;
    draw_glow(cx, cy, glow_r, glow_r,
              tr, tg, tb,
              (0.15f + breath * 0.05f) * tod_current.intensity,
              fb_w, fb_h);

    float ref_y = cy + 80.0f * scale;
    draw_glow(cx, ref_y, 100.0f * scale, 20.0f * scale,
              tr, tg, tb,
              0.12f * tod_current.intensity,
              fb_w, fb_h);

    /* Surprise burst on emotion enter */
    if (emotion != prev_emotion && emotion == EMOTION_SURPRISED) {
        spawn_burst(EMOTION_SURPRISED, cx, cy, scale, 5);
    }
    prev_emotion = emotion;

    /* Spawn particles with per-emotion interval, scaled by ToD */
    spawn_timer -= 1.0f / 60.0f;
    if (spawn_timer <= 0.0f) {
        spawn_particle_for_emotion(emotion, cx, cy, scale);
        int ei = ((int)emotion >= 0 && (int)emotion < EMOTION_COUNT) ? (int)emotion : 0;
        float interval = randf(spawn_interval_lo[ei], spawn_interval_hi[ei]);
        if (tod_current.spawn_mult > 0.01f)
            interval /= tod_current.spawn_mult;
        spawn_timer = interval;
    }

    /* Render particles */
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (!particles[i].active) continue;
        Particle *p = &particles[i];
        float age = p->max_life - p->life;
        float alpha;
        if (age < 0.3f) {
            alpha = age / 0.3f;
        } else if (p->life < 0.5f) {
            alpha = p->life / 0.5f;
        } else {
            alpha = 1.0f;
        }

        /* Tint particle color by time-of-day */
        float pr = p->r * tod_current.tint_r;
        float pg = p->g * tod_current.tint_g;
        float pb = p->b * tod_current.tint_b;
        float pa = alpha * tod_current.intensity;

        if (p->type == PT_DOT) {
            /* Twinkle for happy */
            if (emotion == EMOTION_HAPPY)
                pa *= 0.6f + 0.4f * sinf(time * 5.0f + p->phase);

            draw_glow(p->x, p->y, p->radius, p->radius * p->ry_mult,
                      pr, pg, pb, pa, fb_w, fb_h);
        } else if (p->type == PT_CRACKLE) {
            /* Two endpoint dots + midpoint glow */
            draw_glow(p->x, p->y, p->radius, p->radius,
                      pr, pg, pb, pa, fb_w, fb_h);
            draw_glow(p->x2, p->y2, p->radius, p->radius,
                      pr, pg, pb, pa, fb_w, fb_h);
            float mx = (p->x + p->x2) * 0.5f;
            float my = (p->y + p->y2) * 0.5f;
            draw_glow(mx, my, p->radius * 1.5f, p->radius * 0.5f,
                      pr, pg, pb, pa * 0.7f, fb_w, fb_h);
        } else if (p->type == PT_ORBITER) {
            draw_glow(p->x, p->y, p->radius, p->radius,
                      pr, pg, pb, pa, fb_w, fb_h);
        }
    }
}

void ambient_cleanup(void) {
    glDeleteVertexArrays(1, &a_glow_vao);
    glDeleteBuffers(1, &a_glow_vbo);
    glDeleteProgram(a_glow_prog);
}
