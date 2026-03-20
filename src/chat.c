#include "chat.h"
#include "text.h"
#include "llm.h"
#include "sentiment.h"
#include "mood.h"
#include "stats.h"

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* External mood state for sentiment overrides */
static MoodState *chat_mood_state = NULL;

void chat_set_mood_state(MoodState *ms) {
    chat_mood_state = ms;
}

/* ══════════════════════════════════════════════════════════════════════
   Data structures
   ══════════════════════════════════════════════════════════════════════ */

#define CHAT_MAX_MESSAGES 128
#define CHAT_MAX_MSG_LEN 4096
#define CHAT_INPUT_MAX 256

typedef struct {
    char text[CHAT_MAX_MSG_LEN];
    int is_user;     /* 1 = user, 0 = pet */
    int is_thinking; /* 1 = reasoning/thinking */
    float age;      /* seconds since message appeared */
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
    char pending_thinking[CHAT_MAX_MSG_LEN];  /* accumulates thinking tokens */
    int thinking_len;
    char pending_response[CHAT_MAX_MSG_LEN];  /* accumulates response tokens */
    int pending_len;
    Emotion current_mood;  /* cached for LLM context */
    float cursor_blink;    /* timer for blinking cursor */
    float anim_time;   /* monotonic timer for animations */
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
        "sleepy", "bored", "curious", "sad", "thinking"
    };
    if (e >= 0 && e < EMOTION_COUNT) return names[e];
    return "neutral";
}

static void mood_glow_color(Emotion e, float *r, float *g, float *b) {
    static const float colors[][3] = {
        {0.40f, 0.40f, 0.45f}, /* neutral */
        {0.90f, 0.80f, 0.30f}, /* happy */
        {1.00f, 0.60f, 0.20f}, /* excited */
        {0.90f, 0.90f, 1.00f}, /* surprised */
        {0.20f, 0.30f, 0.70f}, /* sleepy */
        {0.30f, 0.30f, 0.35f}, /* bored */
        {0.20f, 0.70f, 0.70f}, /* curious */
        {0.30f, 0.40f, 0.80f}, /* sad */
        {0.50f, 0.30f, 0.80f}, /* thinking */
    };
    int idx = (e >= 0 && e < EMOTION_COUNT) ? e : 0;
    *r = colors[idx][0];
    *g = colors[idx][1];
    *b = colors[idx][2];
}

/* ══════════════════════════════════════════════════════════════════════
   LLM token callback
   ══════════════════════════════════════════════════════════════════════ */

