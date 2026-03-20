#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include "platform.h"

/* ── Ellipse geometry ──────────────────────────────────────────────── */

#define SEGMENTS 64
#define PI 3.14159265358979323846f

static GLuint ellipse_vao, ellipse_vbo;

static void init_ellipse(void) {
    float verts[(SEGMENTS + 2) * 2];
    verts[0] = 0.0f;
    verts[1] = 0.0f;
    for (int i = 0; i <= SEGMENTS; i++) {
        float a = (float)i / (float)SEGMENTS * 2.0f * PI;
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

static void draw_circle(float cx, float cy, float r, float cr, float cg, float cb) {
    glUniform2f(u_center, cx, cy);
    glUniform2f(u_radius, r, r);
    glUniform3f(u_color, cr, cg, cb);
    glBindVertexArray(ellipse_vao);
    glDrawArrays(GL_TRIANGLE_FAN, 0, SEGMENTS + 2);
}

/* ── Helpers ───────────────────────────────────────────────────────── */

static float lerpf(float a, float b, float t) {
    return a + (b - a) * t;
}

/* Shortest-path angle lerp (handles wrap-around) */
static float lerp_angle(float a, float b, float t) {
    float diff = fmodf(b - a + 3.0f * PI, 2.0f * PI) - PI;
    return a + diff * t;
}

/* ── Callbacks ─────────────────────────────────────────────────────── */

static int fb_w, fb_h, win_w, win_h;
static double mouse_x, mouse_y;

static void framebuffer_size_cb(GLFWwindow *w, int width, int height) {
    (void)w; fb_w = width; fb_h = height;
    glViewport(0, 0, width, height);
}

static void window_size_cb(GLFWwindow *w, int width, int height) {
    (void)w; win_w = width; win_h = height;
}

static void cursor_pos_cb(GLFWwindow *w, double x, double y) {
    (void)w; mouse_x = x; mouse_y = y;
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

    /* Eye layout — matching the React reference */
    const float eye_r      = 45.0f;   /* eye (white) radius */
    const float pupil_r    = 14.0f;   /* pupil radius */
    const float pupil_dist = 22.0f;   /* how far pupil orbits from center */
    const float eye_gap    = 120.0f;  /* distance between eye centers */

    /* Colors */
    const float white_eye[] = {0.953f, 0.937f, 0.937f};  /* #f3efef */
    const float red_eye[]   = {0.973f, 0.776f, 0.776f};  /* #f8c6c6 */
    const float pupil_col[] = {0.10f,  0.10f,  0.10f};

    /* Smoothed angles for each eye (the "tiredness" effect) */
    float smooth_angle[2] = {0.0f, 0.0f};
    float smooth_r[2]     = {white_eye[0], white_eye[0]};
    float smooth_g[2]     = {white_eye[1], white_eye[1]};
    float smooth_b[2]     = {white_eye[2], white_eye[2]};
    double last_time = glfwGetTime();

    while (!glfwWindowShouldClose(window)) {
        double now = glfwGetTime();
        float dt = (float)(now - last_time);
        last_time = now;

        glViewport(0, 0, fb_w, fb_h);
        glClearColor(0.11f, 0.11f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(prog);
        glUniform2f(u_resolution, (float)fb_w, (float)fb_h);

        float scale = (float)fb_w / (float)win_w;
        float center_x = (float)fb_w * 0.5f;
        float center_y = (float)fb_h * 0.5f;
        float mx = (float)mouse_x * scale;
        float my = (float)mouse_y * scale;

        /* Compute raw angle for each eye (degrees, 0=right, CCW) */
        float angles_deg[2];
        float eye_cx[2];
        for (int i = 0; i < 2; i++) {
            float side = (i == 0) ? -1.0f : 1.0f;
            eye_cx[i] = center_x + side * eye_gap * 0.5f * scale;
            float dx = mx - eye_cx[i];
            float dy = my - center_y;
            float angle_rad = atan2f(-dy, dx);  /* screen Y is flipped */
            float angle_deg = angle_rad * 180.0f / PI;
            if (angle_deg < 0.0f) angle_deg += 360.0f;
            angles_deg[i] = angle_deg;
        }

        /* Cross-eye detection (from React reference):
           Left eye looking right (330-360 or 0-30) AND
           right eye looking left (150-210) */
        int cross_eyed =
            (angles_deg[0] < 30.0f || angles_deg[0] > 330.0f) &&
            (angles_deg[1] > 150.0f && angles_deg[1] < 210.0f);

        /* Smoothing factor — higher = snappier */
        float smoothing = 12.0f;
        float color_smoothing = 6.0f;
        float t  = 1.0f - expf(-smoothing * dt);
        float tc = 1.0f - expf(-color_smoothing * dt);

        for (int i = 0; i < 2; i++) {
            float target_rad = angles_deg[i] * PI / 180.0f;

            /* Smooth the angle */
            smooth_angle[i] = lerp_angle(smooth_angle[i], target_rad, t);

            /* Smooth the color */
            const float *target_col = cross_eyed ? red_eye : white_eye;
            smooth_r[i] = lerpf(smooth_r[i], target_col[0], tc);
            smooth_g[i] = lerpf(smooth_g[i], target_col[1], tc);
            smooth_b[i] = lerpf(smooth_b[i], target_col[2], tc);

            /* Draw eye (white circle) */
            draw_circle(eye_cx[i], center_y, eye_r * scale,
                        smooth_r[i], smooth_g[i], smooth_b[i]);

            /* Draw pupil orbiting inside the eye */
            float px = eye_cx[i] + cosf(smooth_angle[i]) * pupil_dist * scale;
            float py = center_y  - sinf(smooth_angle[i]) * pupil_dist * scale;
            draw_circle(px, py, pupil_r * scale,
                        pupil_col[0], pupil_col[1], pupil_col[2]);
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
