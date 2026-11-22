// The street style's building details, built once into a chunk's mesh (game/render3d.py: _draw_house_wall,
// _draw_porch, _draw_shop_front, _draw_windows, _draw_arched_windows, _attic_window, _draw_dormers, _draw_chimney,
// _draw_battlements, the steeples and the clock tower). What goes where comes from the map's BDET chunk.
#pragma once
#include <vector>

#include "core/math.h"
#include "render/mesh.h"
#include "world/map_file.h"

namespace amr {

// Everything on and around building `index`'s walls and roof. `hi` is the ground at its highest corner (heights
// are measured from there, as in the Python game) and `base` the foot of its walls.
void add_building_details(MeshBuilder& m, const Building& b, int index, float hi, float base);

// Where a clock tower's four faces are (their middles, a hair in front, and outward normals), for the moving hands.
struct ClockFace {
    float x, y, z, nx, ny;
};
std::vector<ClockFace> clock_faces(const Building& b, float hi);

// A small window in a gable end (cape and two-storey houses): tri = the two eave corners, then the apex; n its normal.
void add_attic_window(MeshBuilder& m, const Vec3 tri[3], Vec3 n);

}  // namespace amr
