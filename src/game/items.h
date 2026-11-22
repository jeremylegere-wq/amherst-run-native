// Battle items (game/items.py): item boxes at junctions, the items they hand out, and the shots and slicks they leave.
//   puck    a flat disc fired straight ahead; skims along the road and bounces off buildings up to 4 times
//   seeker  a homing shot that turns towards the nearest rival; breaks on the first wall it touches
//   slick   an oil slick dropped behind the car; whoever drives over it spins out
//   turbo   a 2 second burst of speed (ramming the rival while it lasts pops one of their balloons)
//   ghost   6 seconds untouchable: shots and slicks pass through, top speed up, touching the rival pops a balloon
//   triple  three pucks circling the car: they block shots coming in, and fire one at a time
// All names and looks are this game's own.
#pragma once
#include <cstdint>
#include <random>
#include <utility>
#include <vector>

#include "sim/car.h"
#include "sim/roadgraph.h"
#include "world/world.h"

namespace amr {

enum class Item { None = -1, Puck = 0, Seeker, Slick, Turbo, Ghost, Triple };
constexpr int kItemCount = 6;
const char* item_name(Item k);                // "PUCK", "OIL SLICK"...
const std::uint8_t* item_colour(Item k);

constexpr double kRouletteTime = 1.2, kBoxRespawn = 8.0, kBoxReach = 2.6, kBoxSpacing = 70.0;
constexpr double kOwnerSafe = 0.5, kSlickRadius = 1.7, kSlickLife = 45.0, kSlickSafe = 1.0;
constexpr double kOrbitRadius = 2.6, kOrbitSpin = 4.0, kOrbitBlock = 1.3;

Item pick_item(std::mt19937& rng);            // weighted: ghost and triple are rarer

struct ItemBox {
    double x, y, wait = 0;                    // wait: seconds until it is back (0 = there)
    bool present() const { return wait <= 0.0; }
};
// Junctions (3+ roads leave) within `radius` of the centre, at least `spacing` apart, nearest first.
std::vector<ItemBox> box_sites(const RoadGraph& g, double cx, double cy, double radius, double spacing = kBoxSpacing,
                               std::size_t limit = 80);

struct Shot {
    Item kind;
    double x, y, vx, vy, life, age = 0;
    int owner, bounces = 0;
};
struct Slick {
    double x, y;
    int owner;
    double life = kSlickLife, age = 0;
};

Shot fire_shot(Item kind, const Car& car, int owner);   // from just in front of the nose, along the heading
Slick drop_slick(const Car& car, int owner);            // just behind the car
std::vector<std::pair<double, double>> orbit_points(const Car& car, int count, double t);

// Move a shot one step. rivals = (player, car) it may home on or hit. Returns 0 fly, -1 gone (worn out or broke on
// a wall), or 1 + the player index it hit.
int step_shot(Shot& s, const World& world, double dt, const std::vector<std::pair<int, const Car*>>& rivals);

}  // namespace amr
