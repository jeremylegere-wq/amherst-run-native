// Tagging: spots on walls around town that the player sprays with their own tag. Pull up beside a marked wall
// (the wall on that side, under 60 km/h) and hold TAG for about a second; the tag appears on the wall, pays $25,
// and spraying with police nearby raises the wanted level. This game's own design (not in the Python game).
//
// The spots are worked out from the map when the game starts: long walls of commercial and downtown buildings (not
// homes or landmarks) that face a street close by, spread at least 70 m apart, always the same ones.
#pragma once
#include <vector>

#include "render/mesh.h"
#include "sim/car.h"
#include "world/world.h"

namespace amr {

struct TagSpot {
    double x = 0, y = 0;    // the middle of the tag, on the wall's face
    double nx = 0, ny = 0;  // the wall's outward normal
    double width = 5;       // how wide the tag is
    double base = 0;        // the height the wall's heights are measured from (the building's highest corner)
    double wall_h = 6;      // the wall's height
    int building = -1;
    bool tagged = false;
};

class Tagging {
public:
    static constexpr double kReach = 11.0;          // metres out from the wall the car can spray from
    static constexpr double kMaxSpeed = 60.0 / 3.6; // faster than this, no tagging
    static constexpr double kSprayTime = 0.9;       // seconds of holding TAG
    static constexpr int kPay = 25;

    explicit Tagging(const World& world, int count = 40);
    // One step. `holding`: the TAG button. Returns the spot just finished (or -1).
    int update(double dt, const Car& car, bool holding);
    int in_reach() const { return reach_; }         // the untagged spot the car could spray now (-1: none)
    double progress() const { return progress_ / kSprayTime; }  // 0..1 while spraying
    const std::vector<TagSpot>& spots() const { return spots_; }
    int tagged() const { return tagged_; }
    // A saved tag: mark the untagged spot within 2 m of (x, y) as done; its index, or -1 (the map moved on).
    int mark_tagged(double x, double y);

private:
    int find_reach(const Car& car) const;
    std::vector<TagSpot> spots_;
    int reach_ = -1, tagged_ = 0;
    double progress_ = 0;
};

// The tag sprayed on spot s, as a mesh in world space: an ink-edged back plate and 3-5 bubble letters with
// highlights, in one of the crew palettes. `seed` picks the letters' shapes; the same seed, the same tag.
MeshBuilder tag_mesh(const TagSpot& s, unsigned seed);

}  // namespace amr
