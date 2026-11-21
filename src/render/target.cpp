#include "render/target.h"

#include <SDL.h>

#include <algorithm>
#include <vector>

namespace amr {

RenderTarget::RenderTarget(int width, int height) : w_(width), h_(height) {
    glGenTextures(1, &color_);
    glBindTexture(GL_TEXTURE_2D, color_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w_, h_, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenRenderbuffers(1, &depth_);
    glBindRenderbuffer(GL_RENDERBUFFER, depth_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w_, h_);
    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glDeleteFramebuffers(1, &fbo_);
        fbo_ = 0;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

RenderTarget::~RenderTarget() {
    if (fbo_) glDeleteFramebuffers(1, &fbo_);
    if (depth_) glDeleteRenderbuffers(1, &depth_);
    if (color_) glDeleteTextures(1, &color_);
}

void RenderTarget::bind() {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, w_, h_);
}

void RenderTarget::present(int window_w, int window_h) {
    // the biggest 16:9 rectangle that fits, centred; black bars elsewhere
    int dw = window_w, dh = window_w * h_ / w_;
    if (dh > window_h) {
        dh = window_h;
        dw = window_h * w_ / h_;
    }
    int x0 = (window_w - dw) / 2, y0 = (window_h - dh) / 2;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glViewport(0, 0, window_w, window_h);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_);
    glBlitFramebuffer(0, 0, w_, h_, x0, y0, x0 + dw, y0 + dh, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
}

bool RenderTarget::save_bmp(const std::string& path) {
    std::vector<unsigned char> px(static_cast<size_t>(w_) * h_ * 4);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_);
    glReadPixels(0, 0, w_, h_, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    std::vector<unsigned char> flipped(px.size());  // OpenGL rows run bottom-up
    const size_t row = static_cast<size_t>(w_) * 4;
    for (int y = 0; y < h_; ++y)
        std::copy_n(&px[static_cast<size_t>(h_ - 1 - y) * row], row, &flipped[static_cast<size_t>(y) * row]);
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(flipped.data(), w_, h_, 32, static_cast<int>(row),
                                                        SDL_PIXELFORMAT_ABGR8888);
    if (!s) return false;
    bool ok = SDL_SaveBMP(s, path.c_str()) == 0;
    SDL_FreeSurface(s);
    return ok;
}

}  // namespace amr
