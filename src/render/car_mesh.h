// Chunky cartoon cars (game/render3d.py _draw_cartoon_car_body): big wheels sticking out, a body, a tapered glass
// cabin, head and tail lights; a pickup bed, a spare wheel on the back door, a police light bar. The shape comes from
// the model's measurements (game/car.py MODELS), the colours from its look. Built once per model and look, in the
// car's own frame (x forward, y to the right, z up, the car's centre on the ground at the origin).
#pragma once
#include <cstdint>
#include <string>

#include "render/mesh.h"

namespace amr {

struct CarShape {  // game/car.py MODELS
    const char* name;
    double length, width, ride, belt, roof, cab0, cab1, top0, top1, wheel;
    bool bed, spare;
    double bed0, bed1;
};
const CarShape& car_shape(const std::string& model);  // "sedan", "fj", "crv", "sorento", "wagon", "ram"

struct CarLook {
    std::uint8_t body[3], roof[3], glass[3], tail[3];
    int lightbar = 0;  // 0 none, 1 / 2 the police bar's two flashing states
};
CarLook player_look(int index);      // game/car.py PLAYER_LOOKS: P1 yellow/white SUV, P2 white, P3 grey, P4 black
CarLook traffic_look(int colour);    // game/ai.py PAL_TRAFFIC
CarLook police_look(int flash);      // game/ai.py PAL_POLICE, light bar state 1 or 2

MeshBuilder build_car(const CarShape& s, const CarLook& look);

}  // namespace amr
