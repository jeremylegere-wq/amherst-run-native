// Jobs (game/missions.py): drive into a glowing marker at a landmark and (nearly) stop to start one.
//   deliveries (post office)  Parcel Run: deliver 1-3 parcels to real street addresses before time runs out; stop
//                             at each. Some parcels are fragile.
//   getaway (banks)           Getaway: you get three stars; lose the police before the clock runs out.
//   tour (square, museum)     Heritage Tour: a checkpoint race past the town's landmarks.
//   courthouse                Pay your fine: clears the wanted level for $100 a star.
// Jobs pay cash plus a time bonus. No job while wanted. Getting busted costs $100. The hubs and tour spots are baked
// into the map from the Python game; targets come from the road graph.
#pragma once
#include <cstdint>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "sim/roadgraph.h"
#include "sim/town.h"
#include "world/world.h"

namespace amr {

struct Rgb8 {
    std::uint8_t r, g, b;
};

struct JobTarget {
    double x, y, radius;
    std::string label;
    bool stop;
};

struct Job {
    enum Kind { Delivery, Getaway, Tour } kind;
    std::string title;
    Rgb8 colour;
    std::vector<JobTarget> targets;
    std::size_t stage = 0;
    double time_limit = 0, time_left = 0, elapsed = 0, start_damage = 0;
    int reward = 0;
    bool fragile = false;
};

struct Marker {
    double x, y, radius;
    Rgb8 colour;
    bool hub;
};

class MissionManager {
public:
    MissionManager(const World& world, const RoadGraph& graph, Town& town, unsigned seed = 1);
    void update(double dt, const Car& player);

    int cash() const { return cash_; }
    int completed() const { return completed_; }
    const Job* job() const { return job_.has_value ? &job_.value : nullptr; }
    // The banner message (empty when none) and its colour.
    const std::string& banner() const { return banner_; }
    Rgb8 banner_colour() const { return banner_colour_; }
    std::string objective() const;
    std::string hint(const Car& player) const;
    bool current_target(double& x, double& y) const;
    std::vector<Marker> markers() const;
    void abort();
    void add_cash(int amount, const std::string& banner, Rgb8 colour) {  // pay for something else (a tag)
        cash_ += amount;
        say(banner, colour);
    }

    // The pieces the parity test checks against the Python game:
    std::string address(double x, double y) const;           // real civic number within 45 m, else a made-up one
    double route_len(double ax, double ay, double bx, double by) const;
    bool make_tour(int hub, Job& out);                       // from hub index (map order); false if no tour
    const std::vector<HubSpot>& all_hubs() const { return hubs_; }

private:
    struct OptJob {
        bool has_value = false;
        Job value;
    };
    void say(const std::string& text, Rgb8 colour);
    std::vector<int> hubs_in_use() const;
    void enter_hub(int h, const Car& player);
    bool make_delivery(int h, Job& out);
    bool road_point_near(double x, double y, double rmin, double rmax, double& px, double& py);
    void check_hubs(const Car& player);
    void update_job(double dt, const Car& player);
    void complete();
    void fail(const std::string& reason);

    const World& world_;
    const RoadGraph& graph_;
    Town& town_;
    std::mt19937 rng_;
    std::vector<HubSpot> hubs_;
    int cash_ = 0, completed_ = 0;
    OptJob job_;
    std::string banner_;
    Rgb8 banner_colour_{252, 222, 48};
    double banner_left_ = 0;
    int inside_ = -1;  // the hub the player is sitting in (must leave before it triggers again)
    TownState prev_state_ = TownState::Playing;
    std::unordered_map<long long, std::vector<int>> addr_cells_;  // 50 m cell -> addresses
};

}  // namespace amr
