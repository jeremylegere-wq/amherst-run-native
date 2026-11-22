// The living town (game/town.py Town): civilian traffic, police, the wanted level and player damage.
//   * Hitting a civilian car adds heat (0.6 per hit). Heat 0..4 decides how many police chase (1..4).
//   * Police spawn on roads 70-120 m away, preferring the side behind the player.
//   * Stay out of sight for a few seconds and the heat drains; at zero the police stand down.
//   * Police within 8 m of a (nearly) stopped player for 2.5 s make a BUSTED arrest.
//   * Damage builds up from impacts and slows the car down; at 100 the car is WRECKED.
//   * After BUSTED or WRECKED the game resets the player (consume_reset()).
// Traffic is kept around the player, spawned out of sight on busier roads, removed when far away or stuck, and
// obeys the junction rules on the town clock. Everyone drives the same physics as the player and bumps into it.
#pragma once
#include <memory>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "sim/ai.h"
#include "sim/junctions.h"
#include "sim/roadgraph.h"
#include "world/world.h"

namespace amr {

enum class TownState { Playing, Busted, Wrecked };

class Town {
public:
    Town(const World& world, const RoadGraph& graph, Junctions* junctions, unsigned seed = 1, int traffic = 16,
         bool cops = true);
    // One 60 Hz step after the player's own (game/main.py: player physics, then town.update). wall_impact: how
    // hard the player hit a wall or tree this step (m/s), for damage.
    void update(double dt, Car& player, double wall_impact = 0.0);
    void reset();                          // clear everything (after a reset of the player)
    bool consume_reset();                  // true once after BUSTED or WRECKED has played out: reset the player

    const std::vector<std::unique_ptr<AICar>>& cars() const { return cars_; }
    int local_target(const Car& player);   // how many civilian cars to keep around: fewer where there is little road
    void add_heat(double amount);
    void set_cops(bool on);                // cop-free mode off/on (off stands the police down at once)
    double player_power() const;           // 1.0 undamaged, down to 0.65 at 100 damage
    int stars() const;
    double heat() const { return heat_; }
    bool cops() const { return cops_; }
    double damage() const { return damage_; }
    double time() const { return time_; }  // the town clock (the traffic lights run on it)
    TownState state() const { return state_; }
    bool seen() const { return seen_; }
    int police_count() const;
    std::vector<std::string> events;       // "hit", "busted", "wrecked" (the last 20)

private:
    void manage(const Car& player, bool fill);
    void control(double dt, const Car& player);
    bool spawn_traffic(const Car& player, double rmin, double rmax);
    bool spawn_police(const Car& player);
    bool random_spawn(const Car& player, double rmin, double rmax, const double weights[3], int& eid, double& s);
    bool occupied(double x, double y, const Car& player) const;
    void hurt(double impact);
    void on_crash(const Car* a, const Car* b, double impact, const Car& player);
    void update_state(double dt, const Car& player);
    void end(TownState s, const char* event);

    const World& world_;
    const RoadGraph& graph_;
    Junctions* junctions_;
    std::mt19937 rng_;
    int target_;
    bool cops_;
    std::vector<std::unique_ptr<AICar>> cars_;
    std::unordered_map<long long, double> density_;  // 100 m cell -> how busy its streets are
    double time_ = 0, heat_ = 0, damage_ = 0, state_t_ = 0, busted_t_ = 0, hit_cd_ = 0, spawn_cd_ = 0, lose_t_ = 0;
    double mng_acc_ = 0, ctl_acc_ = 0;
    TownState state_ = TownState::Playing;
    bool seen_ = false, filled_ = false, reset_flag_ = false;
};

}  // namespace amr
