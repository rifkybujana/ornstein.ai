#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include "platform.h"

/* ── Ellipse geometry ──────────────────────────────────────────────── */

#define SEGMENTS 64

static GLuint ellipse_vao, ellipse_vbo;

static void init_ellipse(void) {
    /* Unit circle fan: center + SEGMENTS + 1 (to close the loop) */
    float verts[(SEGMENTS + 2) * 2];
    verts[0] = 0.0f;
    verts[1] = 0.0f;
    for (int i = 0; i <= SEGMENTS; i++) {
        float a = (float)i / (float)SEGMENTS * 2.0f * (float)M_PI;
        verts[(i + 1) * 2 + 0] = cosf(a);
        verts[(i + 1) * 2 + 1] = sinf(a);
    }

    glGenVertexArrays(1, &ellipse_vao);
    glGenBuffers(1, &ellipse_vbo);
    glBindVertexArray(ellipse_vao);
    glBindBuffer(GL_ARRAY_BUFFER, ellipse_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
}

/* ── Shader ────────────────────────────────────────────────────────── */

static const char *vert_src =
    "#version 410 core\n"
    "layout (location = 0) in vec2 aPos;\n"
    "uniform vec2 uCenter;\n"
    "uniform vec2 uRadius;\n"
    "uniform vec2 uResolution;\n"
    "uniform float uAngle;\n"
    "void main() {\n"
    "    vec2 scaled = aPos * uRadius;\n"
    "    float c = cos(uAngle), s = sin(uAngle);\n"
    "    vec2 rotated = vec2(scaled.x*c - scaled.y*s, scaled.x*s + scaled.y*c);\n"
    "    vec2 px = uCenter + rotated;\n"
    "    vec2 ndc = (px / uResolution) * 2.0 - 1.0;\n"
    "    ndc.y = -ndc.y;\n"
    "    gl_Position = vec4(ndc, 0.0, 1.0);\n"
    "}\n";

static const char *frag_src =
    "#version 410 core\n"
    "uniform vec3 uColor;\n"
    "out vec4 FragColor;\n"
    "void main() {\n"
    "    FragColor = vec4(uColor, 1.0);\n"
    "}\n";

static GLuint prog;
static GLint u_center, u_radius, u_resolution, u_color, u_angle;

static GLuint compile(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    int ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        fprintf(stderr, "Shader: %s\n", log);
        exit(1);
    }
    return s;
}

static void init_shader(void) {
    GLuint v = compile(GL_VERTEX_SHADER, vert_src);
    GLuint f = compile(GL_FRAGMENT_SHADER, frag_src);
    prog = glCreateProgram();
    glAttachShader(prog, v);
    glAttachShader(prog, f);
    glLinkProgram(prog);
    glDeleteShader(v);
    glDeleteShader(f);

    u_center     = glGetUniformLocation(prog, "uCenter");
    u_radius     = glGetUniformLocation(prog, "uRadius");
    u_resolution = glGetUniformLocation(prog, "uResolution");
    u_color      = glGetUniformLocation(prog, "uColor");
    u_angle      = glGetUniformLocation(prog, "uAngle");
}

/* ── Draw an ellipse at (cx, cy) with radii (rx, ry) ──────────────── */

static void draw_ellipse(float cx, float cy, float rx, float ry,
                         float angle, float r, float g, float b) {
    glUniform2f(u_center, cx, cy);
    glUniform2f(u_radius, rx, ry);
    glUniform1f(u_angle, angle);
    glUniform3f(u_color, r, g, b);
    glBindVertexArray(ellipse_vao);
    glDrawArrays(GL_TRIANGLE_FAN, 0, SEGMENTS + 2);
}

/* ── Callbacks ─────────────────────────────────────────────────────── */

static int fb_w, fb_h, win_w, win_h;
static double mouse_x, mouse_y;

