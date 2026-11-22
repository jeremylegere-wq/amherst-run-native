// The heads-up display, drawn straight onto the 320x180 picture in pixels: flat rectangles and a small blocky
// 3x5 font (original, like everything else). Speed, wanted stars, the damage bar, and BUSTED / WRECKED.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "render/gl.h"

namespace amr {

class Hud {
public:
    Hud(int width, int height);
    ~Hud();
    bool ok() const { return program_ != 0; }
    void begin();                                                        // start a frame's HUD
    void rect(int x, int y, int w, int h, const std::uint8_t rgb[3]);    // y down from the top, in pixels
    // Text in the 3x5 font at `scale` pixels per dot, with a one-dot dark shadow. Letters, digits, space, / - .
    void text(int x, int y, const std::string& s, const std::uint8_t rgb[3], int scale = 1);
    int text_width(const std::string& s, int scale = 1) const;
    void end();                                                          // draw it all over the picture

private:
    int w_, h_;
    unsigned program_ = 0, vao_ = 0, vbo_ = 0;
    std::vector<float> verts_;  // x, y, r, g, b per vertex
};

}  // namespace amr
