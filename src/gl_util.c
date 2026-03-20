#include "gl_util.h"
#include <stdio.h>
#include <stdlib.h>

GLuint gl_compile_shader(GLenum type, const char *src, const char *label) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    int ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        fprintf(stderr, "%s shader compile failed: %s\n", label, log);
        abort();
    }
    return s;
}

GLuint gl_link_program(GLuint vert, GLuint frag, const char *label) {
    GLuint p = glCreateProgram();
    glAttachShader(p, vert);
    glAttachShader(p, frag);
    glLinkProgram(p);
    int ok;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(p, sizeof(log), NULL, log);
        fprintf(stderr, "%s shader link failed: %s\n", label, log);
        abort();
    }
    return p;
}
