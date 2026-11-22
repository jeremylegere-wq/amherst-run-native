#include "render/target.h"

#include <SDL.h>

#include <algorithm>
#include <cstdio>
#include <vector>

#include "render/shader.h"

namespace amr {

namespace {

const char* kInkVertex = R"(#version 300 es
void main() {  // one triangle over the whole picture, from the vertex number alone
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

const char* kInkFragment = R"(#version 300 es
precision highp float;
uniform sampler2D u_color;
uniform sampler2D u_normal;
uniform highp sampler2D u_depth;
uniform vec2 u_planes;      // near, far
uniform float u_until;      // metres: no ink beyond
uniform vec3 u_ink;
uniform int u_enabled;
out vec4 o_color;
float metres(ivec2 p) {
    float d = texelFetch(u_depth, p, 0).r * 2.0 - 1.0;
    return 2.0 * u_planes.x * u_planes.y / (u_planes.y + u_planes.x - d * (u_planes.y - u_planes.x));
}
void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    ivec2 size = textureSize(u_color, 0);
    vec4 c = texelFetch(u_color, p, 0);
    o_color = vec4(c.rgb, 1.0);
    if (u_enabled == 0 || p.x < 1 || p.y < 1 || p.x >= size.x - 1 || p.y >= size.y - 1) return;
    ivec2 n[4] = ivec2[4](ivec2(1, 0), ivec2(-1, 0), ivec2(0, 1), ivec2(0, -1));
    float mask = c.a;
    for (int i = 0; i < 4; ++i) mask = max(mask, texelFetch(u_color, p + n[i], 0).a);
    if (mask < 0.5) return;                      // only buildings, cars and props get lines
    float dc = metres(p);
    float dl = metres(p + n[1]), dr = metres(p + n[0]), du = metres(p + n[2]), dd = metres(p + n[3]);
    float near = min(dc, min(min(dl, dr), min(du, dd)));
    if (near > u_until) return;
    // a silhouette (something much nearer next door) or a fold (the depth's second difference: a corner)
    float jump = max(max(dc - dl, dc - dr), max(dc - du, dc - dd));
    float fold = max(abs(dl + dr - 2.0 * dc), abs(du + dd - 2.0 * dc));
    if (jump > 0.06 * dc + 0.3 || fold > 0.025 * dc + 0.12) { o_color = vec4(u_ink, 1.0); return; }
    // close up the outline is two pixels thick (something within 30 m two pixels away, much nearer than here)
    for (int i = 0; i < 4; ++i) {
        ivec2 q = p + 2 * n[i];
        if (q.x < 0 || q.y < 0 || q.x >= size.x || q.y >= size.y) continue;
        float dq = metres(q);
        if (dq < 30.0 && dc - dq > 0.06 * dc + 0.3 && texelFetch(u_color, q, 0).a > 0.5) { o_color = vec4(u_ink, 1.0); return; }
    }
    // a crease: the surface turns (a building's corner, a roof's edge, a porch, a car's panels), drawn on the nearer
    // side so the line stays one pixel; only where both sides want creases inked (not across a tree's facets)
    vec4 nc = texelFetch(u_normal, p, 0);
    if (nc.a < 0.5) return;
    vec3 vc = nc.xyz * 2.0 - 1.0;
    float dn[4] = float[4](dr, dl, du, dd);
    for (int i = 0; i < 4; ++i) {
        vec4 nn = texelFetch(u_normal, p + n[i], 0);
        if (nn.a < 0.5) continue;
        if (dot(vc, nn.xyz * 2.0 - 1.0) < 0.82 && dc <= dn[i] + 0.02) { o_color = vec4(u_ink, 1.0); return; }
    }
}
)";

GLuint make_color(int w, int h) {
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    return t;
}

}  // namespace

