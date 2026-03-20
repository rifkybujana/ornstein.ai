#include "text.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ══════════════════════════════════════════════════════════════════════
   Font data — 8x8 VGA/IBM bitmap font (public domain)
   128 characters, 8 bytes per character. LSB is leftmost pixel.
   Source: Daniel Hepper / Marcel Sondaar / IBM public domain VGA fonts.
   ══════════════════════════════════════════════════════════════════════ */

#define FONT_W 8
#define FONT_H 8
#define FONT_FIRST 0
#define FONT_LAST  127
#define FONT_CHARS (FONT_LAST - FONT_FIRST + 1)

/* Atlas layout: 16 columns x 8 rows */
#define ATLAS_COLS 16
#define ATLAS_ROWS 8
#define ATLAS_W    (ATLAS_COLS * FONT_W)  /* 128 */
#define ATLAS_H    (ATLAS_ROWS * FONT_H)  /* 64  */

static const unsigned char font8x8_data[128][8] = {
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* U+0000 (nul) */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* U+0001 */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* U+0002 */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* U+0003 */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* U+0004 */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* U+0005 */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* U+0006 */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* U+0007 */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* U+0008 */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* U+0009 */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* U+000A */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* U+000B */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* U+000C */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* U+000D */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* U+000E */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* U+000F */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* U+0010 */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* U+0011 */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* U+0012 */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* U+0013 */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* U+0014 */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* U+0015 */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* U+0016 */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* U+0017 */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* U+0018 */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* U+0019 */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* U+001A */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* U+001B */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* U+001C */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* U+001D */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* U+001E */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* U+001F */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* U+0020 (space) */
    { 0x18, 0x3C, 0x3C, 0x18, 0x18, 0x00, 0x18, 0x00 },   /* U+0021 (!) */
    { 0x36, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* U+0022 (") */
    { 0x36, 0x36, 0x7F, 0x36, 0x7F, 0x36, 0x36, 0x00 },   /* U+0023 (#) */
    { 0x0C, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x0C, 0x00 },   /* U+0024 ($) */
    { 0x00, 0x63, 0x33, 0x18, 0x0C, 0x66, 0x63, 0x00 },   /* U+0025 (%) */
    { 0x1C, 0x36, 0x1C, 0x6E, 0x3B, 0x33, 0x6E, 0x00 },   /* U+0026 (&) */
    { 0x06, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* U+0027 (') */
    { 0x18, 0x0C, 0x06, 0x06, 0x06, 0x0C, 0x18, 0x00 },   /* U+0028 (() */
    { 0x06, 0x0C, 0x18, 0x18, 0x18, 0x0C, 0x06, 0x00 },   /* U+0029 ()) */
    { 0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00 },   /* U+002A (*) */
    { 0x00, 0x0C, 0x0C, 0x3F, 0x0C, 0x0C, 0x00, 0x00 },   /* U+002B (+) */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x06 },   /* U+002C (,) */
    { 0x00, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x00 },   /* U+002D (-) */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x00 },   /* U+002E (.) */
    { 0x60, 0x30, 0x18, 0x0C, 0x06, 0x03, 0x01, 0x00 },   /* U+002F (/) */
    { 0x3E, 0x63, 0x73, 0x7B, 0x6F, 0x67, 0x3E, 0x00 },   /* U+0030 (0) */
    { 0x0C, 0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x3F, 0x00 },   /* U+0031 (1) */
    { 0x1E, 0x33, 0x30, 0x1C, 0x06, 0x33, 0x3F, 0x00 },   /* U+0032 (2) */
    { 0x1E, 0x33, 0x30, 0x1C, 0x30, 0x33, 0x1E, 0x00 },   /* U+0033 (3) */
    { 0x38, 0x3C, 0x36, 0x33, 0x7F, 0x30, 0x78, 0x00 },   /* U+0034 (4) */
    { 0x3F, 0x03, 0x1F, 0x30, 0x30, 0x33, 0x1E, 0x00 },   /* U+0035 (5) */
    { 0x1C, 0x06, 0x03, 0x1F, 0x33, 0x33, 0x1E, 0x00 },   /* U+0036 (6) */
    { 0x3F, 0x33, 0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x00 },   /* U+0037 (7) */
    { 0x1E, 0x33, 0x33, 0x1E, 0x33, 0x33, 0x1E, 0x00 },   /* U+0038 (8) */
    { 0x1E, 0x33, 0x33, 0x3E, 0x30, 0x18, 0x0E, 0x00 },   /* U+0039 (9) */
    { 0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x00 },   /* U+003A (:) */
    { 0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x06 },   /* U+003B (;) */
    { 0x18, 0x0C, 0x06, 0x03, 0x06, 0x0C, 0x18, 0x00 },   /* U+003C (<) */
    { 0x00, 0x00, 0x3F, 0x00, 0x00, 0x3F, 0x00, 0x00 },   /* U+003D (=) */
    { 0x06, 0x0C, 0x18, 0x30, 0x18, 0x0C, 0x06, 0x00 },   /* U+003E (>) */
    { 0x1E, 0x33, 0x30, 0x18, 0x0C, 0x00, 0x0C, 0x00 },   /* U+003F (?) */
    { 0x3E, 0x63, 0x7B, 0x7B, 0x7B, 0x03, 0x1E, 0x00 },   /* U+0040 (@) */
    { 0x0C, 0x1E, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x00 },   /* U+0041 (A) */
    { 0x3F, 0x66, 0x66, 0x3E, 0x66, 0x66, 0x3F, 0x00 },   /* U+0042 (B) */
    { 0x3C, 0x66, 0x03, 0x03, 0x03, 0x66, 0x3C, 0x00 },   /* U+0043 (C) */
    { 0x1F, 0x36, 0x66, 0x66, 0x66, 0x36, 0x1F, 0x00 },   /* U+0044 (D) */
    { 0x7F, 0x46, 0x16, 0x1E, 0x16, 0x46, 0x7F, 0x00 },   /* U+0045 (E) */
    { 0x7F, 0x46, 0x16, 0x1E, 0x16, 0x06, 0x0F, 0x00 },   /* U+0046 (F) */
    { 0x3C, 0x66, 0x03, 0x03, 0x73, 0x66, 0x7C, 0x00 },   /* U+0047 (G) */
    { 0x33, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x33, 0x00 },   /* U+0048 (H) */
    { 0x1E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00 },   /* U+0049 (I) */
    { 0x78, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E, 0x00 },   /* U+004A (J) */
    { 0x67, 0x66, 0x36, 0x1E, 0x36, 0x66, 0x67, 0x00 },   /* U+004B (K) */
    { 0x0F, 0x06, 0x06, 0x06, 0x46, 0x66, 0x7F, 0x00 },   /* U+004C (L) */
    { 0x63, 0x77, 0x7F, 0x7F, 0x6B, 0x63, 0x63, 0x00 },   /* U+004D (M) */
    { 0x63, 0x67, 0x6F, 0x7B, 0x73, 0x63, 0x63, 0x00 },   /* U+004E (N) */
    { 0x1C, 0x36, 0x63, 0x63, 0x63, 0x36, 0x1C, 0x00 },   /* U+004F (O) */
    { 0x3F, 0x66, 0x66, 0x3E, 0x06, 0x06, 0x0F, 0x00 },   /* U+0050 (P) */
    { 0x1E, 0x33, 0x33, 0x33, 0x3B, 0x1E, 0x38, 0x00 },   /* U+0051 (Q) */
    { 0x3F, 0x66, 0x66, 0x3E, 0x36, 0x66, 0x67, 0x00 },   /* U+0052 (R) */
    { 0x1E, 0x33, 0x07, 0x0E, 0x38, 0x33, 0x1E, 0x00 },   /* U+0053 (S) */
    { 0x3F, 0x2D, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00 },   /* U+0054 (T) */
    { 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x3F, 0x00 },   /* U+0055 (U) */
    { 0x33, 0x33, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00 },   /* U+0056 (V) */
    { 0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x00 },   /* U+0057 (W) */
    { 0x63, 0x63, 0x36, 0x1C, 0x1C, 0x36, 0x63, 0x00 },   /* U+0058 (X) */
    { 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x0C, 0x1E, 0x00 },   /* U+0059 (Y) */
    { 0x7F, 0x63, 0x31, 0x18, 0x4C, 0x66, 0x7F, 0x00 },   /* U+005A (Z) */
    { 0x1E, 0x06, 0x06, 0x06, 0x06, 0x06, 0x1E, 0x00 },   /* U+005B ([) */
    { 0x03, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x40, 0x00 },   /* U+005C (\) */
    { 0x1E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x1E, 0x00 },   /* U+005D (]) */
    { 0x08, 0x1C, 0x36, 0x63, 0x00, 0x00, 0x00, 0x00 },   /* U+005E (^) */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF },   /* U+005F (_) */
    { 0x0C, 0x0C, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* U+0060 (`) */
    { 0x00, 0x00, 0x1E, 0x30, 0x3E, 0x33, 0x6E, 0x00 },   /* U+0061 (a) */
    { 0x07, 0x06, 0x06, 0x3E, 0x66, 0x66, 0x3B, 0x00 },   /* U+0062 (b) */
    { 0x00, 0x00, 0x1E, 0x33, 0x03, 0x33, 0x1E, 0x00 },   /* U+0063 (c) */
    { 0x38, 0x30, 0x30, 0x3E, 0x33, 0x33, 0x6E, 0x00 },   /* U+0064 (d) */
    { 0x00, 0x00, 0x1E, 0x33, 0x3F, 0x03, 0x1E, 0x00 },   /* U+0065 (e) */
    { 0x1C, 0x36, 0x06, 0x0F, 0x06, 0x06, 0x0F, 0x00 },   /* U+0066 (f) */
    { 0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x1F },   /* U+0067 (g) */
    { 0x07, 0x06, 0x36, 0x6E, 0x66, 0x66, 0x67, 0x00 },   /* U+0068 (h) */
    { 0x0C, 0x00, 0x0E, 0x0C, 0x0C, 0x0C, 0x1E, 0x00 },   /* U+0069 (i) */
    { 0x30, 0x00, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E },   /* U+006A (j) */
    { 0x07, 0x06, 0x66, 0x36, 0x1E, 0x36, 0x67, 0x00 },   /* U+006B (k) */
    { 0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00 },   /* U+006C (l) */
    { 0x00, 0x00, 0x33, 0x7F, 0x7F, 0x6B, 0x63, 0x00 },   /* U+006D (m) */
    { 0x00, 0x00, 0x1F, 0x33, 0x33, 0x33, 0x33, 0x00 },   /* U+006E (n) */
    { 0x00, 0x00, 0x1E, 0x33, 0x33, 0x33, 0x1E, 0x00 },   /* U+006F (o) */
    { 0x00, 0x00, 0x3B, 0x66, 0x66, 0x3E, 0x06, 0x0F },   /* U+0070 (p) */
    { 0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x78 },   /* U+0071 (q) */
    { 0x00, 0x00, 0x3B, 0x6E, 0x66, 0x06, 0x0F, 0x00 },   /* U+0072 (r) */
    { 0x00, 0x00, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x00 },   /* U+0073 (s) */
    { 0x08, 0x0C, 0x3E, 0x0C, 0x0C, 0x2C, 0x18, 0x00 },   /* U+0074 (t) */
    { 0x00, 0x00, 0x33, 0x33, 0x33, 0x33, 0x6E, 0x00 },   /* U+0075 (u) */
    { 0x00, 0x00, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00 },   /* U+0076 (v) */
    { 0x00, 0x00, 0x63, 0x6B, 0x7F, 0x7F, 0x36, 0x00 },   /* U+0077 (w) */
    { 0x00, 0x00, 0x63, 0x36, 0x1C, 0x36, 0x63, 0x00 },   /* U+0078 (x) */
    { 0x00, 0x00, 0x33, 0x33, 0x33, 0x3E, 0x30, 0x1F },   /* U+0079 (y) */
    { 0x00, 0x00, 0x3F, 0x19, 0x0C, 0x26, 0x3F, 0x00 },   /* U+007A (z) */
    { 0x38, 0x0C, 0x0C, 0x07, 0x0C, 0x0C, 0x38, 0x00 },   /* U+007B ({) */
    { 0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x18, 0x00 },   /* U+007C (|) */
    { 0x07, 0x0C, 0x0C, 0x38, 0x0C, 0x0C, 0x07, 0x00 },   /* U+007D (}) */
    { 0x6E, 0x3B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* U+007E (~) */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* U+007F */
};

/* ══════════════════════════════════════════════════════════════════════
   OpenGL resources
   ══════════════════════════════════════════════════════════════════════ */

static GLuint t_font_tex;    /* GL_RED 8-bit font atlas texture */
static GLuint t_prog;        /* shader program */
static GLuint t_vao, t_vbo;  /* dynamic quad batch */

static GLint t_u_resolution;
static GLint t_u_font_atlas;
static GLint t_u_color;

/* Max characters per single text_draw call */
#define MAX_CHARS 512
/* 6 vertices per char, 4 floats per vertex (x, y, u, v) */
#define VBO_SIZE (MAX_CHARS * 6 * 4 * sizeof(float))

/* Shared scratch buffer for vertex data (single-threaded text rendering) */
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

static const char *t_frag_src =
    "#version 410 core\n"
    "in vec2 vTexCoord;\n"
    "uniform sampler2D uFontAtlas;\n"
    "uniform vec3 uColor;\n"
    "out vec4 FragColor;\n"
    "void main() {\n"
    "    float a = texture(uFontAtlas, vTexCoord).r;\n"
    "    if (a < 0.5) discard;\n"
    "    FragColor = vec4(uColor, 1.0);\n"
    "}\n";

/* ── Shader compilation (mirrors face.c pattern) ─────────────────── */

static GLuint t_compile(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    int ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        fprintf(stderr, "text shader compile: %s\n", log);
    }
    return s;
}

static GLuint t_link(GLuint vert, GLuint frag) {
    GLuint p = glCreateProgram();
    glAttachShader(p, vert);
    glAttachShader(p, frag);
    glLinkProgram(p);
    int ok;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(p, sizeof(log), NULL, log);
        fprintf(stderr, "text shader link: %s\n", log);
    }
    return p;
}

/* ── Build font atlas texture ────────────────────────────────────── */

static void t_build_atlas(void) {
    /* Allocate atlas: 128 x 64 single-channel */
    unsigned char atlas[ATLAS_W * ATLAS_H];
    memset(atlas, 0, sizeof(atlas));

    for (int ch = 0; ch < FONT_CHARS; ch++) {
        int col = ch % ATLAS_COLS;
        int row = ch / ATLAS_COLS;
        int ox = col * FONT_W;
        int oy = row * FONT_H;

        for (int y = 0; y < FONT_H; y++) {
            unsigned char bits = font8x8_data[ch][y];
            for (int x = 0; x < FONT_W; x++) {
                /* LSB = leftmost pixel for this font data */
                int set = (bits >> x) & 1;
                atlas[(oy + y) * ATLAS_W + (ox + x)] = set ? 0xFF : 0x00;
            }
        }
    }

    glGenTextures(1, &t_font_tex);
    glBindTexture(GL_TEXTURE_2D, t_font_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, ATLAS_W, ATLAS_H, 0,
                 GL_RED, GL_UNSIGNED_BYTE, atlas);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

/* ══════════════════════════════════════════════════════════════════════
   Public API
   ══════════════════════════════════════════════════════════════════════ */

void text_init(void) {
    /* Compile shaders */
    GLuint vert = t_compile(GL_VERTEX_SHADER, t_vert_src);
    GLuint frag = t_compile(GL_FRAGMENT_SHADER, t_frag_src);
    t_prog = t_link(vert, frag);
    glDeleteShader(vert);
    glDeleteShader(frag);

    t_u_resolution = glGetUniformLocation(t_prog, "uResolution");
    t_u_font_atlas = glGetUniformLocation(t_prog, "uFontAtlas");
    t_u_color      = glGetUniformLocation(t_prog, "uColor");

    /* Build font atlas texture */
    t_build_atlas();

    /* Create VAO/VBO for dynamic text quads */
    glGenVertexArrays(1, &t_vao);
    glGenBuffers(1, &t_vbo);
    glBindVertexArray(t_vao);
    glBindBuffer(GL_ARRAY_BUFFER, t_vbo);
    glBufferData(GL_ARRAY_BUFFER, VBO_SIZE, NULL, GL_DYNAMIC_DRAW);

    /* aPos: location 0, 2 floats */
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);
    /* aTexCoord: location 1, 2 floats */
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          (void *)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);
}

void text_draw(const char *str, float x, float y, float scale,
               float r, float g, float b, float fb_w, float fb_h) {
    if (!str || !*str) return;

    size_t len = strlen(str);
    if (len > MAX_CHARS) len = MAX_CHARS;

    float gw = FONT_W * scale;  /* glyph width in pixels */
    float gh = FONT_H * scale;  /* glyph height in pixels */

    /* Build vertex data: 6 verts per char, 4 floats per vert */
    float *verts = t_vert_scratch;
    int vert_count = 0;

    float cursor_x = x;
    float cursor_y = y;

    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)str[i];
        if (c == '\n') {
            cursor_x = x;
            cursor_y += gh;
            continue;
        }
        if (c == ' ') {
            cursor_x += gw;
            continue;
        }
        if (c < FONT_FIRST || c > FONT_LAST) c = '?';

        /* Texture coordinates for this character in the atlas */
        int col = c % ATLAS_COLS;
        int row = c / ATLAS_COLS;
        float u0 = (float)(col * FONT_W) / (float)ATLAS_W;
        float v0 = (float)(row * FONT_H) / (float)ATLAS_H;
        float u1 = (float)((col + 1) * FONT_W) / (float)ATLAS_W;
        float v1 = (float)((row + 1) * FONT_H) / (float)ATLAS_H;

        /* Pixel positions for the quad */
        float px0 = cursor_x;
        float py0 = cursor_y;
        float px1 = cursor_x + gw;
        float py1 = cursor_y + gh;

        /* Triangle 1: top-left, top-right, bottom-right */
        int vi = vert_count * 4;
        verts[vi + 0] = px0; verts[vi + 1] = py0; verts[vi + 2] = u0; verts[vi + 3] = v0;
        vi += 4;
        verts[vi + 0] = px1; verts[vi + 1] = py0; verts[vi + 2] = u1; verts[vi + 3] = v0;
        vi += 4;
        verts[vi + 0] = px1; verts[vi + 1] = py1; verts[vi + 2] = u1; verts[vi + 3] = v1;
        vert_count += 3;

        /* Triangle 2: top-left, bottom-right, bottom-left */
        vi = vert_count * 4;
        verts[vi + 0] = px0; verts[vi + 1] = py0; verts[vi + 2] = u0; verts[vi + 3] = v0;
        vi += 4;
        verts[vi + 0] = px1; verts[vi + 1] = py1; verts[vi + 2] = u1; verts[vi + 3] = v1;
        vi += 4;
        verts[vi + 0] = px0; verts[vi + 1] = py1; verts[vi + 2] = u0; verts[vi + 3] = v1;
        vert_count += 3;

        cursor_x += gw;
    }

    if (vert_count == 0) return;

    /* Upload and draw */
    glUseProgram(t_prog);
    glUniform2f(t_u_resolution, fb_w, fb_h);
    glUniform1i(t_u_font_atlas, 0);
    glUniform3f(t_u_color, r, g, b);

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
    float gw = FONT_W * scale;
    float max_w = 0.0f;
    float cur_w = 0.0f;
    for (const char *p = str; *p; p++) {
        if (*p == '\n') {
            if (cur_w > max_w) max_w = cur_w;
            cur_w = 0.0f;
        } else {
            cur_w += gw;
        }
    }
    if (cur_w > max_w) max_w = cur_w;
    return max_w;
}

float text_line_height(float scale) {
    return FONT_H * scale;
}

float text_draw_wrapped(const char *str, float x, float y,
                        float max_width, float scale,
                        float r, float g, float b,
                        float fb_w, float fb_h) {
    if (!str || !*str) return 0.0f;

    float gw = FONT_W * scale;
    float gh = FONT_H * scale;

    /* First pass: compute wrapped layout into a temporary buffer.
       We build one large string of positioned characters and batch-draw. */
    float *verts = t_vert_scratch;
    int vert_count = 0;
    int char_count = 0;

    float cursor_x = x;
    float cursor_y = y;
    float start_y = y;

    const char *p = str;
    while (*p && char_count < MAX_CHARS) {
        /* Handle explicit newlines */
        if (*p == '\n') {
            cursor_x = x;
            cursor_y += gh;
            p++;
            continue;
        }

        /* Find the next word boundary */
        const char *word_end = p;
        while (*word_end && *word_end != ' ' && *word_end != '\n') {
            word_end++;
        }
        float word_width = (float)(word_end - p) * gw;

        /* If the word doesn't fit on this line and we're not at the start,
           wrap to the next line */
        if (cursor_x > x && (cursor_x - x + word_width) > max_width) {
            cursor_x = x;
            cursor_y += gh;
        }

        /* Emit quads for the word character by character */
        while (p < word_end && char_count < MAX_CHARS) {
            /* If a single character exceeds max_width from line start,
               force a line break */
            if (cursor_x > x && (cursor_x - x + gw) > max_width) {
                cursor_x = x;
                cursor_y += gh;
            }

            unsigned char c = (unsigned char)*p;
            if (c < FONT_FIRST || c > FONT_LAST) c = '?';

            int col = c % ATLAS_COLS;
            int row = c / ATLAS_COLS;
            float u0 = (float)(col * FONT_W) / (float)ATLAS_W;
            float v0 = (float)(row * FONT_H) / (float)ATLAS_H;
            float u1 = (float)((col + 1) * FONT_W) / (float)ATLAS_W;
            float v1 = (float)((row + 1) * FONT_H) / (float)ATLAS_H;

            float px0 = cursor_x;
            float py0 = cursor_y;
            float px1 = cursor_x + gw;
            float py1 = cursor_y + gh;

            int vi = vert_count * 4;
            verts[vi+0]=px0; verts[vi+1]=py0; verts[vi+2]=u0; verts[vi+3]=v0;
            vi += 4;
            verts[vi+0]=px1; verts[vi+1]=py0; verts[vi+2]=u1; verts[vi+3]=v0;
            vi += 4;
            verts[vi+0]=px1; verts[vi+1]=py1; verts[vi+2]=u1; verts[vi+3]=v1;
            vert_count += 3;

            vi = vert_count * 4;
            verts[vi+0]=px0; verts[vi+1]=py0; verts[vi+2]=u0; verts[vi+3]=v0;
            vi += 4;
            verts[vi+0]=px1; verts[vi+1]=py1; verts[vi+2]=u1; verts[vi+3]=v1;
            vi += 4;
            verts[vi+0]=px0; verts[vi+1]=py1; verts[vi+2]=u0; verts[vi+3]=v1;
            vert_count += 3;

            char_count++;
            cursor_x += gw;
            p++;
        }

        /* Consume the space after the word */
        if (*p == ' ') {
            cursor_x += gw;
            p++;
        }
    }

    if (vert_count == 0) return gh;

    /* Single batched draw call for all wrapped text */
    glUseProgram(t_prog);
    glUniform2f(t_u_resolution, fb_w, fb_h);
    glUniform1i(t_u_font_atlas, 0);
    glUniform3f(t_u_color, r, g, b);

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

    return (cursor_y - start_y) + gh;
}

void text_cleanup(void) {
    glDeleteTextures(1, &t_font_tex);
    glDeleteProgram(t_prog);
    glDeleteVertexArrays(1, &t_vao);
    glDeleteBuffers(1, &t_vbo);
}
