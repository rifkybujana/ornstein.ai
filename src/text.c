#include "text.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gl_util.h"

/* ══════════════════════════════════════════════════════════════════════
   stb_truetype — packed font atlas from Nunito.ttf with oversampling
   ══════════════════════════════════════════════════════════════════════ */

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#define BAKE_SIZE   64.0f          /* bake at high res for crisp downscaling */
#define OVERSAMPLE  2              /* 2x oversampling for smooth AA */
#define FIRST_CHAR  32
#define LAST_CHAR   126
#define NUM_CHARS   (LAST_CHAR - FIRST_CHAR + 1)
#define ATLAS_W     1024
#define ATLAS_H     1024

static stbtt_packedchar pdata[NUM_CHARS]; /* per-glyph metrics (packed) */
static unsigned char    *ttf_buffer;
static float            ascent_px;        /* ascent in bake pixels */

/* ══════════════════════════════════════════════════════════════════════
   OpenGL resources
   ══════════════════════════════════════════════════════════════════════ */

static GLuint t_font_tex;
static GLuint t_prog;
static GLuint t_vao, t_vbo;

static GLint t_u_resolution;
static GLint t_u_font_atlas;
static GLint t_u_color;
static float t_alpha = 1.0f;
static int t_font_loaded = 0;

void text_set_alpha(float a) { t_alpha = a; }

/* Rect shader resources */
static GLuint r_prog;
static GLuint r_vao, r_vbo;
static GLint  r_u_resolution;
static GLint  r_u_color;

#define MAX_CHARS 8192
#define VBO_SIZE (MAX_CHARS * 6 * 4 * sizeof(float))

static float t_vert_scratch[MAX_CHARS * 6 * 4];

/* ── Shaders ─────────────────────────────────────────────────────── */

static const char *t_vert_src =
    "#version 410 core\n"
    "layout (location = 0) in vec2 aPos;\n"
    "layout (location = 1) in vec2 aTexCoord;\n"
    "uniform vec2 uResolution;\n"
    "out vec2 vTexCoord;\n"
    "void main() {\n"
    "    vec2 ndc = (aPos / uResolution) * 2.0 - 1.0;\n"
    "    ndc.y = -ndc.y;\n"
    "    gl_Position = vec4(ndc, 0.0, 1.0);\n"
    "    vTexCoord = aTexCoord;\n"
    "}\n";

static const char *r_vert_src =
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
    "uniform vec4 uColor;\n"
    "out vec4 FragColor;\n"
    "void main() {\n"
    "    FragColor = uColor;\n"
    "}\n";

static const char *t_frag_src =
    "#version 410 core\n"
    "in vec2 vTexCoord;\n"
    "uniform sampler2D uFontAtlas;\n"
    "uniform vec4 uColor;\n"
    "out vec4 FragColor;\n"
    "void main() {\n"
    "    float a = texture(uFontAtlas, vTexCoord).r;\n"
    "    FragColor = vec4(uColor.rgb, uColor.a * a);\n"
    "}\n";

/* ── Build font atlas texture ────────────────────────────────────── */

static void t_build_atlas(void) {
    const char *paths[] = {
        "assets/Nunito.ttf",
        "../assets/Nunito.ttf",
        NULL
    };

    FILE *fp = NULL;
    for (int i = 0; paths[i]; i++) {
        fp = fopen(paths[i], "rb");
        if (fp) break;
    }
    if (!fp) {
        fprintf(stderr, "text: could not open Nunito.ttf\n");
        return;
    }

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    ttf_buffer = (unsigned char *)malloc(size);
    if (!ttf_buffer) { fclose(fp); return; }
    size_t nread = fread(ttf_buffer, 1, (size_t)size, fp);
    fclose(fp);
    if ((long)nread != size) {
        fprintf(stderr, "text: short read on font file (%zu of %ld bytes)\n", nread, size);
        free(ttf_buffer);
        ttf_buffer = NULL;
        return;
    }

    /* Get font metrics for proper vertical positioning */
    stbtt_fontinfo info;
    stbtt_InitFont(&info, ttf_buffer, 0);
    int asc, desc, gap;
    stbtt_GetFontVMetrics(&info, &asc, &desc, &gap);
    float metric_scale = stbtt_ScaleForPixelHeight(&info, BAKE_SIZE);
    ascent_px = (float)asc * metric_scale;  /* ascent in bake-pixel units */

    /* Pack with oversampling for smooth anti-aliased edges */
    unsigned char *atlas = (unsigned char *)calloc(ATLAS_W * ATLAS_H, 1);
    stbtt_pack_context pc;
    stbtt_PackBegin(&pc, atlas, ATLAS_W, ATLAS_H, 0, 1, NULL);
    stbtt_PackSetOversampling(&pc, OVERSAMPLE, OVERSAMPLE);
    stbtt_PackFontRange(&pc, ttf_buffer, 0, BAKE_SIZE,
                        FIRST_CHAR, NUM_CHARS, pdata);
    stbtt_PackEnd(&pc);

    glGenTextures(1, &t_font_tex);
    glBindTexture(GL_TEXTURE_2D, t_font_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, ATLAS_W, ATLAS_H, 0,
                 GL_RED, GL_UNSIGNED_BYTE, atlas);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    free(atlas);
    t_font_loaded = 1;
}

