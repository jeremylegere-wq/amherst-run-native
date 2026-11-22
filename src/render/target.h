// The game's low-resolution picture: everything is drawn into a 320x180 colour + depth buffer, then scaled up to
// the window with nearest-neighbour (chunky pixels), letterboxed to keep 16:9. Same look as the Python game.
//
// Between the scene and the HUD comes the ink pass (the street style's dark outlines): buildings, cars and props
// write 1 in the picture's alpha; wherever such a pixel's depth jumps or folds against its neighbours, it is inked.
// Terrain, roads and areas write 0 and get no lines, like the Python game, and the ink fades out with distance.
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

    bool ok() const { return fbo_ != 0 && fbo2_ != 0 && ink_prog_ != 0; }
    int width() const { return w_; }
    int height() const { return h_; }
    void bind();                                   // draw the scene into the small picture
    // Outline the scene (near / far: the projection's planes, to turn depth into metres) into the final picture,
    // and leave that bound for the HUD. ink_until: metres beyond which there are no lines.
    void ink(float znear, float zfar, float ink_until, const float rgb[3], bool enabled = true);
    void present(int window_w, int window_h);      // scale the final picture up into the window
    bool save_bmp(const std::string& path);        // the final picture as a .bmp (tests, screenshots)
    unsigned final_fbo() const { return fbo2_; }   // the final picture's framebuffer (the HUD and minimap draw into it)

private:
    int w_, h_;
    GLuint fbo_ = 0, color_ = 0, depth_ = 0;       // the scene
    GLuint fbo2_ = 0, color2_ = 0;                 // the final picture (scene + ink, then the HUD)
    GLuint ink_prog_ = 0, vao_ = 0;
};

}  // namespace amr
