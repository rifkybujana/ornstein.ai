#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include "platform.h"
#include "face.h"
#include "mood.h"
#include "llm.h"
#include "chat.h"
#include "text.h"
#include "ambient.h"
#include "stats.h"

/* ── Callbacks ─────────────────────────────────────────────────────── */

static int fb_w, fb_h, win_w, win_h;
static double mouse_x, mouse_y;
static MoodState *g_mood;
static ChatState *g_chat;

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

static void char_cb(GLFWwindow *w, unsigned int codepoint) {
    (void)w;
    if (g_chat && chat_has_focus(g_chat))
        chat_on_char(g_chat, codepoint);
}

static void key_cb(GLFWwindow *w, int key, int sc, int action, int mods) {
    (void)sc;
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        if (g_chat && chat_has_focus(g_chat))
            chat_on_key(g_chat, key, action, mods);
        else
            glfwSetWindowShouldClose(w, GLFW_TRUE);
        return;
    }
    if (key == GLFW_KEY_TAB && action == GLFW_PRESS) {
        if (g_chat) chat_toggle(g_chat);
        return;
    }
    if (g_chat && chat_has_focus(g_chat))
        chat_on_key(g_chat, key, action, mods);
}

static void mouse_button_cb(GLFWwindow *w, int button, int action, int mods) {
    (void)w; (void)mods;
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS)
        mood_on_click(g_mood);
}

static void cursor_enter_cb(GLFWwindow *w, int entered) {
    (void)w;
    mood_on_cursor_enter(g_mood, entered);
}

