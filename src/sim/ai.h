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
    // at junctions (sim/junctions.h)
    int jn_node = -1;           // the junction this car is approaching
    int jn_pass = -1;           // the junction it has been cleared to drive through
    bool jn_hold = false;       // held at a line this tick (red light, stop sign, giving way)
    bool jn_stopped = false;    // has made its full stop at the stop sign
    double jn_stop_t = 0;       // seconds standing at the stop line
    double jn_wait = 0;         // seconds waiting at the line for a gap
    bool has_v_smooth = false;
    double v_smooth = 0;
    int colour = 0;             // which traffic colour (drawing)
};

double wrap_angle(double a);  // to [-pi, pi)

// Highest safe speed given the cars ahead (1e9 if clear): where each is now and where it will be in `predict` s.
double obstacle_limit(const Car& car, const std::vector<const Car*>& others, double look = -1, double width = 2.3,
                      double gap = 6.5, double gain = 0.8, double predict = 0.9);

class Junctions;

// One decision for a civilian driver: follow the right-hand lane, slow for corners, cars ahead and junction rules
// (junctions may be null: no rules). next_edge picks the edge after a given one (choose_next with the dice, or
// choose_best in tests); now is the town clock the traffic lights run on.
void drive_traffic(AICar& car, const RoadGraph& graph, const std::vector<const Car*>& others,
                   const std::function<int(int)>& next_edge, double dt, Junctions* junctions = nullptr,
                   double now = 0.0);

}  // namespace amr
