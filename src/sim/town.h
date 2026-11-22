// The living town, traffic part (game/town.py Town): civilian cars kept around the player, spawned out of sight on
// busier roads, removed when far away or stuck, driven by sim/ai.h at 20 Hz on the same physics as the player, and
// bumping into each other and the player, obeying the junction rules (sim/junctions.h) on the town clock. Police and
// the wanted level come later.
#pragma once
#include <memory>
#include <random>
#include <unordered_map>
#include <vector>

#include "sim/ai.h"
#include "sim/junctions.h"
#include "sim/roadgraph.h"
#include "world/world.h"

namespace amr {

class Town {
public:
    Town(const World& world, const RoadGraph& graph, Junctions* junctions, unsigned seed = 1, int traffic = 16);
    double time() const { return time_; }  // the town clock (the traffic lights run on it)
    // One 60 Hz step after the player's own (game/main.py: player physics, then town.update).
    void update(double dt, Car& player);
    const std::vector<std::unique_ptr<AICar>>& cars() const { return cars_; }
    int local_target(const Car& player);  // how many cars to keep around: fewer where there is little road

private:
    void manage(const Car& player, bool fill);
    bool spawn_traffic(const Car& player, double rmin, double rmax);
    bool occupied(double x, double y, const Car& player) const;

    const World& world_;
    const RoadGraph& graph_;
    Junctions* junctions_;
    double time_ = 0;
    std::mt19937 rng_;
    int target_;
    std::vector<std::unique_ptr<AICar>> cars_;
    std::unordered_map<long long, double> density_;  // 100 m cell -> how busy its streets are
    double mng_acc_ = 0, ctl_acc_ = 0;
    bool filled_ = false;
};

}  // namespace amr
