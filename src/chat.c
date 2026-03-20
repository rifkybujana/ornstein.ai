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

#include "gl_util.h"
#include "mood_colors.h"

/* External mood state for sentiment overrides */
static MoodState *chat_mood_state = NULL;

void chat_set_mood_state(MoodState *ms) {
    chat_mood_state = ms;
}

/* ══════════════════════════════════════════════════════════════════════
   Data structures
   ══════════════════════════════════════════════════════════════════════ */

#define CHAT_MAX_MESSAGES 128
#define CHAT_MAX_MSG_LEN 65536
#define CHAT_INPUT_MAX 256

typedef struct {
    char text[CHAT_MAX_MSG_LEN];
    int is_user;     /* 1 = user, 0 = pet */
    int is_thinking; /* 1 = reasoning/thinking */
    float age;       /* seconds since message appeared */
    float fade;      /* animated fade alpha [0..1], 1 = fully visible */
    float cached_md_height; /* cached markdown height, -1 = needs recompute */
    float cached_md_width;  /* the max_width used for the cached height */
    int   has_markdown;     /* 1 if text contains *, ` markers; -1 = unchecked */
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
    GLuint vert = gl_compile_shader(GL_VERTEX_SHADER, c_rect_vert_src, "chat");
    GLuint frag = gl_compile_shader(GL_FRAGMENT_SHADER, c_rect_frag_src, "chat");
    c_rect_prog = gl_link_program(vert, frag, "chat");
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
   Markdown rendering (for pet response bubbles)
   ══════════════════════════════════════════════════════════════════════ */

typedef enum {
    MD_NORMAL,
    MD_BOLD,
    MD_ITALIC,
    MD_CODE,
    MD_CODE_BLOCK,
} MdStyle;

#define MD_MAX_SEGMENTS 1024

typedef struct {
    const char *start;
    int         len;
    MdStyle     style;
} MdSegment;

static int md_parse(const char *text, MdSegment *segs, int max_segs) {
    int count = 0;
    const char *p = text;
    int in_code_block = 0;

    while (*p && count < max_segs) {
        /* Code block: ``` */
        if (p[0] == '`' && p[1] == '`' && p[2] == '`') {
            if (!in_code_block) {
                p += 3;
                /* Skip optional language tag until newline */
                while (*p && *p != '\n') p++;
                if (*p == '\n') p++;
                in_code_block = 1;
                const char *block_start = p;
                /* Find closing ``` */
                const char *end = strstr(p, "```");
                int blen;
                if (end) {
                    blen = (int)(end - block_start);
                    p = end + 3;
                } else {
                    blen = (int)strlen(block_start);
                    p = block_start + blen;
                }
                in_code_block = 0;
                if (blen > 0) {
                    /* Strip trailing newline */
                    if (blen > 0 && block_start[blen - 1] == '\n') blen--;
                    segs[count].start = block_start;
                    segs[count].len = blen;
                    segs[count].style = MD_CODE_BLOCK;
                    count++;
                }
                continue;
            }
        }

        /* Bold: **text** */
        if (p[0] == '*' && p[1] == '*') {
            const char *start = p + 2;
            const char *end = strstr(start, "**");
            if (end && end > start) {
                segs[count].start = start;
                segs[count].len = (int)(end - start);
                segs[count].style = MD_BOLD;
                count++;
                p = end + 2;
                continue;
            }
        }

        /* Inline code: `text` */
        if (p[0] == '`') {
            const char *start = p + 1;
            const char *end = strchr(start, '`');
            if (end && end > start) {
                segs[count].start = start;
                segs[count].len = (int)(end - start);
                segs[count].style = MD_CODE;
                count++;
                p = end + 1;
                continue;
            }
        }

        /* Italic: *text* (single star, not **) */
        if (p[0] == '*' && p[1] != '*') {
            const char *start = p + 1;
            const char *end = strchr(start, '*');
            if (end && end > start) {
                segs[count].start = start;
                segs[count].len = (int)(end - start);
                segs[count].style = MD_ITALIC;
                count++;
                p = end + 1;
                continue;
            }
        }

        /* Normal text: consume until next marker or end.
           If we're stuck on an unmatched marker, consume it as normal text. */
        const char *start = p;
        if (*p == '*' || *p == '`') p++; /* eat unmatched marker */
        while (*p) {
            if (p[0] == '*' || p[0] == '`') break;
            p++;
        }
        if (p > start) {
            segs[count].start = start;
            segs[count].len = (int)(p - start);
            segs[count].style = MD_NORMAL;
            count++;
        }
    }
    return count;
}

static int md_has_markers(const char *text) {
    for (const char *p = text; *p; p++) {
        if (*p == '*' || *p == '`') return 1;
    }
    return 0;
}

static void md_style_color(MdStyle style, float *r, float *g, float *b) {
    switch (style) {
    case MD_BOLD:       *r = 1.0f;  *g = 1.0f;  *b = 1.0f;  break;
    case MD_ITALIC:     *r = 0.85f; *g = 0.85f; *b = 0.95f; break;
    case MD_CODE:
    case MD_CODE_BLOCK: *r = 0.7f;  *g = 0.85f; *b = 0.9f;  break;
    default:            *r = 0.9f;  *g = 0.9f;  *b = 0.85f; break;
    }
}

/* Measure height of markdown-wrapped text without drawing. */
static float chat_measure_markdown_wrapped(const char *text, float max_w,
                                           float tscale, float fb_w, float fb_h) {
    MdSegment segs[MD_MAX_SEGMENTS];
    int seg_count = md_parse(text, segs, MD_MAX_SEGMENTS);

    float line_h = text_line_height(tscale);
    float cursor_x = 0.0f;
    float total_h = line_h;
    for (int s = 0; s < seg_count; s++) {
        MdSegment *seg = &segs[s];
        float eff_max_w = max_w;

        if (seg->style == MD_CODE_BLOCK) {
            /* Code block: each line on its own */
            if (cursor_x > 0.0f) { total_h += line_h; cursor_x = 0.0f; }
            const char *lp = seg->start;
            const char *end = seg->start + seg->len;
            while (lp < end) {
                total_h += line_h;
                while (lp < end && *lp != '\n') lp++;
                if (lp < end) lp++; /* skip newline */
            }
            continue;
        }

        /* Word wrap within segment */
        const char *p = seg->start;
        const char *end = seg->start + seg->len;
        char word[512];

        while (p < end) {
            /* Extract next word (including trailing space/newline) */
            int wlen = 0;
            int has_newline = 0;
            while (p < end && *p != ' ' && *p != '\n' && wlen < 510) {
                word[wlen++] = *p++;
            }
            if (p < end && *p == ' ') { word[wlen++] = *p++; }
            if (p < end && *p == '\n') { p++; has_newline = 1; }
            word[wlen] = '\0';

            float ww = text_width(word, tscale);
            if (cursor_x + ww > eff_max_w && cursor_x > 0.0f) {
                total_h += line_h;
                cursor_x = 0.0f;
            }
            cursor_x += ww;

            if (has_newline) {
                total_h += line_h;
                cursor_x = 0.0f;
            }
        }
    }
    return total_h;
}

/* Draw markdown-wrapped text. Returns height used.
   Batches consecutive words of the same style on the same line into
   a single text_draw call to minimise GL overhead. */
static float chat_draw_markdown_wrapped(const char *text,
                                        float x, float y,
                                        float max_w, float tscale,
                                        float fb_w, float fb_h) {
    MdSegment segs[MD_MAX_SEGMENTS];
    int seg_count = md_parse(text, segs, MD_MAX_SEGMENTS);

    float line_h = text_line_height(tscale);
    float cursor_x = 0.0f;
    float cursor_y = 0.0f;
    float code_indent = 8.0f * tscale;

    /* Batch buffer: accumulates text of one style on one line */
    char batch[2048];
    int  batch_len = 0;
    float batch_x = 0.0f;
    float batch_y = 0.0f;
    MdStyle batch_style = MD_NORMAL;

    #define FLUSH_BATCH() do { \
        if (batch_len > 0) { \
            batch[batch_len] = '\0'; \
            float br, bg, bb; \
            md_style_color(batch_style, &br, &bg, &bb); \
            if (batch_style == MD_CODE) { \
                float bw = text_width(batch, tscale); \
                float pad = 2.0f * tscale; \
                c_draw_rect(x + batch_x - pad, y + batch_y - 1.0f, \
                            bw + pad * 2, line_h + 2.0f, \
                            0.08f, 0.08f, 0.10f, 0.8f, fb_w, fb_h); \
            } \
            text_draw(batch, x + batch_x, y + batch_y, tscale, \
                      br, bg, bb, fb_w, fb_h); \
            batch_len = 0; \
        } \
    } while (0)

    for (int s = 0; s < seg_count; s++) {
        MdSegment *seg = &segs[s];

        if (seg->style == MD_CODE_BLOCK) {
            FLUSH_BATCH();
            if (cursor_x > 0.0f) { cursor_y += line_h; cursor_x = 0.0f; }

            /* Count lines to draw one background rect */
            int nlines = 1;
            for (int ci = 0; ci < seg->len; ci++)
                if (seg->start[ci] == '\n') nlines++;
            float block_h = (float)nlines * line_h;
            c_draw_rect(x, y + cursor_y, max_w, block_h,
                        0.08f, 0.08f, 0.10f, 0.8f, fb_w, fb_h);

            float sr, sg, sb;
            md_style_color(MD_CODE_BLOCK, &sr, &sg, &sb);
            const char *lp = seg->start;
            const char *end = seg->start + seg->len;
            while (lp < end) {
                char line[512];
                int ll = 0;
                while (lp < end && *lp != '\n' && ll < 510)
                    line[ll++] = *lp++;
                line[ll] = '\0';
                if (lp < end && *lp == '\n') lp++;

                text_draw(line, x + code_indent, y + cursor_y, tscale,
                          sr, sg, sb, fb_w, fb_h);
                cursor_y += line_h;
            }
            continue;
        }

        /* On style change, flush */
        if (seg->style != batch_style) {
            FLUSH_BATCH();
            batch_style = seg->style;
        }

        const char *p = seg->start;
        const char *end = seg->start + seg->len;
        char word[512];

        while (p < end) {
            int wlen = 0;
            int has_newline = 0;
            while (p < end && *p != ' ' && *p != '\n' && wlen < 510)
                word[wlen++] = *p++;
            if (p < end && *p == ' ') { word[wlen++] = *p++; }
            if (p < end && *p == '\n') { p++; has_newline = 1; }
            word[wlen] = '\0';

            float ww = text_width(word, tscale);
            if (cursor_x + ww > max_w && cursor_x > 0.0f) {
                FLUSH_BATCH();
                cursor_y += line_h;
                cursor_x = 0.0f;
            }

            /* Start new batch if empty */
            if (batch_len == 0) {
                batch_x = cursor_x;
                batch_y = cursor_y;
                batch_style = seg->style;
            }

            /* Append word to batch if it fits */
            if (batch_len + wlen < (int)sizeof(batch) - 1) {
                memcpy(batch + batch_len, word, wlen);
                batch_len += wlen;
            } else {
                FLUSH_BATCH();
                batch_x = cursor_x;
                batch_y = cursor_y;
                batch_style = seg->style;
                if (wlen < (int)sizeof(batch) - 1) {
                    memcpy(batch, word, wlen);
                    batch_len = wlen;
                }
            }
            cursor_x += ww;

            if (has_newline) {
                FLUSH_BATCH();
                cursor_y += line_h;
                cursor_x = 0.0f;
            }
        }
    }

    FLUSH_BATCH();
    #undef FLUSH_BATCH

    return cursor_y + line_h;
}

