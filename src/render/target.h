// The game's low-resolution picture: everything is drawn into a 320x180 colour + depth buffer, then scaled up to
// the window with nearest-neighbour (chunky pixels), letterboxed to keep 16:9. Same look as the Python game.
#pragma once
#include <string>

#include "render/gl.h"

namespace amr {

class RenderTarget {
public:
    RenderTarget(int width, int height);
    ~RenderTarget();
    RenderTarget(const RenderTarget&) = delete;
    RenderTarget& operator=(const RenderTarget&) = delete;

    bool ok() const { return fbo_ != 0; }
    int width() const { return w_; }
    int height() const { return h_; }
    void bind();                                   // draw into the small picture
    void present(int window_w, int window_h);      // scale it up into the window (default framebuffer)
    bool save_bmp(const std::string& path);        // the small picture as a .bmp (tests, screenshots)

private:
    int w_, h_;
    GLuint fbo_ = 0, color_ = 0, depth_ = 0;
};

}  // namespace amr
