#ifndef GL_UTIL_H
#define GL_UTIL_H

#include <glad/gl.h>

/* Compile a shader. Aborts on failure with diagnostic to stderr. */
GLuint gl_compile_shader(GLenum type, const char *src, const char *label);

/* Link a vertex + fragment shader into a program. Aborts on failure. */
GLuint gl_link_program(GLuint vert, GLuint frag, const char *label);

#endif