/* ══════════════════════════════════════════════════════════════════════
   Emotion name helper
   ══════════════════════════════════════════════════════════════════════ */

static const char *emotion_name(Emotion e) {
    static const char *names[] = {
        "neutral", "happy", "excited", "surprised",
        "sleepy", "bored", "curious", "sad", "thinking"
    };
    if ((int)e >= 0 && (int)e < EMOTION_COUNT) return names[(int)e];
    return "neutral";
}

/* ══════════════════════════════════════════════════════════════════════
   LLM token callback
   ══════════════════════════════════════════════════════════════════════ */

static void on_token(const char *token, int thinking, int done, void *userdata) {
    ChatState *cs = (ChatState *)userdata;
    if (!done) {
        int len = (int)strlen(token);
        if (len == 0) return;
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
            msg->fade = 1.0f;
            msg->cached_md_height = -1.0f;
            msg->has_markdown = -1;
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
            msg->fade = 1.0f;
            msg->cached_md_height = -1.0f;
            msg->has_markdown = -1;
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
            msg->fade = 1.0f;
            msg->cached_md_height = -1.0f;
            msg->has_markdown = -1;
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
                    msg->fade = 1.0f;
                    msg->cached_md_height = -1.0f;
                msg->has_markdown = -1;
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
                msg->fade = 1.0f;
                msg->cached_md_height = -1.0f;
                msg->has_markdown = -1;
                stats_on_message();
            }

            /* Clear pending buffers, start generation */
            cs->pending_thinking[0] = '\0';
            cs->thinking_len = 0;
            cs->pending_response[0] = '\0';
            cs->pending_len = 0;
            cs->generating = 1;

            /* Build conversation history from past messages
               (exclude the user message we just added — it goes as user_msg).
               Ensure strict user/assistant alternation. */
            LlmMessage hist[CHAT_MAX_MESSAGES];
            int hist_count = 0;
            const char *last_role = NULL;
            for (int mi = 0; mi < cs->msg_count - 1; mi++) {
                ChatMessage *m = &cs->messages[mi];
                if (m->is_thinking) continue;
                const char *role = m->is_user ? "user" : "assistant";
                /* Skip consecutive same-role messages (keep latest) */
                if (last_role && strcmp(role, last_role) == 0) {
                    hist[hist_count - 1].content = m->text;
                    continue;
                }
                hist[hist_count].role = role;
                hist[hist_count].content = m->text;
                last_role = role;
                hist_count++;
            }
            /* History must start with user and alternate. If first is assistant, skip it. */
            int skip = 0;
            if (hist_count > 0 && strcmp(hist[0].role, "assistant") == 0)
                skip = 1;

            /* Send to LLM with history */
            llm_send(emotion_name(cs->current_mood),
                     hist + skip, hist_count - skip,
                     cs->input, on_token, cs);

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
        mood_color_get(cs->current_mood, &mr, &mg, &mb);
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
            /* Show only the last ~500 chars while streaming to prevent overflow */
            const char *display_text = cs->pending_response;
            if (cs->pending_len > 500) {
                display_text = cs->pending_response + cs->pending_len - 500;
                /* Skip to next newline or space for clean break */
                const char *brk = strchr(display_text, '\n');
                if (!brk) brk = strchr(display_text, ' ');
                if (brk) display_text = brk + 1;
            }
            float h = text_draw_wrapped(display_text,
                                        msgs_x, fb_h * 2, content_w, text_scale,
                                        0, 0, 0, fb_w, fb_h);
            float bubble_h = h + bubble_pad_y * 2;
            y -= bubble_h;
            float bubble_x = msgs_x;
            c_draw_rect(bubble_x, y, max_bubble_w, bubble_h,
                        0.18f, 0.18f, 0.20f, 0.85f,
                        fb_w, fb_h);
            text_draw_wrapped(display_text,
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
        int is_pet_response = (!msg->is_user && !msg->is_thinking);
        /* Lazy check for markdown markers */
        if (is_pet_response && msg->has_markdown < 0)
            msg->has_markdown = md_has_markers(msg->text);
        int use_markdown = (is_pet_response && msg->has_markdown == 1);
        float h;
        if (use_markdown) {
            if (msg->cached_md_height < 0.0f || msg->cached_md_width != content_w) {
                msg->cached_md_height = chat_measure_markdown_wrapped(
                    msg->text, content_w, text_scale, fb_w, fb_h);
                msg->cached_md_width = content_w;
            }
            h = msg->cached_md_height;
        } else {
            h = text_draw_wrapped(msg->text,
                                  msgs_x, fb_h * 2, content_w, text_scale,
                                  0, 0, 0, fb_w, fb_h);
        }
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

            /* Animated fade near top of message area.
               Use the bottom of the bubble (or msgs_bottom if clipped)
               so tall bubbles don't fade entirely. */
            float fade_height = (msgs_bottom - msgs_top) * 0.5f;
            float fade_target = 1.0f;
            float bubble_bottom = y + bubble_h;
            if (bubble_bottom > msgs_bottom) bubble_bottom = msgs_bottom;
            if (bubble_bottom < msgs_top + fade_height) {
                fade_target = (bubble_bottom - msgs_top) / fade_height;
                if (fade_target < 0.0f) fade_target = 0.0f;
                fade_target = fade_target * fade_target * (3.0f - 2.0f * fade_target);
            }
            /* Animate msg->fade toward target (~8 fps-independent speed) */
            float fade_speed = 8.0f * (1.0f / 60.0f);
            if (msg->fade < fade_target)
                msg->fade += fade_speed;
            if (msg->fade > fade_target)
                msg->fade -= fade_speed;
            if (msg->fade < 0.0f) msg->fade = 0.0f;
            if (msg->fade > 1.0f) msg->fade = 1.0f;
            anim_alpha *= msg->fade;

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
            text_set_alpha(anim_alpha);
            if (use_markdown) {
                /* Markdown rendered text */
                chat_draw_markdown_wrapped(msg->text,
                                           bubble_x + bubble_pad_x, y + bubble_pad_y,
                                           content_w, text_scale,
                                           fb_w, fb_h);
            } else {
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
                                  r, g, b, fb_w, fb_h);
            }
            text_set_alpha(1.0f);
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
