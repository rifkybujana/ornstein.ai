#include <stdio.h>
#include <stdlib.h>

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include "platform.h"
#include "face.h"
#include "mood.h"
#include "llm.h"
#include "chat.h"
#include "text.h"

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

    g_chat = chat_create();

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

    if (llm_init(model_path, server_path) == 0) {
        printf("LLM server started\n");
    } else {
        printf("LLM server failed to start (continuing without chat AI)\n");
    }

    MoodState mood;
    mood_init(&mood);
    g_mood = &mood;

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

        /* Layout: if chat visible, face on left, chat on right */
        float chat_w = 300.0f * scale;
        float face_area_w = (g_chat && chat_visible(g_chat)) ? (float)fb_w - chat_w : (float)fb_w;
        float cx = face_area_w * 0.5f;
        float cy = (float)fb_h * 0.5f;

        /* Update mood → face → chat → LLM */
        Emotion target = mood_update(&mood, mouse_x, mouse_y, dt);
        face.target_emotion = target;
        FaceParams params = face_update(&face, dt);
        if (g_chat) chat_update(g_chat, target, dt);
        llm_poll();

        /* Render face */
        face_render(&params, cx, cy, mx, my, scale, (float)fb_w, (float)fb_h);

        /* Render chat panel */
        if (g_chat && chat_visible(g_chat)) {
            chat_render(g_chat, face_area_w, chat_w, (float)fb_h, scale, (float)fb_w);
        }

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    llm_shutdown();
    chat_destroy(g_chat);
    text_cleanup();
    face_render_cleanup();
    glfwDestroyWindow(window);
    glfwTerminate();
    return EXIT_SUCCESS;
}
