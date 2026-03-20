#ifndef TEXT_H
#define TEXT_H

#include <glad/gl.h>

/* Initialize the text renderer. Call once after GL context is ready. */
void text_init(void);

/* Draw a string at pixel position (x, y) in framebuffer coordinates.
   scale: multiplier for glyph size (1.0 = 8x8 pixels).
   r, g, b: text color.
   fb_w, fb_h: framebuffer dimensions for NDC conversion. */
void text_draw(const char *str, float x, float y, float scale,
               float r, float g, float b, float fb_w, float fb_h);

/* Measure the width in pixels of a string at given scale. */
float text_width(const char *str, float scale);

/* Returns the line height in pixels at given scale. */
float text_line_height(float scale);

/* Draw text with word-wrapping within max_width pixels.
   Returns the total height used (for layout). */
float text_draw_wrapped(const char *str, float x, float y,
                        float max_width, float scale,
                        float r, float g, float b,
                        float fb_w, float fb_h);

/* Draw a filled rectangle at pixel coordinates.
   x, y: top-left corner. w, h: size.
   r, g, b, a: color with alpha. */
void text_draw_rect(float x, float y, float w, float h,
                    float r, float g, float b, float a,
                    float fb_w, float fb_h);

/* Cleanup GL resources. */
void text_cleanup(void);

#endif
