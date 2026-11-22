#include "core/log.h"
#include "render/hud.h"

#include <cctype>
#include <cmath>
#include <cstdio>

#include "render/shader.h"

namespace amr {

namespace {

const char* kVertex = R"(#version 300 es
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec3 a_color;
uniform vec2 u_size;
out vec3 v_color;
void main() {
    v_color = a_color;
    gl_Position = vec4(a_pos.x / u_size.x * 2.0 - 1.0, 1.0 - a_pos.y / u_size.y * 2.0, 0.0, 1.0);
}
)";

const char* kFragment = R"(#version 300 es
precision mediump float;
in vec3 v_color;
uniform float u_alpha;
layout(location = 0) out vec4 o_color;
layout(location = 1) out vec4 o_normal;  // (the scene's normal buffer, when the sky is drawn there: no surface)
void main() {
    o_color = vec4(v_color, u_alpha);
    o_normal = vec4(0.5, 0.5, 1.0, 0.0);
}
)";

// 3x5 glyphs, one row per entry, bit 2 = left column.
struct Glyph {
    char c;
    unsigned char rows[5];
};
const Glyph kFont[] = {
    {'0', {7, 5, 5, 5, 7}}, {'1', {2, 6, 2, 2, 7}}, {'2', {7, 1, 7, 4, 7}}, {'3', {7, 1, 7, 1, 7}},
    {'4', {5, 5, 7, 1, 1}}, {'5', {7, 4, 7, 1, 7}}, {'6', {7, 4, 7, 5, 7}}, {'7', {7, 1, 1, 2, 2}},
    {'8', {7, 5, 7, 5, 7}}, {'9', {7, 5, 7, 1, 7}}, {'A', {2, 5, 7, 5, 5}}, {'B', {6, 5, 6, 5, 6}},
    {'C', {3, 4, 4, 4, 3}}, {'D', {6, 5, 5, 5, 6}}, {'E', {7, 4, 6, 4, 7}}, {'F', {7, 4, 6, 4, 4}},
    {'G', {3, 4, 5, 5, 3}}, {'H', {5, 5, 7, 5, 5}}, {'I', {7, 2, 2, 2, 7}}, {'J', {1, 1, 1, 5, 2}},
    {'K', {5, 5, 6, 5, 5}}, {'L', {4, 4, 4, 4, 7}}, {'M', {5, 7, 7, 5, 5}}, {'N', {6, 5, 5, 5, 5}},
    {'O', {2, 5, 5, 5, 2}}, {'P', {6, 5, 6, 4, 4}}, {'Q', {2, 5, 5, 6, 3}}, {'R', {6, 5, 6, 5, 5}},
    {'S', {3, 4, 2, 1, 6}}, {'T', {7, 2, 2, 2, 2}}, {'U', {5, 5, 5, 5, 7}}, {'V', {5, 5, 5, 5, 2}},
    {'W', {5, 5, 7, 7, 5}}, {'X', {5, 5, 2, 5, 5}}, {'Y', {5, 5, 2, 2, 2}}, {'Z', {7, 1, 2, 4, 7}},
    {'/', {1, 1, 2, 4, 4}}, {'-', {0, 0, 7, 0, 0}}, {'.', {0, 0, 0, 0, 2}}, {' ', {0, 0, 0, 0, 0}},
    {'!', {2, 2, 2, 0, 2}}, {'(', {1, 2, 2, 2, 1}}, {')', {4, 2, 2, 2, 4}}, {':', {0, 2, 0, 2, 0}},
    {'$', {7, 6, 7, 3, 7}}, {'+', {0, 2, 7, 2, 0}}, {'\'', {2, 2, 0, 0, 0}}, {',', {0, 0, 0, 2, 4}},
};
const Glyph* glyph(char c) {
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    for (const Glyph& g : kFont)
        if (g.c == c) return &g;
    return nullptr;
}
const std::uint8_t kShadow[3] = {16, 16, 24};

}  // namespace

Hud::Hud(int width, int height) : w_(width), h_(height) {
    std::string err;
    program_ = make_program(kVertex, kFragment, err);
    if (!program_) {
        log_error("hud shader: %s", err.c_str());
        return;
    }
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), reinterpret_cast<void*>(2 * sizeof(float)));
    glBindVertexArray(0);
}

Hud::~Hud() {
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (program_) glDeleteProgram(program_);
}

void Hud::begin() { verts_.clear(); }