static void on_token(const char *token, int thinking, int done, void *userdata) {
    ChatState *cs = (ChatState *)userdata;
    if (!done) {
        int len = (int)strlen(token);
        if (thinking) {
            /* Rolling thinking buffer — keep latest text when full */
            if (cs->thinking_len + len >= CHAT_MAX_MSG_LEN - 1) {
                int keep = CHAT_MAX_MSG_LEN / 2;
                int start = cs->thinking_len - keep;
                if (start < 0) start = 0;
                memmove(cs->pending_thinking, cs->pending_thinking + start, cs->thinking_len - start);
                cs->thinking_len -= start;
                cs->pending_thinking[cs->thinking_len] = '\0';
            }
            if (cs->thinking_len + len < CHAT_MAX_MSG_LEN - 1) {
                memcpy(cs->pending_thinking + cs->thinking_len, token, len);
                cs->thinking_len += len;
                cs->pending_thinking[cs->thinking_len] = '\0';
            }
        } else {
            /* Append to response buffer */
            if (cs->pending_len + len < CHAT_MAX_MSG_LEN - 1) {
                memcpy(cs->pending_response + cs->pending_len, token, len);
                cs->pending_len += len;
                cs->pending_response[cs->pending_len] = '\0';
            }
        }
    } else {
        /* Generation complete — commit thinking (last portion) then response */
        if (cs->thinking_len > 0 && cs->msg_count < CHAT_MAX_MESSAGES) {
            ChatMessage *msg = &cs->messages[cs->msg_count++];
            int copy_len = cs->thinking_len;
            const char *src = cs->pending_thinking;
            if (copy_len >= CHAT_MAX_MSG_LEN) {
                src += copy_len - (CHAT_MAX_MSG_LEN - 1);
                copy_len = CHAT_MAX_MSG_LEN - 1;
            }
            memcpy(msg->text, src, copy_len);
            msg->text[copy_len] = '\0';
            msg->is_user = 0;
            msg->is_thinking = 1;
            msg->age = 0.0f;
        }
        if (cs->pending_len > 0 && cs->msg_count < CHAT_MAX_MESSAGES) {
            /* Parse mood tag from response */
            Emotion detected;
            int tag_end = 0;
            const char *response_text = cs->pending_response;
            if (sentiment_parse(response_text, &detected, &tag_end)) {
                response_text += tag_end; /* strip tag from display */
                if (chat_mood_state) {
                    mood_set_sentiment_override(chat_mood_state, detected, 5.0f);
                }
                stats_on_mood(detected);
            }

            ChatMessage *msg = &cs->messages[cs->msg_count++];
            int text_len = (int)strlen(response_text);
            if (text_len >= CHAT_MAX_MSG_LEN) text_len = CHAT_MAX_MSG_LEN - 1;
            memcpy(msg->text, response_text, text_len);
            msg->text[text_len] = '\0';
            msg->is_user = 0;
            msg->is_thinking = 0;
            msg->age = 0.0f;
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
    cs->thinking_len = 0;
    cs->pending_thinking[0] = '\0';
    cs->pending_len = 0;
    cs->pending_response[0] = '\0';
    cs->current_mood = EMOTION_NEUTRAL;
    cs->cursor_blink = 0.0f;
    cs->anim_time = 0.0f;
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
                "LLM not available.\n\n"
                "Model download may have\n"
                "failed. Check the terminal\n"
                "for details.");
            msg->is_user = 0;
            msg->age = 0.0f;
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
                    msg->age = 0.0f;
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
                msg->age = 0.0f;
                stats_on_message();
            }

            /* Clear pending buffers, start generation */
            cs->pending_thinking[0] = '\0';
            cs->thinking_len = 0;
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
    cs->anim_time += dt;

    for (int i = 0; i < cs->msg_count; i++) {
        cs->messages[i].age += dt;
    }

    /* Poll LLM for incoming tokens */
    if (cs->generating) {
        llm_poll();
    }
}

/* ── Rendering ──────────────────────────────────────────────────────── */

