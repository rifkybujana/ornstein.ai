#include "chat.h"
#include "text.h"
#include "llm.h"

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ══════════════════════════════════════════════════════════════════════
   Data structures
   ══════════════════════════════════════════════════════════════════════ */

#define CHAT_MAX_MESSAGES 128
#define CHAT_MAX_MSG_LEN 512
#define CHAT_INPUT_MAX 256

typedef struct {
    char text[CHAT_MAX_MSG_LEN];
    int is_user;  /* 1 = user, 0 = pet */
} ChatMessage;

struct ChatState {
    int visible;
    int focused;
    ChatMessage messages[CHAT_MAX_MESSAGES];
    int msg_count;
    char input[CHAT_INPUT_MAX];
    int input_len;
    float scroll_offset;
    int generating;  /* 1 if LLM is currently generating */
    char pending_response[CHAT_MAX_MSG_LEN];  /* accumulates streamed tokens */
    int pending_len;
    Emotion current_mood;  /* cached for LLM context */
    float cursor_blink;    /* timer for blinking cursor */
};

/* ══════════════════════════════════════════════════════════════════════
   Rect-drawing shader (follows face.c pattern)
   ══════════════════════════════════════════════════════════════════════ */

static GLuint c_rect_vao, c_rect_vbo, c_rect_prog;
static GLint  c_u_rect_pos, c_u_rect_size, c_u_resolution, c_u_color, c_u_alpha;

static int c_gl_inited = 0;

static const char *c_rect_vert_src =
    "#version 410 core\n"
    "layout (location = 0) in vec2 aPos;\n"
    "uniform vec2 uRectPos;\n"
    "uniform vec2 uRectSize;\n"
    "uniform vec2 uResolution;\n"
    "void main() {\n"
    "    vec2 px = uRectPos + aPos * uRectSize;\n"
    "    vec2 ndc = (px / uResolution) * 2.0 - 1.0;\n"
    "    ndc.y = -ndc.y;\n"
    "    gl_Position = vec4(ndc, 0.0, 1.0);\n"
    "}\n";

static const char *c_rect_frag_src =
    "#version 410 core\n"
    "uniform vec3 uColor;\n"
    "uniform float uAlpha;\n"
    "out vec4 FragColor;\n"
    "void main() {\n"
    "    FragColor = vec4(uColor, uAlpha);\n"
    "}\n";

static GLuint c_compile(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    int ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        fprintf(stderr, "chat shader compile: %s\n", log);
    }
    return s;
}

static GLuint c_link(GLuint vert, GLuint frag) {
    GLuint p = glCreateProgram();
    glAttachShader(p, vert);
    glAttachShader(p, frag);
    glLinkProgram(p);
    int ok;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(p, sizeof(log), NULL, log);
        fprintf(stderr, "chat shader link: %s\n", log);
    }
    return p;
}

