// Height queries on the baked map: the ground, and overpass decks over it (game/world.py height_at / sp_height).
#pragma once
#include <cmath>
#include <unordered_map>
#include <vector>

#include "world/map_file.h"

namespace amr {

constexpr float kClearance = 4.0f;  // a deck lower than this is a ramp: it IS the surface (world.py CLEARANCE)

class World {
public:
    explicit World(MapData data);
    const MapData& map() const { return map_; }

    float ground(float x, float y) const { return map_.terrain.height(x, y); }
    // The surface under (x, y): the ground, or a deck/ramp above it, whichever is nearest z_hint.
    float height_at(float x, float y, float z_hint) const;
    bool in_bounds(double x, double y, double margin = 0) const {
        return x >= map_.x0 - margin && x <= map_.x1 + margin && y >= map_.y0 - margin && y <= map_.y1 + margin;
    }
    // Within any road's width (decks included, like world.py), or on an open parking lot.
    bool on_road(double x, double y) const;
    bool in_lot(double x, double y) const;
    bool in_building(double x, double y) const;
    // Unit normal of the ground at (x, y), sampled half a grid step either side (terrain.py Terrain.normal).
    void ground_normal(double x, double y, double& nx, double& ny, double& nz) const;

private:
    struct Seg {
        float ax, ay, bx, by, w;
    };
    MapData map_;
    std::unordered_map<long long, std::vector<int>> span_cells_;      // 40 m cell -> spans touching it
    std::unordered_map<long long, std::vector<Seg>> road_cells_;      // 40 m cell -> road segments touching it
    std::unordered_map<long long, std::vector<int>> building_cells_;  // 40 m cell -> buildings whose box touches it
    std::unordered_map<long long, std::vector<int>> lot_cells_;       // 40 m cell -> parking lot polygons
};

// Ray-casting point-in-polygon (world.py point_in_polygon).
bool point_in_polygon(double x, double y, const std::vector<Point2>& pts);

// Deck height of one span at (x, y) above the ground, or a negative number when (x, y) is off it.
float span_height(const Span& sp, float x, float y);

}  // namespace amr
