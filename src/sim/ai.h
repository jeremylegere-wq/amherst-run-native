// AI drivers (game/ai.py): civilian traffic that follows the road graph. They drive the same Car physics as the
// player and only set three controls (throttle, brake, steer), about 20 times a second. Police come later.
#pragma once
#include <functional>
#include <vector>

#include "sim/car.h"
#include "sim/roadgraph.h"

namespace amr {

class AICar : public Car {
public:
    AICar(double x, double y, double heading, bool police = false);
    bool police;
    bool throttle = false, brake = false;
    double steer = 0;
    int edge = -1, nxt = -1;    // traffic: the edge it is on and the one after
    double pace = 1.0;          // traffic: fraction of the posted limit it aims for
    double stuck = 0, wait = 0;
    bool dead = false;          // flagged for removal
    bool jn_hold = false;       // held at a junction line this tick (junction rules come later)
    bool has_v_smooth = false;
    double v_smooth = 0;
    int colour = 0;             // which traffic colour (drawing)
};

double wrap_angle(double a);  // to [-pi, pi)

// Highest safe speed given the cars ahead (1e9 if clear): where each is now and where it will be in `predict` s.
double obstacle_limit(const Car& car, const std::vector<const Car*>& others, double look = -1, double width = 2.3,
                      double gap = 6.5, double gain = 0.8, double predict = 0.9);

// One decision for a civilian driver: follow the right-hand lane, slow for corners and cars ahead.
// next_edge picks the edge after a given one (choose_next with the dice, or choose_best in tests).
void drive_traffic(AICar& car, const RoadGraph& graph, const std::vector<const Car*>& others,
                   const std::function<int(int)>& next_edge, double dt);

}  // namespace amr
