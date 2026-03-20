#include "ambient.h"
#include "text.h"

#include <glad/gl.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define A_PI 3.14159265358979323846f
#define A_SEGMENTS 64

/* -- Mood color table ---------------------------------------------------- */

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

static Color3 current_color = {0.40f, 0.40f, 0.45f};

static float lerpf(float a, float b, float t) {
    return a + (b - a) * t;
}

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

/* -- Particles ----------------------------------------------------------- */

#define MAX_PARTICLES 16

typedef struct {
    float x, y;
    float vx, vy;
    float life;
    float max_life;
    float phase;
    int   active;
} Particle;

static Particle particles[MAX_PARTICLES];
static float    spawn_timer = 0.0f;

static float randf(float lo, float hi) {
    return lo + (float)rand() / (float)RAND_MAX * (hi - lo);
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
    if (emotion >= 0 && emotion < EMOTION_COUNT) {
        const Color3 *target = &mood_colors[emotion];
        float t = 1.0f - expf(-3.0f * dt);
        current_color.r = lerpf(current_color.r, target->r, t);
        current_color.g = lerpf(current_color.g, target->g, t);
        current_color.b = lerpf(current_color.b, target->b, t);
    }

    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (!particles[i].active) continue;
        particles[i].life -= dt;
        if (particles[i].life <= 0.0f) {
            particles[i].active = 0;
            continue;
        }
        particles[i].x += particles[i].vx * dt;
        particles[i].y += particles[i].vy * dt;
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
        particles[i].vy = randf(-25.0f, -15.0f) * scale;
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
    float breath = sinf(time * 2.1f);
    float glow_r = (80.0f + breath * 15.0f) * scale;
    draw_glow(cx, cy, glow_r, glow_r,
              current_color.r, current_color.g, current_color.b,
              0.15f + breath * 0.05f,
              fb_w, fb_h);

    float ref_y = cy + 80.0f * scale;
    draw_glow(cx, ref_y, 100.0f * scale, 20.0f * scale,
              current_color.r, current_color.g, current_color.b,
              0.12f,
              fb_w, fb_h);

    spawn_timer -= 1.0f / 60.0f;
    if (spawn_timer <= 0.0f) {
        spawn_particle(cx, cy, scale);
        spawn_timer = randf(0.5f, 1.0f);
    }

    const char *ptxt = particle_text(emotion);
    float text_sc = 1.2f * scale;
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (!particles[i].active) continue;
        float age = particles[i].max_life - particles[i].life;
        float alpha;
        if (age < 0.3f) {
            alpha = age / 0.3f;
        } else if (particles[i].life < 0.5f) {
            alpha = particles[i].life / 0.5f;
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
