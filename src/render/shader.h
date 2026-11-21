// Compiling and linking GLSL ES 3.00 programs, with readable errors.
#pragma once
#include <string>

#include "render/gl.h"

namespace amr {

// Returns the linked program, or 0 with the compiler's message in `error`.
GLuint make_program(const char* vertex_src, const char* fragment_src, std::string& error);

}  // namespace amr