/* ══════════════════════════════════════════════════════════════════════
   Public API
   ══════════════════════════════════════════════════════════════════════ */

void text_init(void) {
    GLuint vert = gl_compile_shader(GL_VERTEX_SHADER, t_vert_src, "text");
    GLuint frag = gl_compile_shader(GL_FRAGMENT_SHADER, t_frag_src, "text");
    t_prog = gl_link_program(vert, frag, "text");
    glDeleteShader(vert);
    glDeleteShader(frag);

    t_u_resolution = glGetUniformLocation(t_prog, "uResolution");
    t_u_font_atlas = glGetUniformLocation(t_prog, "uFontAtlas");
    t_u_color      = glGetUniformLocation(t_prog, "uColor");

    GLuint rv = gl_compile_shader(GL_VERTEX_SHADER, r_vert_src, "text");
    GLuint rf = gl_compile_shader(GL_FRAGMENT_SHADER, r_frag_src, "text");
    r_prog = gl_link_program(rv, rf, "text");
    glDeleteShader(rv);
    glDeleteShader(rf);

    r_u_resolution = glGetUniformLocation(r_prog, "uResolution");
    r_u_color      = glGetUniformLocation(r_prog, "uColor");

    t_build_atlas();

    glGenVertexArrays(1, &t_vao);
    glGenBuffers(1, &t_vbo);
    glBindVertexArray(t_vao);
    glBindBuffer(GL_ARRAY_BUFFER, t_vbo);
    glBufferData(GL_ARRAY_BUFFER, VBO_SIZE, NULL, GL_DYNAMIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          (void *)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    glGenVertexArrays(1, &r_vao);
    glGenBuffers(1, &r_vbo);
    glBindVertexArray(r_vao);
    glBindBuffer(GL_ARRAY_BUFFER, r_vbo);
    glBufferData(GL_ARRAY_BUFFER, 6 * 2 * sizeof(float), NULL, GL_DYNAMIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
}

/* Helper: decode one UTF-8 codepoint, return bytes consumed (0 = invalid) */
static int utf8_decode(const unsigned char *s, int *cp) {
    if (s[0] < 0x80) { *cp = s[0]; return 1; }
    if ((s[0] & 0xE0) == 0xC0) {
        if ((s[1] & 0xC0) != 0x80) return 0;
        *cp = ((s[0] & 0x1F) << 6) | (s[1] & 0x3F);
        return 2;
    }
    if ((s[0] & 0xF0) == 0xE0) {
        if ((s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80) return 0;
        *cp = ((s[0] & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
        return 3;
    }
    if ((s[0] & 0xF8) == 0xF0) {
        if ((s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80 || (s[3] & 0xC0) != 0x80) return 0;
        *cp = ((s[0] & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
        return 4;
    }
    return 0;
}

/*
 * Scale mapping: callers pass `scale` expecting 8px-base sizing.
 * `sc` converts bake-space coordinates to screen pixels.
 * line_height = 8 * scale (matches old API contract).
 * Glyph quads from stbtt are in bake-pixel coords, multiplied by sc.
 * Baseline offset from top-of-line = ascent_px * sc.
 */
#define SC(scale) ((scale) * 14.0f / BAKE_SIZE)

void text_draw(const char *str, float x, float y, float scale,
               float r, float g, float b, float fb_w, float fb_h) {
    if (!str || !*str) return;
    if (!t_font_loaded) return;

    float sc = SC(scale);

    float *verts = t_vert_scratch;
    int vert_count = 0;

    float cursor_x = x;
    float cursor_y = y;
    float baseline_off = ascent_px * sc;  /* offset from line top to baseline */

    const unsigned char *p = (const unsigned char *)str;
    while (*p) {
        if (vert_count / 6 >= MAX_CHARS) break;

        int cp;
        int bytes = utf8_decode(p, &cp);
        if (bytes == 0) { p++; continue; }
        p += bytes;

        if (cp == '\n') {
            cursor_x = x;
            cursor_y += 14.0f * scale;
            continue;
        }

        if (cp < FIRST_CHAR || cp > LAST_CHAR) continue;

        stbtt_aligned_quad q;
        float bx = (cursor_x - x) / sc;
        float by = 0.0f;
        stbtt_GetPackedQuad(pdata, ATLAS_W, ATLAS_H,
                            cp - FIRST_CHAR, &bx, &by, &q, 1);

        float qx0 = x + q.x0 * sc;
        float qy0 = cursor_y + q.y0 * sc + baseline_off;
        float qx1 = x + q.x1 * sc;
        float qy1 = cursor_y + q.y1 * sc + baseline_off;

        int vi = vert_count * 4;
        verts[vi+0]=qx0; verts[vi+1]=qy0; verts[vi+2]=q.s0; verts[vi+3]=q.t0;
        vi += 4;
        verts[vi+0]=qx1; verts[vi+1]=qy0; verts[vi+2]=q.s1; verts[vi+3]=q.t0;
        vi += 4;
        verts[vi+0]=qx1; verts[vi+1]=qy1; verts[vi+2]=q.s1; verts[vi+3]=q.t1;
        vert_count += 3;

        vi = vert_count * 4;
        verts[vi+0]=qx0; verts[vi+1]=qy0; verts[vi+2]=q.s0; verts[vi+3]=q.t0;
        vi += 4;
        verts[vi+0]=qx1; verts[vi+1]=qy1; verts[vi+2]=q.s1; verts[vi+3]=q.t1;
        vi += 4;
        verts[vi+0]=qx0; verts[vi+1]=qy1; verts[vi+2]=q.s0; verts[vi+3]=q.t1;
        vert_count += 3;

        cursor_x = x + bx * sc;
    }

    if (vert_count == 0) return;

    glUseProgram(t_prog);
    glUniform2f(t_u_resolution, fb_w, fb_h);
    glUniform1i(t_u_font_atlas, 0);
    glUniform4f(t_u_color, r, g, b, t_alpha);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, t_font_tex);

    glBindVertexArray(t_vao);
    glBindBuffer(GL_ARRAY_BUFFER, t_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    (GLsizeiptr)(vert_count * 4 * sizeof(float)), verts);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_TRIANGLES, 0, vert_count);
    glDisable(GL_BLEND);

    glBindVertexArray(0);
}

float text_width(const char *str, float scale) {
    if (!str) return 0.0f;
    if (!t_font_loaded) return 0.0f;
    float sc = SC(scale);
    float max_w = 0.0f;
    float cur_w = 0.0f;

    const unsigned char *p = (const unsigned char *)str;
    while (*p) {
        int cp;
        int bytes = utf8_decode(p, &cp);
        if (bytes == 0) { p++; continue; }
        p += bytes;

        if (cp == '\n') {
            if (cur_w > max_w) max_w = cur_w;
            cur_w = 0.0f;
            continue;
        }
        if (cp >= FIRST_CHAR && cp <= LAST_CHAR) {
            cur_w += pdata[cp - FIRST_CHAR].xadvance * sc;
        }
    }
    if (cur_w > max_w) max_w = cur_w;
    return max_w;
}

float text_line_height(float scale) {
    return 14.0f * scale;
}

float text_draw_wrapped(const char *str, float x, float y,
                        float max_width, float scale,
                        float r, float g, float b,
                        float fb_w, float fb_h) {
    if (!str || !*str) return 0.0f;
    if (!t_font_loaded) return 0.0f;

    float sc = SC(scale);
    float line_h = 14.0f * scale;
    float baseline_off = ascent_px * sc;

    float *verts = t_vert_scratch;
    int vert_count = 0;
    int char_count = 0;

    float cursor_x = x;
    float cursor_y = y;
    float start_y = y;

    const unsigned char *p = (const unsigned char *)str;
    while (*p && char_count < MAX_CHARS) {
        if (*p == '\n') {
            cursor_x = x;
            cursor_y += line_h;
            p++;
            continue;
        }

        /* Find the next word boundary and measure its width */
        float word_width = 0.0f;
        const unsigned char *wp = p;
        while (*wp && *wp != ' ' && *wp != '\n') {
            int wcp;
            int wb = utf8_decode(wp, &wcp);
            if (wb == 0) { wp++; continue; }
            wp += wb;
            if (wcp >= FIRST_CHAR && wcp <= LAST_CHAR)
                word_width += pdata[wcp - FIRST_CHAR].xadvance * sc;
        }
        const unsigned char *word_end = wp;

        /* Wrap if word doesn't fit and we're not at line start */
        if (cursor_x > x && (cursor_x - x + word_width) > max_width) {
            cursor_x = x;
            cursor_y += line_h;
        }

        /* Emit quads for the word */
        float bx = (cursor_x - x) / sc;

        while (p < word_end && char_count < MAX_CHARS) {
            int cp;
            int bytes = utf8_decode(p, &cp);
            if (bytes == 0) { p++; continue; }
            p += bytes;

            /* Force line break if single char exceeds width */
            if (cp >= FIRST_CHAR && cp <= LAST_CHAR) {
                float cw = pdata[cp - FIRST_CHAR].xadvance * sc;
                if (cursor_x > x && (cursor_x - x + cw) > max_width) {
                    cursor_x = x;
                    cursor_y += line_h;
                    bx = 0.0f;
                }
            }

            if (cp < FIRST_CHAR || cp > LAST_CHAR) continue;

            stbtt_aligned_quad q;
            float by = 0.0f;
            stbtt_GetPackedQuad(pdata, ATLAS_W, ATLAS_H,
                                cp - FIRST_CHAR, &bx, &by, &q, 1);

            float qx0 = x + q.x0 * sc;
            float qy0 = cursor_y + q.y0 * sc + baseline_off;
            float qx1 = x + q.x1 * sc;
            float qy1 = cursor_y + q.y1 * sc + baseline_off;

            int vi = vert_count * 4;
            verts[vi+0]=qx0; verts[vi+1]=qy0; verts[vi+2]=q.s0; verts[vi+3]=q.t0;
            vi += 4;
            verts[vi+0]=qx1; verts[vi+1]=qy0; verts[vi+2]=q.s1; verts[vi+3]=q.t0;
            vi += 4;
            verts[vi+0]=qx1; verts[vi+1]=qy1; verts[vi+2]=q.s1; verts[vi+3]=q.t1;
            vert_count += 3;

            vi = vert_count * 4;
            verts[vi+0]=qx0; verts[vi+1]=qy0; verts[vi+2]=q.s0; verts[vi+3]=q.t0;
            vi += 4;
            verts[vi+0]=qx1; verts[vi+1]=qy1; verts[vi+2]=q.s1; verts[vi+3]=q.t1;
            vi += 4;
            verts[vi+0]=qx0; verts[vi+1]=qy1; verts[vi+2]=q.s0; verts[vi+3]=q.t1;
            vert_count += 3;

            char_count++;
            cursor_x = x + bx * sc;
        }

        /* Consume space after word */
        if (*p == ' ') {
            if (FIRST_CHAR <= ' ' && ' ' <= LAST_CHAR)
                cursor_x += pdata[' ' - FIRST_CHAR].xadvance * sc;
            bx = (cursor_x - x) / sc;
            p++;
        }
    }

    if (vert_count == 0) return line_h;

    glUseProgram(t_prog);
    glUniform2f(t_u_resolution, fb_w, fb_h);
    glUniform1i(t_u_font_atlas, 0);
    glUniform4f(t_u_color, r, g, b, t_alpha);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, t_font_tex);

    glBindVertexArray(t_vao);
    glBindBuffer(GL_ARRAY_BUFFER, t_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    (GLsizeiptr)(vert_count * 4 * sizeof(float)), verts);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_TRIANGLES, 0, vert_count);
    glDisable(GL_BLEND);

    glBindVertexArray(0);

    return (cursor_y - start_y) + line_h;
}

void text_draw_rect(float x, float y, float w, float h,
                    float r, float g, float b, float a,
                    float fb_w, float fb_h) {
    float verts[12] = {
        x,     y,
        x + w, y,
        x + w, y + h,
        x,     y,
        x + w, y + h,
        x,     y + h,
    };

    glUseProgram(r_prog);
    glUniform2f(r_u_resolution, fb_w, fb_h);
    glUniform4f(r_u_color, r, g, b, a);

    glBindVertexArray(r_vao);
    glBindBuffer(GL_ARRAY_BUFFER, r_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisable(GL_BLEND);

    glBindVertexArray(0);
}

void text_cleanup(void) {
    glDeleteTextures(1, &t_font_tex);
    glDeleteProgram(t_prog);
    glDeleteVertexArrays(1, &t_vao);
    glDeleteBuffers(1, &t_vbo);
    glDeleteProgram(r_prog);
    glDeleteVertexArrays(1, &r_vao);
    glDeleteBuffers(1, &r_vbo);
    free(ttf_buffer);
    ttf_buffer = NULL;
}
