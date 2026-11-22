// Which street the car is on, and its speed limit (game/minimap.py StreetTracker): holds the last name for a moment
// across unnamed bits (junctions, short links), so the street sign does not flicker.
#pragma once
#include <string>

#include "sim/car.h"
#include "world/world.h"

namespace amr {

class StreetTracker {
public:
    void update(const World& world, const Car& car, double dt) {
        const int r = world.road_at(car.x, car.y, car.z);
        if (r >= 0) {
            const Road& road = world.map().roads[r];
            limit = road.speed_kmh > 0 ? static_cast<int>(road.speed_kmh)
                                       : (road.type == kMain ? 60 : road.type == kSide ? 50 : 30);  // DEFAULT_LIMIT
            const std::string& n = world.map().names[road.name];
            if (!n.empty()) {
                name = n;
                since_ = 0.0;
                return;
            }
        }
        since_ += dt;
        if (since_ > 1.5) {  // HOLD_SECONDS
            name.clear();
            if (r < 0) limit = 0;
        }
    }
    std::string name;
    int limit = 0;  // km/h, 0 off-road

private:
    double since_ = 0.0;
};

}  // namespace amr
