#include "render/shader.h"

namespace amr {

static GLuint compile(GLenum kind, const char* src, std::string& error) {
    GLuint s = glCreateShader(kind);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = {};
        glGetShaderInfoLog(s, sizeof log, nullptr, log);
        error = std::string(kind == GL_VERTEX_SHADER ? "vertex shader: " : "fragment shader: ") + log;
        glDeleteShader(s);
        return 0;
    }
    return s;
}

GLuint make_program(const char* vertex_src, const char* fragment_src, std::string& error) {
    GLuint vs = compile(GL_VERTEX_SHADER, vertex_src, error);
    if (!vs) return 0;
    GLuint fs = compile(GL_FRAGMENT_SHADER, fragment_src, error);
    if (!fs) {
        glDeleteShader(vs);
        return 0;
    }
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024] = {};
        glGetProgramInfoLog(p, sizeof log, nullptr, log);
        error = std::string("link: ") + log;
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

}  // namespace amr
