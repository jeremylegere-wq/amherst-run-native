// A freight train that runs along the real rail line now and then (game/train.py). The line and the stretch either
// side of town it runs are baked into the map. Every few minutes a train (locomotive + freight cars) drives the whole
// stretch and disappears at the far end. It is solid: cars that touch it are pushed off and slowed.
#pragma once
#include <random>
#include <vector>

#include "sim/car.h"
#include "world/map_file.h"

namespace amr {

struct TrainUnit {  // one 9 m block: the cab, the rest of the locomotive, or half a freight car
    double x, y, heading;
    enum Kind { Cab, Loco, Freight } kind;
    int idx;
};

class Train {
public:
    static constexpr double kSpeed = 16.0, kUnitLen = 9.0, kHalfW = 1.45;
    Train(const MapData& map, unsigned seed = 1, bool enabled = true);
    void update(double dt);
    const std::vector<TrainUnit>& units();           // from the front backwards (empty when no train is running)
    double push_out(Car& car);                       // shove a car out of the train; returns the impact speed
    bool active() const { return active_; }
    double wait() const { return wait_; }
    // For tests: put a train at s along the line, going dir (+1 / -1), with this many freight cars.
    void place(double s, int dir, int cars);
    void set_wait(double secs) { wait_ = secs; }  // when the next train comes
    void point_at(double s, double& x, double& y, double& heading) const;
    double lo() const { return lo_; }
    double hi() const { return hi_; }

private:
    double length() const { return (2 + 2 * cars_) * kUnitLen; }
    std::vector<GraphNode> chain_;
    std::vector<double> cum_;
    double total_ = 0, lo_ = 0, hi_ = 0, s_ = 0, wait_ = 0;
    int dir_ = 1, cars_ = 0;
    bool active_ = false, cache_ok_ = false;
    std::vector<TrainUnit> cache_;
    std::mt19937 rng_;
};

}  // namespace amr