void Hud::rect(int x, int y, int w, int h, const std::uint8_t rgb[3]) {
    const float r = rgb[0] / 255.0f, g = rgb[1] / 255.0f, b = rgb[2] / 255.0f;
    const float x0 = static_cast<float>(x), y0 = static_cast<float>(y), x1 = x0 + w, y1 = y0 + h;
    const float v[6][2] = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y0}, {x1, y1}, {x0, y1}};
    for (const auto& p : v) verts_.insert(verts_.end(), {p[0], p[1], r, g, b});
}

void Hud::vgrad(int x, int y, int w, int h, const std::uint8_t top[3], const std::uint8_t bottom[3]) {
    const float t[3] = {top[0] / 255.0f, top[1] / 255.0f, top[2] / 255.0f};
    const float b[3] = {bottom[0] / 255.0f, bottom[1] / 255.0f, bottom[2] / 255.0f};
    const float x0 = static_cast<float>(x), y0 = static_cast<float>(y), x1 = x0 + w, y1 = y0 + h;
    verts_.insert(verts_.end(), {x0, y0, t[0], t[1], t[2], x1, y0, t[0], t[1], t[2], x1, y1, b[0], b[1], b[2],
                                 x0, y0, t[0], t[1], t[2], x1, y1, b[0], b[1], b[2], x0, y1, b[0], b[1], b[2]});
}

int Hud::text_width(const std::string& s, int scale) const { return s.empty() ? 0 : static_cast<int>(s.size()) * 4 * scale - scale; }

void Hud::text(int x, int y, const std::string& s, const std::uint8_t rgb[3], int scale, bool shadow) {
    const bool italic = shadow && scale >= 2;
    auto draw = [&](int ox, int oy, const std::uint8_t* col) {
        int cx = x;
        for (char c : s) {
            if (const Glyph* g = glyph(c))
                for (int row = 0; row < 5; ++row) {
                    const int lean = italic ? (4 - row) * scale / 2 : 0;  // upper rows sit further right
                    for (int bit = 0; bit < 3; ++bit)
                        if (g->rows[row] & (4 >> bit)) rect(cx + bit * scale + ox + lean, y + row * scale + oy, scale, scale, col);
                }
            cx += 4 * scale;
        }
    };
    if (shadow) {
        const int k = scale >= 3 ? 2 : 1;  // the ink outline's thickness
        draw(k + 1, k + 1, kShadow);       // drop shadow
        for (int dx = -k; dx <= k; dx += k)
            for (int dy = -k; dy <= k; dy += k)
                if (dx || dy) draw(dx, dy, kShadow);
    }
    draw(0, 0, rgb);
}

void Hud::slant(int x, int y, int w, int h, int lean, const std::uint8_t rgb[3]) {
    const float x0 = static_cast<float>(x), y0 = static_cast<float>(y), x1 = x0 + w, y1 = y0 + h;
    tri(x0 + lean, y0, x1 + lean, y0, x1, y1, rgb);
    tri(x0 + lean, y0, x1, y1, x0, y1, rgb);
}

void Hud::ellipse(float cx, float cy, float rx, float ry, const std::uint8_t rgb[3]) {
    for (int k = 0; k < 16; ++k) {
        const float a0 = k * 0.3926991f, a1 = (k + 1) * 0.3926991f;
        tri(cx, cy, cx + rx * std::cos(a0), cy + ry * std::sin(a0), cx + rx * std::cos(a1), cy + ry * std::sin(a1), rgb);
    }
}

void Hud::tri(float x0, float y0, float x1, float y1, float x2, float y2, const std::uint8_t rgb[3]) {
    const float r = rgb[0] / 255.0f, g = rgb[1] / 255.0f, b = rgb[2] / 255.0f;
    verts_.insert(verts_.end(), {x0, y0, r, g, b, x1, y1, r, g, b, x2, y2, r, g, b});
}

void Hud::line(float x0, float y0, float x1, float y1, float width, const std::uint8_t rgb[3]) {
    float dx = x1 - x0, dy = y1 - y0;
    const float l = std::sqrt(dx * dx + dy * dy);
    if (l < 1e-4f) return;
    dx /= l;
    dy /= l;
    const float hw = width / 2, nx = -dy * hw, ny = dx * hw;
    x0 -= dx * hw; y0 -= dy * hw; x1 += dx * hw; y1 += dy * hw;
    tri(x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, rgb);
    tri(x0 + nx, y0 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny, rgb);
}

void Hud::end() {
    if (!program_ || verts_.empty()) return;
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glUseProgram(program_);
    glUniform2f(glGetUniformLocation(program_, "u_size"), static_cast<float>(w_), static_cast<float>(h_));
    glUniform1f(glGetUniformLocation(program_, "u_alpha"), alpha_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts_.size() * sizeof(float)), verts_.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts_.size() / 5));
    glBindVertexArray(0);
}

}  // namespace amr
