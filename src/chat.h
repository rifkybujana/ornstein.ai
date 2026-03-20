#ifndef CHAT_H
#define CHAT_H

#include "face.h"  /* for Emotion enum */

typedef struct ChatState ChatState;

/* Create and initialize the chat panel. */
ChatState *chat_create(void);

/* Toggle chat panel visibility. Returns new visibility state. */
int chat_toggle(ChatState *cs);

/* Returns 1 if chat panel is visible. */
int chat_visible(ChatState *cs);

/* Returns 1 if chat panel is capturing keyboard input. */
int chat_has_focus(ChatState *cs);

/* Handle a character input (from GLFW char callback). */
void chat_on_char(ChatState *cs, unsigned int codepoint);

/* Handle a key press (backspace, enter, etc.). */
void chat_on_key(ChatState *cs, int key, int action, int mods);

/* Handle scroll input. */
void chat_on_scroll(ChatState *cs, double yoffset);

/* Update chat state (call each frame).
   mood: current emotion for LLM context. */
void chat_update(ChatState *cs, Emotion mood, float dt);

/* Render the chat panel.
   panel_x: left edge of the panel in fb pixels.
   panel_w: width of the panel in fb pixels.
   fb_h: framebuffer height.
   scale: HiDPI scale factor. */
void chat_render(ChatState *cs, float panel_x, float panel_w,
                 float fb_h, float scale, float fb_w);

/* Free resources. */
void chat_destroy(ChatState *cs);

#endif
