#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include <glad/gl.h>
#include <GLFW/glfw3.h>

/* ── Constants ─────────────────────────────────────────────────────── */

#define TITLE_BAR_HEIGHT  32.0f
#define BUTTON_SIZE       16.0f
#define BUTTON_PADDING    12.0f
#define BUTTON_SPACING    8.0f
#define RESIZE_BORDER     6.0f
#define MIN_WIN_W         400
#define MIN_WIN_H         200

/* Dark theme colors */
#define BG_R     0.11f
#define BG_G     0.11f
#define BG_B     0.12f
#define TITLE_R  0.14f
#define TITLE_G  0.14f
#define TITLE_B  0.15f

/* ── Window state ──────────────────────────────────────────────────── */

typedef enum {
    HOVER_NONE,
    HOVER_CLOSE,
    HOVER_MAXIMIZE,
    HOVER_MINIMIZE,
} HoverButton;

typedef enum {
    RESIZE_NONE   = 0,
    RESIZE_LEFT   = 1 << 0,
    RESIZE_RIGHT  = 1 << 1,
    RESIZE_TOP    = 1 << 2,
    RESIZE_BOTTOM = 1 << 3,
} ResizeEdge;

typedef struct {
    int          dragging;
    double       drag_x, drag_y;
    int          resizing;
    ResizeEdge   resize_edge;
    double       resize_start_cx, resize_start_cy;
    int          resize_start_x, resize_start_y;
    int          resize_start_w, resize_start_h;
    int          maximized;
    int          pre_max_x, pre_max_y;
    int          pre_max_w, pre_max_h;
    int          win_w, win_h;
    int          fb_w, fb_h;
    HoverButton  hover;
} AppState;

static AppState state = {0};

/* ── Shaders ───────────────────────────────────────────────────────── */

static const char *vert_src =
    "#version 410 core\n"
    "layout (location = 0) in vec2 aPos;\n"
    "uniform vec2 uResolution;\n"
    "void main() {\n"
    "    vec2 ndc = (aPos / uResolution) * 2.0 - 1.0;\n"
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

static GLuint shader_prog;
static GLint  u_resolution, u_color;
static GLuint quad_vao, quad_vbo;

static GLuint compile_shader(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    int ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        fprintf(stderr, "Shader error: %s\n", log);
        exit(EXIT_FAILURE);
    }
    return s;
}