static void framebuffer_size_cb(GLFWwindow *w, int width, int height) {
    (void)w;
    fb_w = width; fb_h = height;
    glViewport(0, 0, width, height);
}

static void window_size_cb(GLFWwindow *w, int width, int height) {
    (void)w;
    win_w = width; win_h = height;
}

static void cursor_pos_cb(GLFWwindow *w, double x, double y) {
    (void)w;
    mouse_x = x; mouse_y = y;
}

static void key_cb(GLFWwindow *w, int key, int sc, int action, int mods) {
    (void)sc; (void)mods;
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
        glfwSetWindowShouldClose(w, GLFW_TRUE);
}

/* ── Main ──────────────────────────────────────────────────────────── */

int main(void) {
    if (!glfwInit()) {
        fprintf(stderr, "Failed to initialize GLFW\n");
        return EXIT_FAILURE;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

    GLFWwindow *window = glfwCreateWindow(1000, 700, "ornstein", NULL, NULL);
    if (!window) {
        fprintf(stderr, "Failed to create GLFW window\n");
        glfwTerminate();
        return EXIT_FAILURE;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    glfwGetWindowSize(window, &win_w, &win_h);
    glfwGetFramebufferSize(window, &fb_w, &fb_h);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_cb);
    glfwSetWindowSizeCallback(window, window_size_cb);
    glfwSetCursorPosCallback(window, cursor_pos_cb);
    glfwSetKeyCallback(window, key_cb);

    platform_style_window(window);

    int version = gladLoadGL(glfwGetProcAddress);
    if (!version) {
        fprintf(stderr, "Failed to initialize GLAD\n");
        glfwTerminate();
        return EXIT_FAILURE;
    }
    printf("OpenGL %d.%d\n", GLAD_VERSION_MAJOR(version), GLAD_VERSION_MINOR(version));

    init_shader();
    init_ellipse();
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    /* Hollow Knight style: simple white ovals, no iris/pupil.
       Eyes are tall, narrow, slightly tilted inward, close together. */
    const float eye_rx  = 22.0f;   /* narrow */
    const float eye_ry  = 55.0f;   /* tall */
    const float eye_gap = 70.0f;   /* close together */
    const float eye_tilt = 0.15f;  /* inward tilt (radians) */
    const float max_look = 8.0f;   /* subtle shift toward cursor */

    while (!glfwWindowShouldClose(window)) {
        glViewport(0, 0, fb_w, fb_h);
        glClearColor(0.11f, 0.11f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(prog);
        glUniform2f(u_resolution, (float)fb_w, (float)fb_h);

        float scale = (float)fb_w / (float)win_w;
        float cx = (float)fb_w * 0.5f;
        float cy = (float)fb_h * 0.5f;

        float mx = (float)mouse_x * scale;
        float my = (float)mouse_y * scale;

        for (int i = 0; i < 2; i++) {
            float side = (i == 0) ? -1.0f : 1.0f;
            float ex = cx + side * eye_gap * 0.5f * scale;
            float ey = cy;

            /* Subtle shift toward cursor */
            float dx = mx - ex;
            float dy = my - ey;
            float dist = sqrtf(dx * dx + dy * dy);
            float ox = 0.0f, oy = 0.0f;
            if (dist > 0.001f) {
                float t = fminf(dist, max_look * scale) / dist;
                ox = dx * t;
                oy = dy * t;
            }

            /* Tilt inward: left eye tilts right, right eye tilts left */
            float angle = side * eye_tilt;

            draw_ellipse(ex + ox, ey + oy,
                         eye_rx * scale, eye_ry * scale,
                         angle, 1.0f, 1.0f, 1.0f);
        }

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glDeleteVertexArrays(1, &ellipse_vao);
    glDeleteBuffers(1, &ellipse_vbo);
    glDeleteProgram(prog);
    glfwDestroyWindow(window);
    glfwTerminate();
    return EXIT_SUCCESS;
}