static void scroll_cb(GLFWwindow *w, double xoff, double yoff) {
    (void)w; (void)xoff;
    if (g_chat && chat_visible(g_chat))
        chat_on_scroll(g_chat, yoff);
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
    glfwSetMouseButtonCallback(window, mouse_button_cb);
    glfwSetCursorEnterCallback(window, cursor_enter_cb);
    glfwSetCharCallback(window, char_cb);
    glfwSetScrollCallback(window, scroll_cb);

    platform_style_window(window);

    int version = gladLoadGL(glfwGetProcAddress);
    if (!version) {
        fprintf(stderr, "Failed to initialize GLAD\n");
        glfwTerminate();
        return EXIT_FAILURE;
    }
    printf("OpenGL %d.%d\n", GLAD_VERSION_MAJOR(version), GLAD_VERSION_MINOR(version));

    /* Init modules */
    FaceState face;
    face_init(&face);
    face_render_init();

    text_init();
    ambient_init();
    stats_init();

    MoodState mood;
    mood_init(&mood);
    g_mood = &mood;

    g_chat = chat_create();
    chat_set_mood_state(&mood);

    /* Find model and server paths */
    char model_path[1024];
    const char *env_model = getenv("ORNSTEIN_MODEL");
    if (env_model) {
        snprintf(model_path, sizeof(model_path), "%s", env_model);
    } else {
        const char *home = getenv("HOME");
        snprintf(model_path, sizeof(model_path), "%s/.ornstein/model.gguf", home ? home : ".");
    }

    /* Find llama-server — look next to the ornstein binary */
    /* The build produces it at build/bin/llama-server */
    const char *server_path = "bin/llama-server";

    llm_init(model_path, server_path);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    double last_time = glfwGetTime();

    while (!glfwWindowShouldClose(window)) {
        double now = glfwGetTime();
        float dt = (float)(now - last_time);
        last_time = now;

        glViewport(0, 0, fb_w, fb_h);
        glClearColor(0.11f, 0.11f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        float scale = (float)fb_w / (float)win_w;
        float mx = (float)mouse_x * scale;
        float my = (float)mouse_y * scale;

        /* Face always centered on full screen */
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

        /* Render chat overlay or hint */
        if (g_chat && chat_visible(g_chat)) {
            chat_render(g_chat, (float)fb_w, (float)fb_h, scale);
        } else {
            text_draw("Tab to chat", (float)fb_w - 120.0f * scale, (float)fb_h - 20.0f * scale,
                      1.0f * scale, 0.3f, 0.3f, 0.35f, (float)fb_w, (float)fb_h);
        }

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
                text_draw(bars[i].label, bx, bar_y, label_sc,
                          0.5f, 0.5f, 0.55f, (float)fb_w, (float)fb_h);
                float by = bar_y + lh + 4.0f * scale;
                text_draw_rect(bx, by, bar_w, bar_h,
                               0.2f, 0.2f, 0.25f, 1.0f,
                               (float)fb_w, (float)fb_h);
                if (bars[i].value > 0.0f)
                    text_draw_rect(bx, by, bar_w * bars[i].value, bar_h,
                                   bars[i].r, bars[i].g, bars[i].b, 1.0f,
                                   (float)fb_w, (float)fb_h);
            }
        }

        /* Show download / startup status (centered, no panel) */
        {
            int pct = 0;
            float mb_done = 0, mb_total = 0;
            char err_msg[128] = "";
            int st = llm_status(&pct, &mb_done, &mb_total, err_msg, sizeof(err_msg));
            float ts = 1.0f * scale;
            float lh = text_line_height(ts);
            float center_x = (float)fb_w * 0.5f;
            float center_y = (float)fb_h * 0.65f;

            if (st == 1) {
                /* Downloading */
                int dots = ((int)(now * 2.0)) % 4;
                const char *dot_str[] = {"", ".", "..", "..."};
                char title[48];
                snprintf(title, sizeof(title), "Downloading model%s", dot_str[dots]);
                float tw = text_width(title, ts);
                text_draw(title, center_x - tw * 0.5f, center_y,
                          ts, 0.85f, 0.85f, 0.9f, (float)fb_w, (float)fb_h);

                /* Progress bar */
                float bar_w = 200.0f * scale;
                float bar_h = 8.0f * scale;
                float bar_x = center_x - bar_w * 0.5f;
                float bar_y = center_y + lh * 1.5f;
                float fill = (float)pct / 100.0f;

                text_draw_rect(bar_x, bar_y, bar_w, bar_h,
                               0.2f, 0.2f, 0.25f, 1.0f,
                               (float)fb_w, (float)fb_h);
                if (fill > 0.0f)
                    text_draw_rect(bar_x, bar_y, bar_w * fill, bar_h,
                                   0.4f, 0.7f, 1.0f, 1.0f,
                                   (float)fb_w, (float)fb_h);

                /* MB info */
                char mb_str[48];
                snprintf(mb_str, sizeof(mb_str), "%.0f / %.0f MB  %d%%", mb_done, mb_total, pct);
                float mw = text_width(mb_str, ts);
                text_draw(mb_str, center_x - mw * 0.5f, bar_y + bar_h + 6.0f * scale,
                          ts, 0.4f, 0.4f, 0.5f, (float)fb_w, (float)fb_h);

            } else if (st == 2 || st == 3) {
                /* Starting server */
                int dots = ((int)(now * 2.0)) % 4;
                const char *dot_str[] = {"", ".", "..", "..."};
                char title[48];
                snprintf(title, sizeof(title), "Starting server%s", dot_str[dots]);
                float tw = text_width(title, ts);
                text_draw(title, center_x - tw * 0.5f, center_y,
                          ts, 0.85f, 0.85f, 0.9f, (float)fb_w, (float)fb_h);

            } else if (st == -1) {
                /* Error */
                const char *etitle = "Download failed";
                float ew = text_width(etitle, ts);
                text_draw(etitle, center_x - ew * 0.5f, center_y,
                          ts, 0.9f, 0.35f, 0.35f, (float)fb_w, (float)fb_h);
                if (err_msg[0]) {
                    float mw = text_width(err_msg, ts);
                    text_draw(err_msg, center_x - mw * 0.5f, center_y + lh * 1.5f,
                              ts, 0.5f, 0.4f, 0.4f, (float)fb_w, (float)fb_h);
                }
            }
        }

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    llm_shutdown();
    stats_shutdown();
    ambient_cleanup();
    chat_destroy(g_chat);
    text_cleanup();
    face_render_cleanup();
    glfwDestroyWindow(window);
    glfwTerminate();
    return EXIT_SUCCESS;
}
