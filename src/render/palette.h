// Colours of the street style (the Python game's STYLES["street"] in game/render3d.py), 0..255.
#pragma once
#include <cstdint>

namespace amr::palette {

inline constexpr std::uint8_t kGrass[3] = {80, 178, 70};
inline constexpr std::uint8_t kRoadLane[3] = {156, 150, 142};
inline constexpr std::uint8_t kRoadSide[3] = {124, 124, 132};
inline constexpr std::uint8_t kRoadMain[3] = {108, 108, 118};
inline constexpr std::uint8_t kGravel[3] = {186, 164, 126};
inline constexpr std::uint8_t kKerb[3] = {206, 202, 194};
inline constexpr std::uint8_t kWater[3] = {36, 118, 224};
inline constexpr std::uint8_t kFog[3] = {160, 206, 244};
inline constexpr std::uint8_t kSkyTop[3] = {16, 58, 196};
inline constexpr std::uint8_t kSkyBottom[3] = {104, 184, 248};
inline constexpr std::uint8_t kCarYellow[3] = {245, 219, 61};

}  // namespace amr::palette
