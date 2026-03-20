#include "face.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

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
            ? elapsed / half            /* closing: 0->1 */
            : 1.0f - (elapsed - half) / half;  /* opening: 1->0 */
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
        float norm = elapsed / YAWN_DURATION; /* 0->1 */
        float intensity = sinf(norm * 3.14159f); /* 0->1->0 */
        result.mouth_openness = fmaxf(result.mouth_openness, intensity);
        result.eye_openness *= (1.0f - 0.7f * intensity);
        if (fs->yawn_phase < 0.0f) fs->yawn_phase = 0.0f;
    }

    return result;
}

/* ══════════════════════════════════════════════════════════════════════
   Rendering — OpenGL 4.1 Core Profile
   ══════════════════════════════════════════════════════════════════════ */

#define R_SEGMENTS 64
#define R_PI 3.14159265358979323846f

/* ── Ellipse geometry (unit circle triangle fan) ─────────────────── */

static GLuint r_ellipse_vao, r_ellipse_vbo;

static void r_init_ellipse(void) {
    float verts[(R_SEGMENTS + 2) * 2];
    verts[0] = 0.0f;
    verts[1] = 0.0f;
    for (int i = 0; i <= R_SEGMENTS; i++) {
        float a = (float)i / (float)R_SEGMENTS * 2.0f * R_PI;
        verts[(i + 1) * 2 + 0] = cosf(a);
        verts[(i + 1) * 2 + 1] = sinf(a);
    }
    glGenVertexArrays(1, &r_ellipse_vao);
    glGenBuffers(1, &r_ellipse_vbo);
    glBindVertexArray(r_ellipse_vao);
    glBindBuffer(GL_ARRAY_BUFFER, r_ellipse_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
}

/* ── Quad geometry (for rectangles / squint mask) ────────────────── */

static GLuint r_quad_vao, r_quad_vbo;

static void r_init_quad(void) {
    /* Unit quad: two triangles covering (0,0)-(1,1) */
    float verts[] = {
        0.0f, 0.0f,
        1.0f, 0.0f,
        1.0f, 1.0f,
        0.0f, 0.0f,
        1.0f, 1.0f,
        0.0f, 1.0f,
    };
    glGenVertexArrays(1, &r_quad_vao);
    glGenBuffers(1, &r_quad_vbo);
    glBindVertexArray(r_quad_vao);
    glBindBuffer(GL_ARRAY_BUFFER, r_quad_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
}

/* ── Mouth geometry (dynamic triangle strip) ─────────────────────── */

#define MOUTH_POINTS 20
/* Max verts: top arc strip (MOUTH_POINTS*2) + bottom arc strip (MOUTH_POINTS*2) + fill strip (MOUTH_POINTS*2) */
#define MOUTH_MAX_VERTS (MOUTH_POINTS * 6)

static GLuint r_mouth_vao, r_mouth_vbo;

static void r_init_mouth(void) {
    glGenVertexArrays(1, &r_mouth_vao);
    glGenBuffers(1, &r_mouth_vbo);
    glBindVertexArray(r_mouth_vao);
    glBindBuffer(GL_ARRAY_BUFFER, r_mouth_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 sizeof(float) * 2 * MOUTH_MAX_VERTS,
                 NULL, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
}

/* ── Shader ──────────────────────────────────────────────────────── */

static const char *r_vert_src =
    "#version 410 core\n"
    "layout (location = 0) in vec2 aPos;\n"
    "uniform vec2 uCenter;\n"
    "uniform vec2 uRadius;\n"
    "uniform vec2 uResolution;\n"
    "void main() {\n"
    "    vec2 px = uCenter + aPos * uRadius;\n"
    "    vec2 ndc = (px / uResolution) * 2.0 - 1.0;\n"
    "    ndc.y = -ndc.y;\n"
    "    gl_Position = vec4(ndc, 0.0, 1.0);\n"
    "}\n";

/* Rect shader: maps unit quad (0,0)-(1,1) to a pixel rect */
static const char *r_rect_vert_src =
    "#version 410 core\n"
    "layout (location = 0) in vec2 aPos;\n"
    "uniform vec2 uRectPos;\n"   /* top-left in pixels */
    "uniform vec2 uRectSize;\n"  /* width, height in pixels */
    "uniform vec2 uResolution;\n"
    "void main() {\n"
    "    vec2 px = uRectPos + aPos * uRectSize;\n"
    "    vec2 ndc = (px / uResolution) * 2.0 - 1.0;\n"
    "    ndc.y = -ndc.y;\n"
    "    gl_Position = vec4(ndc, 0.0, 1.0);\n"
    "}\n";

/* Mouth shader: vertices are already in pixel coords */
static const char *r_mouth_vert_src =
    "#version 410 core\n"
    "layout (location = 0) in vec2 aPos;\n"
    "uniform vec2 uResolution;\n"
    "void main() {\n"
    "    vec2 ndc = (aPos / uResolution) * 2.0 - 1.0;\n"
    "    ndc.y = -ndc.y;\n"
    "    gl_Position = vec4(ndc, 0.0, 1.0);\n"
    "}\n";

static const char *r_frag_src =
    "#version 410 core\n"
    "uniform vec3 uColor;\n"
    "out vec4 FragColor;\n"
    "void main() {\n"
    "    FragColor = vec4(uColor, 1.0);\n"
    "}\n";

static GLuint r_prog_ellipse;
static GLint  r_u_center, r_u_radius, r_u_resolution, r_u_color;

static GLuint r_prog_rect;
static GLint  r_u_rect_pos, r_u_rect_size, r_u_rect_resolution, r_u_rect_color;

static GLuint r_prog_mouth;
static GLint  r_u_mouth_resolution, r_u_mouth_color;

static GLuint r_compile(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    int ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        fprintf(stderr, "face shader compile: %s\n", log);
    }
    return s;
}

static GLuint r_link(GLuint vert, GLuint frag) {
    GLuint p = glCreateProgram();
    glAttachShader(p, vert);
    glAttachShader(p, frag);
    glLinkProgram(p);
    int ok;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(p, sizeof(log), NULL, log);
        fprintf(stderr, "face shader link: %s\n", log);
    }
    return p;
}

static void r_init_shaders(void) {
    GLuint frag = r_compile(GL_FRAGMENT_SHADER, r_frag_src);

    /* Ellipse program */
    {
        GLuint vert = r_compile(GL_VERTEX_SHADER, r_vert_src);
        r_prog_ellipse = r_link(vert, frag);
        glDeleteShader(vert);
        r_u_center     = glGetUniformLocation(r_prog_ellipse, "uCenter");
        r_u_radius     = glGetUniformLocation(r_prog_ellipse, "uRadius");
        r_u_resolution = glGetUniformLocation(r_prog_ellipse, "uResolution");
        r_u_color      = glGetUniformLocation(r_prog_ellipse, "uColor");
    }

    /* Rect program */
    {
        GLuint vert = r_compile(GL_VERTEX_SHADER, r_rect_vert_src);
        r_prog_rect = r_link(vert, frag);
        glDeleteShader(vert);
        r_u_rect_pos        = glGetUniformLocation(r_prog_rect, "uRectPos");
        r_u_rect_size       = glGetUniformLocation(r_prog_rect, "uRectSize");
        r_u_rect_resolution = glGetUniformLocation(r_prog_rect, "uResolution");
        r_u_rect_color      = glGetUniformLocation(r_prog_rect, "uColor");
    }

    /* Mouth program */
    {
        GLuint vert = r_compile(GL_VERTEX_SHADER, r_mouth_vert_src);
        r_prog_mouth = r_link(vert, frag);
        glDeleteShader(vert);
        r_u_mouth_resolution = glGetUniformLocation(r_prog_mouth, "uResolution");
        r_u_mouth_color      = glGetUniformLocation(r_prog_mouth, "uColor");
    }

    glDeleteShader(frag);
}

/* ── Draw helpers ────────────────────────────────────────────────── */

static void r_draw_ellipse(float cx, float cy, float rx, float ry,
                           float cr, float cg, float cb,
                           float fb_w, float fb_h) {
    glUseProgram(r_prog_ellipse);
    glUniform2f(r_u_center, cx, cy);
    glUniform2f(r_u_radius, rx, ry);
    glUniform2f(r_u_resolution, fb_w, fb_h);
    glUniform3f(r_u_color, cr, cg, cb);
    glBindVertexArray(r_ellipse_vao);
    glDrawArrays(GL_TRIANGLE_FAN, 0, R_SEGMENTS + 2);
}

static void r_draw_rect(float x, float y, float w, float h,
                         float cr, float cg, float cb,
                         float fb_w, float fb_h) {
    glUseProgram(r_prog_rect);
    glUniform2f(r_u_rect_pos, x, y);
    glUniform2f(r_u_rect_size, w, h);
    glUniform2f(r_u_rect_resolution, fb_w, fb_h);
    glUniform3f(r_u_rect_color, cr, cg, cb);
    glBindVertexArray(r_quad_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

/* Quadratic bezier: P0*(1-t)^2 + P1*2*t*(1-t) + P2*t^2 */
static void bezier_point(float t,
                         float x0, float y0,
                         float x1, float y1,
                         float x2, float y2,
                         float *ox, float *oy) {
    float u = 1.0f - t;
    *ox = u * u * x0 + 2.0f * u * t * x1 + t * t * x2;
    *oy = u * u * y0 + 2.0f * u * t * y1 + t * t * y2;
}

/* Build a triangle strip with thickness around a bezier curve.
   Writes into verts[], returns number of vertices written. */
static int build_bezier_strip(float *verts,
                              float x0, float y0,
                              float x1, float y1,
                              float x2, float y2,
                              float thickness) {
    int count = 0;
    float half_t = thickness * 0.5f;

    for (int i = 0; i < MOUTH_POINTS; i++) {
        float t = (float)i / (float)(MOUTH_POINTS - 1);
        float bx, by;
        bezier_point(t, x0, y0, x1, y1, x2, y2, &bx, &by);

        /* Compute tangent for normal direction */
        float dt = 0.01f;
        float t1 = t - dt, t2 = t + dt;
        if (t1 < 0.0f) t1 = 0.0f;
        if (t2 > 1.0f) t2 = 1.0f;
        float ax, ay, cx2, cy2;
        bezier_point(t1, x0, y0, x1, y1, x2, y2, &ax, &ay);
        bezier_point(t2, x0, y0, x1, y1, x2, y2, &cx2, &cy2);
        float dx = cx2 - ax;
        float dy = cy2 - ay;
        float len = sqrtf(dx * dx + dy * dy);
        if (len < 1e-6f) len = 1.0f;
        float nx = -dy / len;
        float ny =  dx / len;

        /* Two vertices: one above, one below the curve */
        verts[count * 2 + 0] = bx + nx * half_t;
        verts[count * 2 + 1] = by + ny * half_t;
        count++;
        verts[count * 2 + 0] = bx - nx * half_t;
        verts[count * 2 + 1] = by - ny * half_t;
        count++;
    }
    return count;
}

/* Build a triangle strip filling between two bezier curves (for open mouth).
   The strip alternates between points on the top and bottom curves.
   Returns number of vertices written. */
static int build_fill_strip(float *verts,
                            float x0_t, float y0_t,
                            float x1_t, float y1_t,
                            float x2_t, float y2_t,
                            float x0_b, float y0_b,
                            float x1_b, float y1_b,
                            float x2_b, float y2_b) {
    int count = 0;
    for (int i = 0; i < MOUTH_POINTS; i++) {
        float t = (float)i / (float)(MOUTH_POINTS - 1);
        float tx, ty, bx, by;
        bezier_point(t, x0_t, y0_t, x1_t, y1_t, x2_t, y2_t, &tx, &ty);
        bezier_point(t, x0_b, y0_b, x1_b, y1_b, x2_b, y2_b, &bx, &by);
        verts[count * 2 + 0] = tx;
        verts[count * 2 + 1] = ty;
        count++;
        verts[count * 2 + 0] = bx;
        verts[count * 2 + 1] = by;
        count++;
    }
    return count;
}

/* ── Public API ──────────────────────────────────────────────────── */

void face_render_init(void) {
    r_init_shaders();
    r_init_ellipse();
    r_init_quad();
    r_init_mouth();
}

void face_render(const FaceParams *p, float cx, float cy,
                 float mouse_x, float mouse_y, float scale,
                 float fb_w, float fb_h) {

    /* ── Constants (logical pixels, multiplied by scale) ───────── */
    const float eye_r      = 45.0f;
    const float pupil_r    = 14.0f;
    const float pupil_dist = 22.0f;
    const float eye_gap    = 120.0f;

    /* Colors */
    const float bg_r = 0.11f, bg_g = 0.11f, bg_b = 0.12f;
    const float sclera_r = 0.953f, sclera_g = 0.937f, sclera_b = 0.937f;
    const float pupil_cr = 0.10f, pupil_cg = 0.10f, pupil_cb = 0.10f;

    /* Derived eye radii */
    float eye_rx = eye_r * scale;
    float eye_ry = eye_r * p->eye_openness * p->eye_scale * scale;

    /* ── Draw eyes ─────────────────────────────────────────────── */
    for (int i = 0; i < 2; i++) {
        float side = (i == 0) ? -1.0f : 1.0f;
        float ecx = cx + side * eye_gap * 0.5f * scale;
        float ecy = cy;

        /* Sclera (ellipse) */
        r_draw_ellipse(ecx, ecy, eye_rx, eye_ry,
                       sclera_r, sclera_g, sclera_b,
                       fb_w, fb_h);

        /* Squint: cover bottom portion of eye with bg-colored rect */
        if (p->eye_squint > 0.0f) {
            float squint_h = p->eye_squint * eye_ry;
            float rect_x = ecx - eye_rx;
            float rect_y = ecy + eye_ry - squint_h;
            float rect_w = eye_rx * 2.0f;
            float rect_h = squint_h;
            r_draw_rect(rect_x, rect_y, rect_w, rect_h,
                        bg_r, bg_g, bg_b,
                        fb_w, fb_h);
        }

        /* Pupil: orbit toward cursor */
        float dx = mouse_x - ecx;
        float dy = mouse_y - ecy;
        float angle = atan2f(dy, dx);
        /* Dampen orbit by eye_openness so pupil doesn't float when eyes closed */
        float openness_dampen = p->eye_openness;
        if (openness_dampen < 0.0f) openness_dampen = 0.0f;
        if (openness_dampen > 1.0f) openness_dampen = 1.0f;
        float pd = pupil_dist * scale * openness_dampen;
        float px = ecx + cosf(angle) * pd;
        float py = ecy + sinf(angle) * pd;
        float pr = pupil_r * p->pupil_scale * scale;

        r_draw_ellipse(px, py, pr, pr,
                       pupil_cr, pupil_cg, pupil_cb,
                       fb_w, fb_h);
    }

    /* ── Draw mouth (bezier curve) ─────────────────────────────── */
    {
        float mouth_y = cy + 90.0f * scale;
        float half_w  = 30.0f * p->mouth_width * scale;
        float curve_offset = p->mouth_curve * 15.0f * scale;
        float thickness = 2.5f * scale;

        /* Top arc control points */
        float lx = cx - half_w, ly = mouth_y;
        float mx = cx,          my = mouth_y - curve_offset;
        float rx = cx + half_w, ry = mouth_y;

        /* Temp buffer for mouth vertices */
        float verts[MOUTH_MAX_VERTS * 2];

        if (p->mouth_openness > 0.01f) {
            /* Open mouth: draw fill between top and bottom arcs */
            float open_offset = p->mouth_openness * 12.0f * scale;

            /* Bottom arc: pushed down */
            float b_ly = ly + open_offset;
            float b_my = my + open_offset;
            float b_ry = ry + open_offset;

            /* 1. Fill between arcs */
            int fill_count = build_fill_strip(verts,
                lx, ly, mx, my, rx, ry,           /* top arc */
                lx, b_ly, mx, b_my, rx, b_ry);    /* bottom arc */

            glUseProgram(r_prog_mouth);
            glUniform2f(r_u_mouth_resolution, fb_w, fb_h);
            glUniform3f(r_u_mouth_color, 0.15f, 0.15f, 0.15f);
            glBindVertexArray(r_mouth_vao);
            glBindBuffer(GL_ARRAY_BUFFER, r_mouth_vbo);
            glBufferSubData(GL_ARRAY_BUFFER, 0,
                            sizeof(float) * 2 * fill_count, verts);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, fill_count);

            /* 2. Top arc line */
            int top_count = build_bezier_strip(verts,
                lx, ly, mx, my, rx, ry, thickness);

            glUniform3f(r_u_mouth_color, sclera_r, sclera_g, sclera_b);
            glBufferSubData(GL_ARRAY_BUFFER, 0,
                            sizeof(float) * 2 * top_count, verts);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, top_count);

            /* 3. Bottom arc line */
            int bot_count = build_bezier_strip(verts,
                lx, b_ly, mx, b_my, rx, b_ry, thickness);

            glBufferSubData(GL_ARRAY_BUFFER, 0,
                            sizeof(float) * 2 * bot_count, verts);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, bot_count);

        } else {
            /* Closed mouth: single bezier line */
            int count = build_bezier_strip(verts,
                lx, ly, mx, my, rx, ry, thickness);

            glUseProgram(r_prog_mouth);
            glUniform2f(r_u_mouth_resolution, fb_w, fb_h);
            glUniform3f(r_u_mouth_color, sclera_r, sclera_g, sclera_b);
            glBindVertexArray(r_mouth_vao);
            glBindBuffer(GL_ARRAY_BUFFER, r_mouth_vbo);
            glBufferSubData(GL_ARRAY_BUFFER, 0,
                            sizeof(float) * 2 * count, verts);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, count);
        }
    }
}

void face_render_cleanup(void) {
    glDeleteVertexArrays(1, &r_ellipse_vao);
    glDeleteBuffers(1, &r_ellipse_vbo);
    glDeleteVertexArrays(1, &r_quad_vao);
    glDeleteBuffers(1, &r_quad_vbo);
    glDeleteVertexArrays(1, &r_mouth_vao);
    glDeleteBuffers(1, &r_mouth_vbo);
    glDeleteProgram(r_prog_ellipse);
    glDeleteProgram(r_prog_rect);
    glDeleteProgram(r_prog_mouth);
}
