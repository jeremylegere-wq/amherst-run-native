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
    void set_size(int width, int height) { w_ = width; h_ = height; }    // the viewport it draws into
    void set_alpha(float a) { alpha_ = a; }  // what it writes in the picture's alpha (the sky: 0, no ink)
    // A rectangle shading from one colour at the top to another at the bottom.
    void vgrad(int x, int y, int w, int h, const std::uint8_t top[3], const std::uint8_t bottom[3]);
    void rect(int x, int y, int w, int h, const std::uint8_t rgb[3]);    // y down from the top, in pixels
    // Text in the 3x5 font at `scale` pixels per dot, with a one-dot dark shadow. Letters, digits, space and / - . ! ( ) : $ + ' ,
    void text(int x, int y, const std::string& s, const std::uint8_t rgb[3], int scale = 1);
    int text_width(const std::string& s, int scale = 1) const;
    void tri(float x0, float y0, float x1, float y1, float x2, float y2, const std::uint8_t rgb[3]);
    void end();                                                          // draw it all over the picture

private:
    int w_, h_;
    float alpha_ = 1.0f;
    unsigned program_ = 0, vao_ = 0, vbo_ = 0;
    std::vector<float> verts_;  // x, y, r, g, b per vertex
};

}  // namespace amr