RenderTarget::RenderTarget(int width, int height) : w_(width), h_(height) {
    color_ = make_color(w_, h_);
    glGenTextures(1, &depth_);
    glBindTexture(GL_TEXTURE_2D, depth_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, w_, h_, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, depth_, 0);  // (stencil: shadows)
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glDeleteFramebuffers(1, &fbo_);
        fbo_ = 0;
    }
    normal_ = make_color(w_, h_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, normal_, 0);
    {
        static const GLenum kBoth[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
        glDrawBuffers(2, kBoth);
    }
    if (fbo_ && glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glDeleteFramebuffers(1, &fbo_);
        fbo_ = 0;
    }
    color2_ = make_color(w_, h_);
    glGenFramebuffers(1, &fbo2_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo2_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color2_, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glDeleteFramebuffers(1, &fbo2_);
        fbo2_ = 0;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    std::string err;
    ink_prog_ = make_program(kInkVertex, kInkFragment, err);
    if (!ink_prog_) std::fprintf(stderr, "ink shader: %s\n", err.c_str());
    glGenVertexArrays(1, &vao_);
}

RenderTarget::~RenderTarget() {
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (ink_prog_) glDeleteProgram(ink_prog_);
    if (fbo2_) glDeleteFramebuffers(1, &fbo2_);
    if (color2_) glDeleteTextures(1, &color2_);
    if (fbo_) glDeleteFramebuffers(1, &fbo_);
    if (depth_) glDeleteTextures(1, &depth_);
    if (normal_) glDeleteTextures(1, &normal_);
    if (color_) glDeleteTextures(1, &color_);
}

void RenderTarget::bind() {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, w_, h_);
}

void RenderTarget::clear_normals() {
    static const GLfloat kNone[4] = {0.5f, 0.5f, 1.0f, 0.0f};
    glClearBufferfv(GL_COLOR, 1, kNone);
}

void RenderTarget::ink(float znear, float zfar, float ink_until, const float rgb[3], bool enabled) {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo2_);
    glViewport(0, 0, w_, h_);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glUseProgram(ink_prog_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, color_);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, depth_);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, normal_);
    glUniform1i(glGetUniformLocation(ink_prog_, "u_color"), 0);
    glUniform1i(glGetUniformLocation(ink_prog_, "u_depth"), 1);
    glUniform1i(glGetUniformLocation(ink_prog_, "u_normal"), 2);
    glUniform2f(glGetUniformLocation(ink_prog_, "u_planes"), znear, zfar);
    glUniform1f(glGetUniformLocation(ink_prog_, "u_until"), ink_until);
    glUniform3f(glGetUniformLocation(ink_prog_, "u_ink"), rgb[0], rgb[1], rgb[2]);
    glUniform1i(glGetUniformLocation(ink_prog_, "u_enabled"), enabled ? 1 : 0);
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE0);
}

void RenderTarget::present(int window_w, int window_h) {
    // the biggest 16:9 rectangle that fits, centred; black bars elsewhere
    int dw = window_w, dh = window_w * h_ / w_;
    if (dh > window_h) {
        dh = window_h;
        dw = window_h * w_ / h_;
    }
    const int x0 = (window_w - dw) / 2, y0 = (window_h - dh) / 2;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glViewport(0, 0, window_w, window_h);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo2_);
    glBlitFramebuffer(0, 0, w_, h_, x0, y0, x0 + dw, y0 + dh, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
}

bool RenderTarget::save_bmp(const std::string& path) {
    std::vector<unsigned char> px(static_cast<size_t>(w_) * h_ * 4);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo2_);
    glReadPixels(0, 0, w_, h_, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    std::vector<unsigned char> flipped(px.size());  // OpenGL rows run bottom-up
    const size_t row = static_cast<size_t>(w_) * 4;
    for (int y = 0; y < h_; ++y)
        std::copy_n(&px[static_cast<size_t>(h_ - 1 - y) * row], row, &flipped[static_cast<size_t>(y) * row]);
    for (size_t i = 3; i < flipped.size(); i += 4) flipped[i] = 255;  // opaque
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(flipped.data(), w_, h_, 32, static_cast<int>(row),
                                                        SDL_PIXELFORMAT_ABGR8888);
    if (!s) return false;
    const bool ok = SDL_SaveBMP(s, path.c_str()) == 0;
    SDL_FreeSurface(s);
    return ok;
}

}  // namespace amr
