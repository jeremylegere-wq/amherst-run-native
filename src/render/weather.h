// Weather presets (game/render3d.py WEATHER, laid over the street style): sky, fog, distant hills, clouds, shadows,
// a colour grade on everything lit, snow on the ground and falling. Switched with --weather or F4.
#pragma once
#include <cstdint>
#include <cstring>
#include <string>

namespace amr {

struct Weather {
    const char* name;
    std::uint8_t sky_top[3], sky_bottom[3];
    int sky_bands;
    std::uint8_t fog[3];
    float fog_scale;                     // how thick the fog is (the street style's clear day: 0.55)
    std::uint8_t hill_far[3], hill_near[3];
    int clouds;                          // how many cartoon clouds
    bool shadows;                        // see-through hard shadows at all
    std::uint8_t shadow[4];              // their colour (RGBA)
    float shadow_dx, shadow_dy;          // metres of shadow per metre of height (low golden sun: long, to the east)
    float grade[3], desat;               // colour grade: multipliers, then desaturation 0..1
    int snowfall;                        // flakes on screen
    bool hide_horizon;                   // thick fog: no hills
    bool snow;                           // snow on the grass, parks, roofs and trees
};

// clang-format off
inline constexpr Weather kWeathers[] = {
    {"clear",    {16, 58, 196},   {104, 184, 248}, 32, {160, 206, 244}, 0.55f, {126, 176, 226}, {66, 154, 126}, 9,
     true,  {24, 22, 72, 120}, 0.45f, -0.4f, {1.0f, 1.0f, 1.0f},    0.0f,  0,   false, false},
    {"overcast", {108, 122, 140}, {178, 186, 196}, 12, {172, 180, 190}, 0.85f, {140, 150, 162}, {92, 116, 108}, 0,
     false, {0, 0, 0, 0},      0.45f, -0.4f, {0.92f, 0.94f, 0.98f}, 0.30f, 0,   false, false},
    {"fog",      {186, 194, 202}, {208, 214, 220}, 6,  {206, 212, 218}, 1.7f,  {206, 212, 218}, {206, 212, 218}, 0,
     false, {0, 0, 0, 0},      0.45f, -0.4f, {0.96f, 0.98f, 1.0f},  0.40f, 0,   true,  false},
    {"golden",   {58, 70, 160},   {252, 186, 120}, 32, {246, 196, 150}, 0.6f,  {190, 140, 150}, {104, 116, 84}, 9,
     true,  {70, 30, 60, 90},  1.15f, -0.2f, {1.10f, 0.95f, 0.78f}, 0.0f,  0,   false, false},
    {"snow",     {140, 156, 182}, {212, 220, 232}, 16, {216, 222, 232}, 0.95f, {196, 206, 222}, {222, 228, 236}, 0,
     false, {0, 0, 0, 0},      0.45f, -0.4f, {0.96f, 0.98f, 1.04f}, 0.15f, 160, false, true},
};
// clang-format on
inline constexpr int kWeatherCount = sizeof(kWeathers) / sizeof(kWeathers[0]);

// Index of the preset with this name, or -1.
inline int weather_index(const std::string& name) {
    for (int k = 0; k < kWeatherCount; ++k)
        if (name == kWeathers[k].name) return k;
    return -1;
}

// What snow turns things to (game/render3d.py WEATHER["snow"]).
namespace snow {
inline constexpr std::uint8_t kGround[3] = {232, 238, 246};
inline constexpr std::uint8_t kPark[3] = {226, 234, 242};
inline constexpr std::uint8_t kRoof[3] = {232, 236, 244};
inline constexpr std::uint8_t kGravel[3] = {214, 210, 204};
inline constexpr std::uint8_t kTreeLit[3][3] = {{226, 236, 240}, {236, 242, 246}, {214, 228, 232}};
inline constexpr std::uint8_t kTreeShade[3][3] = {{92, 132, 112}, {70, 112, 96}, {104, 120, 92}};
}  // namespace snow

}  // namespace amr
