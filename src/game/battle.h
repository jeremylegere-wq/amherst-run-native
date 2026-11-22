// Battle and VS race (game/battle.py): split screen, item boxes, built on split-screen free roam.
//   balloon  Balloon battle downtown: three balloons each. A shot, a slick or a hard ram pops one (then 1.5 s of
//            safety). The last car with a balloon left wins.
//   sprint   VS race: side by side at a random spot, race to a random landmark (Victoria Square, the Court House,
//            the Dominion Building, the mall, the dam, the border lighthouse) marked by a tall beacon. Items slow the
//            rival down; first within 15 m of the beacon wins.
#pragma once
#include <random>
#include <string>
#include <vector>

#include "game/items.h"
#include "sim/car.h"
#include "sim/roadgraph.h"
#include "world/world.h"

namespace amr {

struct Racer {
    int balloons = 3;
    Item item = Item::None;      // the item held
    double roulette = 0;         // seconds of roulette left
    Item shown = Item::None;     // what the roulette window shows right now
    int orbit = 0;               // triple pucks still circling
    double spin = 0, safe = 0, ghost = 0;
    bool fire_held = false, out = false;
};

class Battle {
public:
    enum class Mode { Balloon, Sprint };
    static constexpr int kBalloons = 3, kTraffic = 8;

    Battle(const World& world, const RoadGraph& graph, Mode mode, int players, unsigned seed);
    Mode mode() const { return mode_; }
    int players() const { return n_; }
    // Where player i starts: (x, y, heading).
    void start(int i, double& x, double& y, double& heading) const;
    // Before physics: a spinning, popped-out or finished player has no control.
    void adjust(int i, bool& throttle, bool& brake, double& steer, bool& handbrake, bool& bumper) const;
    // After physics: spins, ghosts, items, boxes, shots, slicks, rams, the winner. fire: the use-item button held.
    // town: the traffic (a shot breaks on a town car).
    void update(double dt, std::vector<Car*>& cars, const std::vector<bool>& fire,
                const std::vector<const Car*>& town);

    const std::vector<Racer>& racers() const { return racers_; }
    const std::vector<ItemBox>& boxes() const { return boxes_; }
    const std::vector<Shot>& shots() const { return shots_; }
    const std::vector<Slick>& slicks() const { return slicks_; }
    double time() const { return time_; }
    int winner() const { return winner_; }       // -2 still racing, -1 a draw, else the winner
    const std::string& banner() const { return banner_; }
    bool has_target() const { return mode_ == Mode::Sprint; }
    double target_x() const { return tx_; }
    double target_y() const { return ty_; }
    const std::string& target_label() const { return target_label_; }
    static constexpr double kFinish = 15.0;
    std::vector<const char*> sounds;  // effects to play (game/sfx.py names); the game takes them each frame

private:
    void use_item(int i, Car& car);
    bool hurt(int i, bool spin);
    void roulette(Racer& r, double dt);

    const World& world_;
    const RoadGraph& graph_;
    Mode mode_;
    int n_;
    std::mt19937 rng_;
    std::vector<double> sx_, sy_, sh_;  // start spots
    std::vector<Racer> racers_;
    std::vector<ItemBox> boxes_;
    std::vector<Shot> shots_;
    std::vector<Slick> slicks_;
    double time_ = 0, banner_left_ = 3.0, tx_ = 0, ty_ = 0;
    std::string banner_, target_label_;
    int winner_ = -2;
};

}  // namespace amr