static void c_init_gl(void) {
    if (c_gl_inited) return;
    c_gl_inited = 1;

    /* Unit quad geometry */
    float verts[] = {
        0.0f, 0.0f,
        1.0f, 0.0f,
        1.0f, 1.0f,
        0.0f, 0.0f,
        1.0f, 1.0f,
        0.0f, 1.0f,
    };
    glGenVertexArrays(1, &c_rect_vao);
    glGenBuffers(1, &c_rect_vbo);
    glBindVertexArray(c_rect_vao);
    glBindBuffer(GL_ARRAY_BUFFER, c_rect_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    /* Shader program */
    GLuint vert = c_compile(GL_VERTEX_SHADER, c_rect_vert_src);
    GLuint frag = c_compile(GL_FRAGMENT_SHADER, c_rect_frag_src);
    c_rect_prog = c_link(vert, frag);
    glDeleteShader(vert);
    glDeleteShader(frag);

    c_u_rect_pos   = glGetUniformLocation(c_rect_prog, "uRectPos");
    c_u_rect_size  = glGetUniformLocation(c_rect_prog, "uRectSize");
    c_u_resolution = glGetUniformLocation(c_rect_prog, "uResolution");
    c_u_color      = glGetUniformLocation(c_rect_prog, "uColor");
    c_u_alpha      = glGetUniformLocation(c_rect_prog, "uAlpha");
}

static void c_draw_rect(float x, float y, float w, float h,
                         float cr, float cg, float cb, float alpha,
                         float fb_w, float fb_h) {
    glUseProgram(c_rect_prog);
    glUniform2f(c_u_rect_pos, x, y);
    glUniform2f(c_u_rect_size, w, h);
    glUniform2f(c_u_resolution, fb_w, fb_h);
    glUniform3f(c_u_color, cr, cg, cb);
    glUniform1f(c_u_alpha, alpha);
    glBindVertexArray(c_rect_vao);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

/* ══════════════════════════════════════════════════════════════════════
   Emotion name helper
   ══════════════════════════════════════════════════════════════════════ */

static const char *emotion_name(Emotion e) {
    static const char *names[] = {
        "neutral", "happy", "excited", "surprised",
        "sleepy", "bored", "curious", "sad"
    };
    if (e >= 0 && e < EMOTION_COUNT) return names[e];
    return "neutral";
}

/* ══════════════════════════════════════════════════════════════════════
   LLM token callback
   ══════════════════════════════════════════════════════════════════════ */

static void on_token(const char *token, int done, void *userdata) {
    ChatState *cs = (ChatState *)userdata;
    if (!done) {
        /* Append token to pending response */
        int len = (int)strlen(token);
        if (cs->pending_len + len < CHAT_MAX_MSG_LEN - 1) {
            memcpy(cs->pending_response + cs->pending_len, token, len);
            cs->pending_len += len;
            cs->pending_response[cs->pending_len] = '\0';
        }
    } else {
        /* Generation complete — move pending to messages */
        if (cs->pending_len > 0 && cs->msg_count < CHAT_MAX_MESSAGES) {
            ChatMessage *msg = &cs->messages[cs->msg_count++];
            memcpy(msg->text, cs->pending_response, cs->pending_len + 1);
            msg->is_user = 0;
        }
        cs->generating = 0;
    }
}

/* ══════════════════════════════════════════════════════════════════════
   Public API
   ══════════════════════════════════════════════════════════════════════ */

ChatState *chat_create(void) {
    ChatState *cs = (ChatState *)calloc(1, sizeof(ChatState));
    if (!cs) return NULL;
    cs->visible = 0;
    cs->focused = 0;
    cs->msg_count = 0;
    cs->input_len = 0;
    cs->input[0] = '\0';
    cs->scroll_offset = 0.0f;
    cs->generating = 0;
    cs->pending_len = 0;
    cs->pending_response[0] = '\0';
    cs->current_mood = EMOTION_NEUTRAL;
    cs->cursor_blink = 0.0f;
    c_init_gl();
    return cs;
}

int chat_toggle(ChatState *cs) {
    if (!cs) return 0;
    cs->visible = !cs->visible;
    if (cs->visible) {
        cs->focused = 1;
        /* Show help on first open if LLM not ready */
        if (cs->msg_count == 0 && !llm_ready()) {
            ChatMessage *msg = &cs->messages[cs->msg_count++];
            snprintf(msg->text, CHAT_MAX_MSG_LEN,
                "No model found.\n\n"
                "Set ORNSTEIN_MODEL=/path/to/model.gguf\n"
                "or place a GGUF file at\n"
                "~/.ornstein/model.gguf\n\n"
                "Recommended: TinyLlama-1.1B\n"
                "or SmolLM-1.7B");
            msg->is_user = 0;
        }
    } else {
        cs->focused = 0;
    }
    return cs->visible;
}

int chat_visible(ChatState *cs) {
    if (!cs) return 0;
    return cs->visible;
}

int chat_has_focus(ChatState *cs) {
    if (!cs) return 0;
    return cs->visible && cs->focused;
}

/* ── Input handling ─────────────────────────────────────────────────── */

void chat_on_char(ChatState *cs, unsigned int codepoint) {
    if (!cs || !cs->focused) return;
    /* Only accept printable ASCII */
    if (codepoint >= 32 && codepoint < 127) {
        if (cs->input_len < CHAT_INPUT_MAX - 1) {
            cs->input[cs->input_len++] = (char)codepoint;
            cs->input[cs->input_len] = '\0';
        }
    }
}

void chat_on_key(ChatState *cs, int key, int action, int mods) {
    if (!cs || !cs->focused) return;
    (void)mods;

    if (key == GLFW_KEY_ENTER && (action == GLFW_PRESS || action == GLFW_REPEAT)) {
        /* Submit message */
        if (cs->input_len > 0 && !cs->generating) {
            /* Check if LLM is available */
            if (!llm_ready()) {
                if (cs->msg_count < CHAT_MAX_MESSAGES) {
                    ChatMessage *msg = &cs->messages[cs->msg_count++];
                    snprintf(msg->text, CHAT_MAX_MSG_LEN,
                             "LLM not available. Check model path.");
                    msg->is_user = 0;
                }
                cs->input[0] = '\0';
                cs->input_len = 0;
                return;
            }

            /* Add user message */
            if (cs->msg_count < CHAT_MAX_MESSAGES) {
                ChatMessage *msg = &cs->messages[cs->msg_count++];
                memcpy(msg->text, cs->input, cs->input_len + 1);
                msg->is_user = 1;
            }

            /* Clear pending response, start generation */
            cs->pending_response[0] = '\0';
            cs->pending_len = 0;
            cs->generating = 1;

            /* Send to LLM */
            llm_send(emotion_name(cs->current_mood), cs->input,
                     on_token, cs);

            /* Clear input */
            cs->input[0] = '\0';
            cs->input_len = 0;

            /* Auto-scroll to bottom */
            cs->scroll_offset = 0.0f;
        }
    } else if (key == GLFW_KEY_BACKSPACE && (action == GLFW_PRESS || action == GLFW_REPEAT)) {
        if (cs->input_len > 0) {
            cs->input_len--;
            cs->input[cs->input_len] = '\0';
        }
    } else if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        cs->focused = 0;
    }
}

void chat_on_scroll(ChatState *cs, double yoffset) {
    if (!cs || !cs->visible) return;
    cs->scroll_offset += (float)yoffset * 20.0f;
    if (cs->scroll_offset < 0.0f) cs->scroll_offset = 0.0f;
}

/* ── Update ─────────────────────────────────────────────────────────── */

void chat_update(ChatState *cs, Emotion mood, float dt) {
    if (!cs) return;
    cs->current_mood = mood;
    cs->cursor_blink += dt;
    if (cs->cursor_blink > 1.0f) cs->cursor_blink -= 1.0f;

    /* Poll LLM for incoming tokens */
    if (cs->generating) {
        llm_poll();
    }
}

/* ── Rendering ──────────────────────────────────────────────────────── */

void chat_render(ChatState *cs, float panel_x, float panel_w,
                 float fb_h, float scale, float fb_w) {
    if (!cs || !cs->visible) return;

    /* Layout constants (logical pixels, scaled to fb pixels) */
    float padding = 12.0f * scale;
    float msg_spacing = 8.0f * scale;
    float input_box_h = 36.0f * scale;
    float input_pad = 8.0f * scale;
    float text_scale = 1.5f * scale;
    float line_h = text_line_height(text_scale);

    /* 1. Panel background */
    c_draw_rect(panel_x, 0.0f, panel_w, fb_h,
                0.08f, 0.08f, 0.09f, 0.95f,
                fb_w, fb_h);

    /* 2. Input box at bottom */
    float input_box_y = fb_h - input_box_h - padding;
    float input_box_x = panel_x + padding;
    float input_box_w = panel_w - padding * 2.0f;

    /* Separator line above input */
    float sep_h = 1.0f * scale;
    c_draw_rect(panel_x + padding, input_box_y - msg_spacing,
                panel_w - padding * 2.0f, sep_h,
                0.25f, 0.25f, 0.27f, 1.0f,
                fb_w, fb_h);

    /* Input box background */
    c_draw_rect(input_box_x, input_box_y, input_box_w, input_box_h,
                0.15f, 0.15f, 0.16f, 1.0f,
                fb_w, fb_h);

    /* Input text */
    float input_text_x = input_box_x + input_pad;
    float input_text_y = input_box_y + (input_box_h - line_h) * 0.5f;

    if (cs->input_len > 0) {
        text_draw(cs->input, input_text_x, input_text_y, text_scale,
                  1.0f, 1.0f, 1.0f, fb_w, fb_h);
    }

    /* Blinking cursor */
    if (cs->focused && cs->cursor_blink < 0.5f) {
        float cursor_x = input_text_x + text_width(cs->input, text_scale);
        float cursor_w = 2.0f * scale;
        c_draw_rect(cursor_x, input_text_y, cursor_w, line_h,
                    1.0f, 1.0f, 1.0f, 1.0f,
                    fb_w, fb_h);
    }

    /* 3. Messages area (above separator) */
    float msgs_top = padding;
    float msgs_bottom = input_box_y - msg_spacing - sep_h - msg_spacing;
    float msgs_x = panel_x + padding;
    float msgs_w = panel_w - padding * 2.0f;

    /* Enable scissor test to clip messages to the message area */
    glEnable(GL_SCISSOR_TEST);
    glScissor((int)panel_x, (int)(fb_h - msgs_bottom),
              (int)panel_w, (int)(msgs_bottom - msgs_top));

    /* Render messages from bottom up, accounting for scroll */
    float y = msgs_bottom;
    y += cs->scroll_offset;

    /* First, render the pending (streaming) response if generating */
    if (cs->generating && cs->pending_len > 0) {
        float h = text_draw_wrapped(cs->pending_response,
                                    msgs_x, 0, msgs_w, text_scale,
                                    0, 0, 0, fb_w, fb_h);
        /* text_draw_wrapped returns height but we called it with y=0
           just to measure — we need to call it again at the right position */
        y -= h;
        /* Draw "..." indicator if still generating */
        text_draw_wrapped(cs->pending_response,
                          msgs_x, y, msgs_w, text_scale,
                          0.9f, 0.9f, 0.85f, fb_w, fb_h);
        y -= msg_spacing;
    } else if (cs->generating) {
        /* Show typing indicator */
        text_draw("...", msgs_x, y - line_h, text_scale,
                  0.5f, 0.5f, 0.5f, fb_w, fb_h);
        y -= line_h + msg_spacing;
    }

    /* Render messages in reverse order (newest at bottom) */
    for (int i = cs->msg_count - 1; i >= 0; i--) {
        ChatMessage *msg = &cs->messages[i];

        /* Measure height of this message */
        float h = text_draw_wrapped(msg->text,
                                    msgs_x, 0, msgs_w, text_scale,
                                    0, 0, 0, fb_w, fb_h);
        y -= h;

        /* Only draw if visible */
        if (y < msgs_bottom && y + h > msgs_top) {
            float r, g, b;
            if (msg->is_user) {
                r = 0.7f; g = 0.8f; b = 1.0f;
            } else {
                r = 0.9f; g = 0.9f; b = 0.85f;
            }
            text_draw_wrapped(msg->text,
                              msgs_x, y, msgs_w, text_scale,
                              r, g, b, fb_w, fb_h);
        }

        y -= msg_spacing;

        /* Stop if we've scrolled past the top */
        if (y + cs->scroll_offset < msgs_top - fb_h) break;
    }

    glDisable(GL_SCISSOR_TEST);
}

/* ── Cleanup ────────────────────────────────────────────────────────── */

void chat_destroy(ChatState *cs) {
    if (!cs) return;

    if (c_gl_inited) {
        glDeleteVertexArrays(1, &c_rect_vao);
        glDeleteBuffers(1, &c_rect_vbo);
        glDeleteProgram(c_rect_prog);
        c_gl_inited = 0;
    }

    free(cs);
}