void chat_render(ChatState *cs, float fb_w, float fb_h, float scale) {
    if (!cs || !cs->visible) return;

    /* Layout constants */
    float padding = 16.0f * scale;
    float msg_spacing = 8.0f * scale;
    float input_box_h = 36.0f * scale;
    float input_pad = 10.0f * scale;
    float text_scale = 1.5f * scale;
    float line_h = text_line_height(text_scale);

    /* Input box: centered at bottom */
    float input_max_w = 500.0f * scale;
    float input_box_w = fb_w * 0.5f;
    if (input_box_w > input_max_w) input_box_w = input_max_w;
    if (input_box_w < 200.0f * scale) input_box_w = 200.0f * scale;
    float input_box_x = (fb_w - input_box_w) * 0.5f;
    float input_box_y = fb_h - input_box_h - padding;

    /* Input box background */
    c_draw_rect(input_box_x, input_box_y, input_box_w, input_box_h,
                0.15f, 0.15f, 0.16f, 0.9f,
                fb_w, fb_h);

    /* Mood-reactive focus ring */
    if (cs->focused) {
        float mr, mg, mb;
        mood_glow_color(cs->current_mood, &mr, &mg, &mb);
        float border = 1.0f * scale;
        c_draw_rect(input_box_x, input_box_y, input_box_w, border,
                    mr, mg, mb, 0.4f, fb_w, fb_h);
        c_draw_rect(input_box_x, input_box_y + input_box_h - border,
                    input_box_w, border,
                    mr, mg, mb, 0.4f, fb_w, fb_h);
        c_draw_rect(input_box_x, input_box_y, border, input_box_h,
                    mr, mg, mb, 0.4f, fb_w, fb_h);
        c_draw_rect(input_box_x + input_box_w - border, input_box_y,
                    border, input_box_h,
                    mr, mg, mb, 0.4f, fb_w, fb_h);
    }

    /* Input text */
    float input_text_x = input_box_x + input_pad;
    float input_text_y = input_box_y + (input_box_h - line_h) * 0.5f;

    if (cs->input_len > 0) {
        text_draw(cs->input, input_text_x, input_text_y, text_scale,
                  1.0f, 1.0f, 1.0f, fb_w, fb_h);
    } else {
        text_draw("Type a message...", input_text_x, input_text_y, text_scale,
                  0.4f, 0.4f, 0.45f, fb_w, fb_h);
    }

    /* Blinking cursor */
    if (cs->focused && cs->cursor_blink < 0.5f) {
        float cursor_x = input_text_x + text_width(cs->input, text_scale);
        float cursor_w = 2.0f * scale;
        c_draw_rect(cursor_x, input_text_y, cursor_w, line_h,
                    1.0f, 1.0f, 1.0f, 1.0f,
                    fb_w, fb_h);
    }

    /* Messages area: centered above input, same width */
    float msgs_x = input_box_x;
    float msgs_w = input_box_w;
    float msgs_bottom = input_box_y - msg_spacing;
    float msgs_top = padding;

    /* Scissor clip messages */
    glEnable(GL_SCISSOR_TEST);
    glScissor((int)msgs_x, (int)(fb_h - msgs_bottom),
              (int)msgs_w, (int)(msgs_bottom - msgs_top));

    /* Render messages from bottom up */
    float y = msgs_bottom;
    y += cs->scroll_offset;

    /* Bubble layout constants */
    float bubble_pad_x = 8.0f * scale;
    float bubble_pad_y = 6.0f * scale;
    float max_bubble_w = msgs_w * 0.7f;

    /* Pending streaming content */
    if (cs->generating && (cs->pending_len > 0 || cs->thinking_len > 0)) {
        if (cs->pending_len > 0) {
            float content_w = max_bubble_w - bubble_pad_x * 2;
            float h = text_draw_wrapped(cs->pending_response,
                                        msgs_x, fb_h * 2, content_w, text_scale,
                                        0, 0, 0, fb_w, fb_h);
            float bubble_h = h + bubble_pad_y * 2;
            y -= bubble_h;
            float bubble_x = msgs_x;
            c_draw_rect(bubble_x, y, max_bubble_w, bubble_h,
                        0.18f, 0.18f, 0.20f, 0.85f,
                        fb_w, fb_h);
            text_draw_wrapped(cs->pending_response,
                              bubble_x + bubble_pad_x, y + bubble_pad_y,
                              content_w, text_scale,
                              0.9f, 0.9f, 0.85f, fb_w, fb_h);
            y -= msg_spacing;
        }
        if (cs->thinking_len > 0) {
            const char *think_text = cs->pending_thinking;
            if (cs->thinking_len > 200) {
                think_text = cs->pending_thinking + cs->thinking_len - 200;
                const char *nl = strchr(think_text, '\n');
                if (nl) think_text = nl + 1;
            }
            float content_w = max_bubble_w - bubble_pad_x * 2;
            float h = text_draw_wrapped(think_text,
                                        msgs_x, fb_h * 2, content_w, text_scale,
                                        0, 0, 0, fb_w, fb_h);
            float bubble_h = h + bubble_pad_y * 2;
            y -= bubble_h;
            float bubble_x = msgs_x;
            c_draw_rect(bubble_x, y, max_bubble_w, bubble_h,
                        0.15f, 0.15f, 0.17f, 0.70f,
                        fb_w, fb_h);
            text_draw_wrapped(think_text,
                              bubble_x + bubble_pad_x, y + bubble_pad_y,
                              content_w, text_scale,
                              0.45f, 0.45f, 0.5f, fb_w, fb_h);
            y -= msg_spacing;
        }
    } else if (cs->generating) {
        /* Pulsing dots typing indicator */
        float dot_r = 3.0f * scale;
        float dot_gap = 12.0f * scale;
        float dot_base_y = y - line_h * 0.5f;
        float dot_base_x = msgs_x + 16.0f * scale;
        for (int d = 0; d < 3; d++) {
            float dot_scale = 0.5f + 0.5f * sinf(cs->anim_time * 4.0f + (float)d * 1.0f);
            float dr = dot_r * dot_scale;
            float dx = dot_base_x + (float)d * dot_gap;
            c_draw_rect(dx - dr, dot_base_y - dr, dr * 2, dr * 2,
                        0.5f, 0.5f, 0.55f, 0.8f,
                        fb_w, fb_h);
        }
        y -= line_h + msg_spacing;
    }

    /* Historical messages (newest at bottom) */
    for (int i = cs->msg_count - 1; i >= 0; i--) {
        ChatMessage *msg = &cs->messages[i];
        float content_w = max_bubble_w - bubble_pad_x * 2;
        float h = text_draw_wrapped(msg->text,
                                    msgs_x, fb_h * 2, content_w, text_scale,
                                    0, 0, 0, fb_w, fb_h);
        float bubble_h = h + bubble_pad_y * 2;
        y -= bubble_h;

        if (y < msgs_bottom && y + bubble_h > msgs_top) {
            /* Entry animation */
            float anim_alpha = 1.0f;
            float anim_x_off = 0.0f;
            if (msg->age < 0.3f) {
                float t = msg->age / 0.3f;
                float ease = 1.0f - (1.0f - t) * (1.0f - t);
                anim_x_off = (1.0f - ease) * 30.0f * scale;
                anim_alpha = t < 0.67f ? t / 0.67f : 1.0f;
            }

            float bubble_x;
            if (msg->is_user) {
                bubble_x = msgs_x + msgs_w - max_bubble_w + anim_x_off;
            } else {
                bubble_x = msgs_x - anim_x_off;
            }

            /* Bubble background */
            float bg_r, bg_g, bg_b, bg_a;
            if (msg->is_user) {
                bg_r = 0.20f; bg_g = 0.35f; bg_b = 0.60f; bg_a = 0.85f * anim_alpha;
            } else if (msg->is_thinking) {
                bg_r = 0.15f; bg_g = 0.15f; bg_b = 0.17f; bg_a = 0.70f * anim_alpha;
            } else {
                bg_r = 0.18f; bg_g = 0.18f; bg_b = 0.20f; bg_a = 0.85f * anim_alpha;
            }
            c_draw_rect(bubble_x, y, max_bubble_w, bubble_h,
                        bg_r, bg_g, bg_b, bg_a,
                        fb_w, fb_h);

            /* Text */
            float r, g, b;
            if (msg->is_user) {
                r = 0.9f; g = 0.9f; b = 1.0f;
            } else if (msg->is_thinking) {
                r = 0.45f; g = 0.45f; b = 0.5f;
            } else {
                r = 0.9f; g = 0.9f; b = 0.85f;
            }
            text_draw_wrapped(msg->text,
                              bubble_x + bubble_pad_x, y + bubble_pad_y,
                              content_w, text_scale,
                              r * anim_alpha, g * anim_alpha, b * anim_alpha,
                              fb_w, fb_h);
        }

        y -= msg_spacing;
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
