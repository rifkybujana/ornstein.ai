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
    "uniform vec2 uCenter;\n"   /* pixel coords */
    "uniform vec2 uRadius;\n"   /* pixel half-widths */
    "uniform vec2 uResolution;\n"
    "void main() {\n"
    "    vec2 px = uCenter + aPos * uRadius;\n"
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
static GLint u_center, u_radius, u_resolution, u_color;

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
}

/* ── Draw an ellipse at (cx, cy) with radii (rx, ry) ──────────────── */

static void draw_ellipse(float cx, float cy, float rx, float ry,
                         float r, float g, float b) {
    glUniform2f(u_center, cx, cy);
    glUniform2f(u_radius, rx, ry);
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

    /* Eye dimensions (in framebuffer pixels) */
    const float eye_rx   = 60.0f;   /* sclera horizontal radius */
    const float eye_ry   = 80.0f;   /* sclera vertical radius (taller) */
    const float iris_r   = 28.0f;
    const float pupil_r  = 12.0f;
    const float eye_gap  = 180.0f;  /* distance between eye centers */

    /* Colors */
    const float bone[]  = {0.87f, 0.84f, 0.78f};  /* sclera / bone white */
    const float iris_c[] = {0.30f, 0.22f, 0.15f};  /* dark brown iris */
    const float pupil[] = {0.05f, 0.05f, 0.05f};   /* near-black pupil */

    while (!glfwWindowShouldClose(window)) {
        glViewport(0, 0, fb_w, fb_h);
        glClearColor(0.11f, 0.11f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(prog);
        glUniform2f(u_resolution, (float)fb_w, (float)fb_h);

        float scale = (float)fb_w / (float)win_w;
        float cx = (float)fb_w * 0.5f;
        float cy = (float)fb_h * 0.5f;

        /* Cursor in framebuffer coords */
        float mx = (float)mouse_x * scale;
        float my = (float)mouse_y * scale;

        /* Max distance the iris/pupil can travel from eye center */
        float max_travel = (eye_ry - iris_r) * 0.6f;

        for (int i = 0; i < 2; i++) {
            float ex = cx + (i == 0 ? -eye_gap * 0.5f : eye_gap * 0.5f) * scale;
            float ey = cy;

            /* Direction from eye center to cursor */
            float dx = mx - ex;
            float dy = my - ey;
            float dist = sqrtf(dx * dx + dy * dy);

            /* Iris/pupil offset clamped to max travel */
            float ox = 0.0f, oy = 0.0f;
            if (dist > 0.001f) {
                float t = fminf(dist, max_travel * scale) / dist;
                ox = dx * t;
                oy = dy * t;
            }

            /* Sclera */
            draw_ellipse(ex, ey, eye_rx * scale, eye_ry * scale,
                         bone[0], bone[1], bone[2]);
            /* Iris */
            draw_ellipse(ex + ox, ey + oy, iris_r * scale, iris_r * scale,
                         iris_c[0], iris_c[1], iris_c[2]);
            /* Pupil */
            draw_ellipse(ex + ox, ey + oy, pupil_r * scale, pupil_r * scale,
                         pupil[0], pupil[1], pupil[2]);
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
