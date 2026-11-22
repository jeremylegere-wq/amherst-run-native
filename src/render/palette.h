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
// traffic body colours (game/ai.py PAL_TRAFFIC)
inline constexpr std::uint8_t kTraffic[8][3] = {{196, 60, 60}, {60, 120, 200}, {80, 160, 90}, {200, 200, 205},
                                                {70, 70, 80},   {210, 130, 50}, {150, 90, 170}, {120, 190, 190}};
inline constexpr std::uint8_t kTreeLit[3][3] = {{98, 206, 72}, {150, 216, 58}, {72, 192, 112}};
inline constexpr std::uint8_t kTreeShade[3][3] = {{44, 138, 66}, {72, 150, 52}, {34, 124, 88}};
inline constexpr std::uint8_t kTrunk[3] = {124, 80, 46};
inline constexpr std::uint8_t kLamp[3] = {64, 66, 96};
inline constexpr std::uint8_t kLampHead[3] = {255, 246, 190};
inline constexpr std::uint8_t kWoodPole[3] = {112, 84, 60};
inline constexpr std::uint8_t kSteel[3] = {150, 154, 162};
inline constexpr std::uint8_t kStone[3] = {120, 122, 128};
inline constexpr std::uint8_t kFence[3] = {150, 120, 90};
inline constexpr std::uint8_t kHydrant[3] = {214, 52, 44};
inline constexpr std::uint8_t kSignRed[3] = {206, 36, 36};
inline constexpr std::uint8_t kDarkGreen[3] = {50, 84, 62};
inline constexpr std::uint8_t kBench[3] = {132, 96, 62};
inline constexpr std::uint8_t kSignalBox[3] = {44, 44, 50};
inline constexpr std::uint8_t kWhite[3] = {236, 236, 228};

}  // namespace amr::palette
