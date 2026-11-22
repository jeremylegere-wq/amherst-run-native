// The landmark props (game/render3d.py _draw_lighthouse, the cenotaph, _draw_dam, _draw_horse), built into a chunk's
// mesh: the border welcome lighthouse with its porch, planter, sign and flags; the World Wars Memorial cenotaph; the
// Pumping Station dam with its spillway, water curtain, gate piers and fish ladder; and the black horse at a gallop.
#pragma once
#include <string>

#include "render/mesh.h"
#include "world/map_file.h"

namespace amr {

// Add a landmark prop of this kind standing on ground height g. False if the kind is not a landmark.
bool add_landmark(MeshBuilder& m, const std::string& kind, const Prop& p, float g);

}  // namespace amr