static void init_renderer(void) {
    GLuint v = compile_shader(GL_VERTEX_SHADER, vert_src);
    GLuint f = compile_shader(GL_FRAGMENT_SHADER, frag_src);
    shader_prog = glCreateProgram();
    glAttachShader(shader_prog, v);
    glAttachShader(shader_prog, f);
    glLinkProgram(shader_prog);
    glDeleteShader(v);
    glDeleteShader(f);

    u_resolution = glGetUniformLocation(shader_prog, "uResolution");
    u_color      = glGetUniformLocation(shader_prog, "uColor");

    glGenVertexArrays(1, &quad_vao);
    glGenBuffers(1, &quad_vbo);
    glBindVertexArray(quad_vao);
    glBindBuffer(GL_ARRAY_BUFFER, quad_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 12, NULL, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
}

/* ── Drawing helpers (pixel coordinates) ───────────────────────────── */

static void draw_rect(float x, float y, float w, float h, float r, float g, float b) {
    float verts[] = {
        x,     y,
        x + w, y,
        x + w, y + h,
        x,     y,
        x + w, y + h,
        x,     y + h,
    };
    glBindVertexArray(quad_vao);
    glBindBuffer(GL_ARRAY_BUFFER, quad_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
    glUniform3f(u_color, r, g, b);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

static void draw_line(float x1, float y1, float x2, float y2, float thickness,
                       float r, float g, float b) {
    float dx = x2 - x1, dy = y2 - y1;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.001f) return;
    float nx = -dy / len * thickness * 0.5f;
    float ny =  dx / len * thickness * 0.5f;

    float verts[] = {
        x1 + nx, y1 + ny,
        x1 - nx, y1 - ny,
        x2 - nx, y2 - ny,
        x1 + nx, y1 + ny,
        x2 - nx, y2 - ny,
        x2 + nx, y2 + ny,
    };
    glBindVertexArray(quad_vao);
    glBindBuffer(GL_ARRAY_BUFFER, quad_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
    glUniform3f(u_color, r, g, b);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

/* ── Hit testing ───────────────────────────────────────────────────── */

static float pixel_ratio(GLFWwindow *win) {
    int ww, wh, fw, fh;
    glfwGetWindowSize(win, &ww, &wh);
    glfwGetFramebufferSize(win, &fw, &fh);
    return (ww > 0) ? (float)fw / (float)ww : 1.0f;
}

static HoverButton hit_test_buttons(double mx, double my, int win_w) {
    float by = (TITLE_BAR_HEIGHT - BUTTON_SIZE) * 0.5f;
    float bx_close    = (float)win_w - BUTTON_PADDING - BUTTON_SIZE;
    float bx_maximize  = bx_close - BUTTON_SPACING - BUTTON_SIZE;
    float bx_minimize  = bx_maximize - BUTTON_SPACING - BUTTON_SIZE;

    if (mx >= bx_close && mx <= bx_close + BUTTON_SIZE &&
        my >= by && my <= by + BUTTON_SIZE)
        return HOVER_CLOSE;
    if (mx >= bx_maximize && mx <= bx_maximize + BUTTON_SIZE &&
        my >= by && my <= by + BUTTON_SIZE)
        return HOVER_MAXIMIZE;
    if (mx >= bx_minimize && mx <= bx_minimize + BUTTON_SIZE &&
        my >= by && my <= by + BUTTON_SIZE)
        return HOVER_MINIMIZE;
    return HOVER_NONE;
}

static ResizeEdge hit_test_resize(double mx, double my, int win_w, int win_h) {
    ResizeEdge edge = RESIZE_NONE;
    if (mx < RESIZE_BORDER)                  edge |= RESIZE_LEFT;
    if (mx > (double)win_w - RESIZE_BORDER)  edge |= RESIZE_RIGHT;
    if (my < RESIZE_BORDER)                  edge |= RESIZE_TOP;
    if (my > (double)win_h - RESIZE_BORDER)  edge |= RESIZE_BOTTOM;
    return edge;
}

/* ── Callbacks ─────────────────────────────────────────────────────── */

static void framebuffer_size_cb(GLFWwindow *win, int w, int h) {
    (void)win;
    state.fb_w = w;
    state.fb_h = h;
    glViewport(0, 0, w, h);
}

static void window_size_cb(GLFWwindow *win, int w, int h) {
    (void)win;
    state.win_w = w;
    state.win_h = h;
}

static void cursor_pos_cb(GLFWwindow *win, double mx, double my) {
    /* Update hover state */
    state.hover = hit_test_buttons(mx, my, state.win_w);

    /* Update cursor for resize edges */
    if (!state.dragging && !state.resizing) {
        ResizeEdge edge = hit_test_resize(mx, my, state.win_w, state.win_h);
        if ((edge & RESIZE_LEFT && edge & RESIZE_TOP) ||
            (edge & RESIZE_RIGHT && edge & RESIZE_BOTTOM))
            glfwSetCursor(win, glfwCreateStandardCursor(GLFW_RESIZE_NWSE_CURSOR));
        else if ((edge & RESIZE_RIGHT && edge & RESIZE_TOP) ||
                 (edge & RESIZE_LEFT && edge & RESIZE_BOTTOM))
            glfwSetCursor(win, glfwCreateStandardCursor(GLFW_RESIZE_NESW_CURSOR));
        else if (edge & (RESIZE_LEFT | RESIZE_RIGHT))
            glfwSetCursor(win, glfwCreateStandardCursor(GLFW_RESIZE_EW_CURSOR));
        else if (edge & (RESIZE_TOP | RESIZE_BOTTOM))
            glfwSetCursor(win, glfwCreateStandardCursor(GLFW_RESIZE_NS_CURSOR));
        else
            glfwSetCursor(win, NULL);
    }

    /* Drag window */
    if (state.dragging) {
        double gx, gy;
        glfwGetCursorPos(win, &gx, &gy);
        int wx, wy;
        glfwGetWindowPos(win, &wx, &wy);
        glfwSetWindowPos(win,
            wx + (int)(gx - state.drag_x),
            wy + (int)(gy - state.drag_y));
    }

    /* Resize window */
    if (state.resizing) {
        double gx, gy;
        glfwGetCursorPos(win, &gx, &gy);
        int wx, wy;
        glfwGetWindowPos(win, &wx, &wy);

        int new_x = state.resize_start_x;
        int new_y = state.resize_start_y;
        int new_w = state.resize_start_w;
        int new_h = state.resize_start_h;

        double dx = gx - state.resize_start_cx;
        double dy = gy - state.resize_start_cy;

        if (state.resize_edge & RESIZE_RIGHT)
            new_w = state.resize_start_w + (int)dx;
        if (state.resize_edge & RESIZE_BOTTOM)
            new_h = state.resize_start_h + (int)dy;
        if (state.resize_edge & RESIZE_LEFT) {
            new_w = state.resize_start_w - (int)dx;
            new_x = state.resize_start_x + (int)dx;
        }
        if (state.resize_edge & RESIZE_TOP) {
            new_h = state.resize_start_h - (int)dy;
            new_y = state.resize_start_y + (int)dy;
        }

        if (new_w < MIN_WIN_W) { new_w = MIN_WIN_W; if (state.resize_edge & RESIZE_LEFT) new_x = state.resize_start_x + state.resize_start_w - MIN_WIN_W; }
        if (new_h < MIN_WIN_H) { new_h = MIN_WIN_H; if (state.resize_edge & RESIZE_TOP) new_y = state.resize_start_y + state.resize_start_h - MIN_WIN_H; }

        glfwSetWindowPos(win, new_x, new_y);
        glfwSetWindowSize(win, new_w, new_h);
    }
}

static void mouse_button_cb(GLFWwindow *win, int button, int action, int mods) {
    (void)mods;
    if (button != GLFW_MOUSE_BUTTON_LEFT) return;

    double mx, my;
    glfwGetCursorPos(win, &mx, &my);

    if (action == GLFW_PRESS) {
        /* Check buttons first */
        HoverButton btn = hit_test_buttons(mx, my, state.win_w);
        if (btn == HOVER_CLOSE) {
            glfwSetWindowShouldClose(win, GLFW_TRUE);
            return;
        }
        if (btn == HOVER_MINIMIZE) {
            glfwIconifyWindow(win);
            return;
        }
        if (btn == HOVER_MAXIMIZE) {
            if (state.maximized) {
                glfwSetWindowPos(win, state.pre_max_x, state.pre_max_y);
                glfwSetWindowSize(win, state.pre_max_w, state.pre_max_h);
                state.maximized = 0;
            } else {
                glfwGetWindowPos(win, &state.pre_max_x, &state.pre_max_y);
                glfwGetWindowSize(win, &state.pre_max_w, &state.pre_max_h);
                GLFWmonitor *mon = glfwGetPrimaryMonitor();
                int mx_pos, my_pos, mw, mh;
                glfwGetMonitorWorkarea(mon, &mx_pos, &my_pos, &mw, &mh);
                glfwSetWindowPos(win, mx_pos, my_pos);
                glfwSetWindowSize(win, mw, mh);
                state.maximized = 1;
            }
            return;
        }

        /* Check resize edges */
        ResizeEdge edge = hit_test_resize(mx, my, state.win_w, state.win_h);
        if (edge != RESIZE_NONE) {
            state.resizing = 1;
            state.resize_edge = edge;
            state.resize_start_cx = mx;
            state.resize_start_cy = my;
            glfwGetWindowPos(win, &state.resize_start_x, &state.resize_start_y);
            glfwGetWindowSize(win, &state.resize_start_w, &state.resize_start_h);
            return;
        }

        /* Title bar drag */
        if (my <= TITLE_BAR_HEIGHT) {
            state.dragging = 1;
            state.drag_x = mx;
            state.drag_y = my;
        }
    }

    if (action == GLFW_RELEASE) {
        state.dragging = 0;
        state.resizing = 0;
    }
}

static void key_cb(GLFWwindow *win, int key, int scancode, int action, int mods) {
    (void)scancode;
    (void)mods;
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
        glfwSetWindowShouldClose(win, GLFW_TRUE);
}

/* ── Render UI ─────────────────────────────────────────────────────── */

static void render_title_bar(float w) {
    float scale = (float)state.fb_w / (float)state.win_w;

    /* Title bar background */
    draw_rect(0, 0, w, TITLE_BAR_HEIGHT * scale, TITLE_R, TITLE_G, TITLE_B);

    /* Buttons */
    float bs   = BUTTON_SIZE * scale;
    float bp   = BUTTON_PADDING * scale;
    float bsp  = BUTTON_SPACING * scale;
    float by   = (TITLE_BAR_HEIGHT * scale - bs) * 0.5f;
    float thickness = 2.0f * scale;

    /* Close (X) */
    float cx = w - bp - bs;
    float hover_close = (state.hover == HOVER_CLOSE);
    if (hover_close) draw_rect(cx - 4*scale, by - 4*scale, bs + 8*scale, bs + 8*scale, 0.75f, 0.22f, 0.22f);
    float cr = hover_close ? 1.0f : 0.55f, cg = hover_close ? 1.0f : 0.55f, cb = hover_close ? 1.0f : 0.55f;
    draw_line(cx, by, cx + bs, by + bs, thickness, cr, cg, cb);
    draw_line(cx + bs, by, cx, by + bs, thickness, cr, cg, cb);

    /* Maximize (square) */
    float mx = cx - bsp - bs;
    float hover_max = (state.hover == HOVER_MAXIMIZE);
    if (hover_max) draw_rect(mx - 4*scale, by - 4*scale, bs + 8*scale, bs + 8*scale, 0.3f, 0.3f, 0.35f);
    float mr = hover_max ? 0.9f : 0.55f, mg = hover_max ? 0.9f : 0.55f, mb = hover_max ? 0.9f : 0.55f;
    draw_rect(mx, by, bs, thickness, mr, mg, mb);
    draw_rect(mx, by + bs - thickness, bs, thickness, mr, mg, mb);
    draw_rect(mx, by, thickness, bs, mr, mg, mb);
    draw_rect(mx + bs - thickness, by, thickness, bs, mr, mg, mb);

    /* Minimize (dash) */
    float nx = mx - bsp - bs;
    float hover_min = (state.hover == HOVER_MINIMIZE);
    if (hover_min) draw_rect(nx - 4*scale, by - 4*scale, bs + 8*scale, bs + 8*scale, 0.3f, 0.3f, 0.35f);
    float nr = hover_min ? 0.9f : 0.55f, ng = hover_min ? 0.9f : 0.55f, nb = hover_min ? 0.9f : 0.55f;
    draw_rect(nx, by + bs * 0.5f - thickness * 0.5f, bs, thickness, nr, ng, nb);

    /* Subtle separator line below title bar */
    draw_rect(0, TITLE_BAR_HEIGHT * scale - 1.0f, w, 1.0f, 0.2f, 0.2f, 0.22f);
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
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);

    GLFWwindow *window = glfwCreateWindow(1000, 700, "ornstein", NULL, NULL);
    if (!window) {
        fprintf(stderr, "Failed to create GLFW window\n");
        glfwTerminate();
        return EXIT_FAILURE;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    glfwGetWindowSize(window, &state.win_w, &state.win_h);
    glfwGetFramebufferSize(window, &state.fb_w, &state.fb_h);

    glfwSetFramebufferSizeCallback(window, framebuffer_size_cb);
    glfwSetWindowSizeCallback(window, window_size_cb);
    glfwSetCursorPosCallback(window, cursor_pos_cb);
    glfwSetMouseButtonCallback(window, mouse_button_cb);
    glfwSetKeyCallback(window, key_cb);

    int version = gladLoadGL(glfwGetProcAddress);
    if (!version) {
        fprintf(stderr, "Failed to initialize GLAD\n");
        glfwTerminate();
        return EXIT_FAILURE;
    }
    printf("OpenGL %d.%d\n", GLAD_VERSION_MAJOR(version), GLAD_VERSION_MINOR(version));

    init_renderer();
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    while (!glfwWindowShouldClose(window)) {
        glViewport(0, 0, state.fb_w, state.fb_h);
        glClearColor(BG_R, BG_G, BG_B, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(shader_prog);
        glUniform2f(u_resolution, (float)state.fb_w, (float)state.fb_h);

        render_title_bar((float)state.fb_w);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glDeleteVertexArrays(1, &quad_vao);
    glDeleteBuffers(1, &quad_vbo);
    glDeleteProgram(shader_prog);
    glfwDestroyWindow(window);
    glfwTerminate();
    return EXIT_SUCCESS;
}
