// The road graph and civilian drivers against the Python game's golden files (amherst-run tools/golden.py):
// paths.csv (A* routes), nearest.csv (nearest_node) and traffic.csv (drivers on fixed starts).
//   test_traffic MAP GOLDEN_DIR      (exit 77 = skipped when the files are not there)
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "sim/ai.h"
#include "sim/collision.h"
#include "sim/junctions.h"
#include "sim/roadgraph.h"
#include "sim/town.h"
#include "world/map_file.h"
#include "world/world.h"

namespace {

std::vector<std::string> split(const std::string& line, char sep) {
    std::vector<std::string> out;
    std::stringstream s(line);
    std::string cell;
    while (std::getline(s, cell, sep)) out.push_back(cell);
    return out;
}

int check_paths(const amr::RoadGraph& g, const std::string& path) {
    std::ifstream f(path);
    std::string line;
    std::getline(f, line);
    int n = 0, bad = 0;
    while (std::getline(f, line)) {
        const auto c = split(line, ',');
        if (c.size() != 3) continue;
        const std::vector<int> got = g.shortest_path(std::stoi(c[0]), std::stoi(c[1]));
        std::string s;
        for (int v : got) s += (s.empty() ? "" : " ") + std::to_string(v);
        if (got.empty()) s = "none";
        if (s != c[2]) {
            if (bad < 5) std::printf("  route %s -> %s differs\n", c[0].c_str(), c[1].c_str());
            ++bad;
        }
        ++n;
    }
    std::printf("routes: %d, %d differ\n", n, bad);
    return bad;
}

int check_nearest(const amr::RoadGraph& g, const std::string& path) {
    std::ifstream f(path);
    std::string line;
    std::getline(f, line);
    int n = 0, bad = 0;
    while (std::getline(f, line)) {
        const auto c = split(line, ',');
        if (c.size() != 5) continue;
        double h = 0;
        const bool has_h = c[3] != "none";
        if (has_h) h = std::stod(c[3]);
        const int got = g.nearest_node(std::stod(c[0]), std::stod(c[1]), std::stod(c[2]), has_h ? &h : nullptr);
        const int want = c[4] == "none" ? -1 : std::stoi(c[4]);
        if (got != want) {
            if (bad < 5) std::printf("  nearest at %s,%s: %d, want %d\n", c[0].c_str(), c[1].c_str(), got, want);
            ++bad;
        }
        ++n;
    }
    std::printf("nearest nodes: %d, %d differ\n", n, bad);
    return bad;
}

struct Row {
    int step, car;
    double pace, x, y, heading, vx, vy;
    int edge;
};

int check_traffic(const amr::World& world, const amr::RoadGraph& g, amr::Junctions& jn, const std::string& path) {
    std::ifstream f(path);
    std::string line;
    std::getline(f, line);
    std::map<std::string, std::vector<Row>> runs;
    std::vector<std::string> order;
    while (std::getline(f, line)) {
        const auto c = split(line, ',');
        if (c.size() != 13) continue;
        Row r{std::stoi(c[1]), std::stoi(c[2]), std::stod(c[3]), std::stod(c[4]), std::stod(c[5]), std::stod(c[6]),
              std::stod(c[7]), std::stod(c[8]), std::stoi(c[12])};
        if (!runs.count(c[0])) order.push_back(c[0]);
        runs[c[0]].push_back(r);
    }
    const double dt = 1.0 / 60.0;
    auto next = [&](int eid) { return g.choose_best(eid); };
    int failed = 0;
    for (const auto& name : order) {
        const auto& rows = runs[name];
        std::vector<std::unique_ptr<amr::AICar>> cars;
        std::size_t i = 0;
        for (; i < rows.size() && rows[i].step == 0; ++i) {
            const Row& r = rows[i];
            auto c = std::make_unique<amr::AICar>(r.x, r.y, r.heading);
            c->z = world.height_at(static_cast<float>(r.x), static_cast<float>(r.y), 0.0f);
            c->edge = r.edge;
            c->nxt = g.choose_best(r.edge);
            c->pace = r.pace;
            c->vx = r.vx;
            c->vy = r.vy;
            cars.push_back(std::move(c));
        }
        std::vector<const amr::Car*> others;
        for (auto& c : cars) others.push_back(c.get());
        double acc = 0.0, now = 0.0, worst = 0.0;
        int first_bad = -1;
        std::vector<std::vector<int>> got_edges(cars.size()), want_edges(cars.size());  // each car's edge sequence
        auto note = [](std::vector<int>& seq, int e) { if (seq.empty() || seq.back() != e) seq.push_back(e); };
        for (; i < rows.size();) {
            now += dt;  // game/town.py Town.update, for these cars only
            acc += dt;
            if (acc >= 0.05) {
                const double ctl = acc;
                acc = 0.0;
                for (auto& c : cars) amr::drive_traffic(*c, g, others, next, ctl, &jn, now);
            }
            for (auto& c : cars) {
                const double px = c->x, py = c->y;
                c->update(dt, amr::CarInput{c->throttle, c->brake, c->steer, false, false}, true);
                amr::move_and_slide(*c, world, px, py, false);
                amr::settle_height(*c, world, dt);
            }
            for (std::size_t a = 0; a < cars.size(); ++a)
                for (std::size_t b = a + 1; b < cars.size(); ++b)
                    if (std::fabs(cars[a]->x - cars[b]->x) <= 6.0 && std::fabs(cars[a]->y - cars[b]->y) <= 6.0)
                        amr::collide_cars(*cars[a], *cars[b]);
            for (std::size_t k = 0; k < cars.size(); ++k) {
                const auto& c = cars[k];
                const Row& r = rows[i++];
                const double d = std::hypot(c->x - r.x, c->y - r.y);
                worst = std::fmax(worst, d);
                note(got_edges[k], c->edge);
                note(want_edges[k], r.edge);
                if (first_bad < 0 && d > 0.10) first_bad = r.step;  // on/off throttle decisions amplify millimetres
            }
        }
        for (std::size_t k = 0; k < cars.size(); ++k)  // the same roads in the same order (switching a step apart is fine)
            if (got_edges[k] != want_edges[k] && first_bad < 0) first_bad = 0;
        std::printf("traffic %-20s %zu cars, %zu steps: worst position %.4f m%s\n", name.c_str(), cars.size(),
                    rows.size() / cars.size() - 1, worst, first_bad < 0 ? "" : "  <- OFF");
        if (first_bad >= 0) {
            std::printf("    first off at step %d\n", first_bad);
            ++failed;
        }
    }
    return failed;
}

// The police chases (police.csv): car 0 is the player, driving the run's script; the police chase it.
struct Script {
    double secs;
    bool th, br;
    double st;
};

int check_police(const amr::World& world, const amr::RoadGraph& g, const std::string& path) {
    struct Run {
        bool active;
        double v_cap;
        std::vector<Script> script;
    };
    // the same runs as amherst-run tools/golden.py POLICE (starts come from the file's step 0)
    const std::map<std::string, Run> spec = {
        {"box_in_a_parked_player", {true, 31.0, {{25.0, false, false, 0.0}}}},
        {"chase_a_driving_player", {true, 31.0, {{10.0, true, false, 0.0}, {3.0, true, false, 0.6}, {17.0, true, false, 0.0}}}},
        {"stand_down", {false, 26.0, {{12.0, false, false, 0.0}}}},
    };
    std::ifstream f(path);
    if (!f) {
        std::printf("police: no %s (skipped)\n", path.c_str());
        return 0;
    }
    std::string line;
    std::getline(f, line);
    std::map<std::string, std::vector<std::vector<double>>> runs;  // name -> rows (step, car, x, y, heading, vx, vy)
    std::vector<std::string> order;
    while (std::getline(f, line)) {
        const auto c = split(line, ',');
        if (c.size() != 8) continue;
        if (!runs.count(c[0])) order.push_back(c[0]);
        std::vector<double> v;
        for (std::size_t k = 1; k < c.size(); ++k) v.push_back(std::stod(c[k]));
        runs[c[0]].push_back(v);
    }
    const double dt = 1.0 / 60.0;
    int failed = 0;
    for (const auto& name : order) {
        const auto& rows = runs[name];
        const Run& run = spec.at(name);
        std::vector<std::unique_ptr<amr::Car>> cars;
        std::size_t i = 0;
        for (; i < rows.size() && rows[i][0] == 0; ++i) {
            const auto& r = rows[i];
            if (r[1] == 0) {
                auto p = std::make_unique<amr::Car>(r[2], r[3], r[4], amr::player_model(0));
                p->z = world.height_at(static_cast<float>(r[2]), static_cast<float>(r[3]),
                                       world.ground(static_cast<float>(r[2]), static_cast<float>(r[3])));
                cars.push_back(std::move(p));
            } else {
                auto c = std::make_unique<amr::AICar>(r[2], r[3], r[4], true);
                c->z = world.height_at(static_cast<float>(r[2]), static_cast<float>(r[3]), 0.0f);
                c->path_t = 0.5;
                c->vx = r[5];
                c->vy = r[6];
                cars.push_back(std::move(c));
            }
        }
        amr::Car& player = *cars[0];
        std::vector<amr::AICar*> cops;
        std::vector<const amr::Car*> cop_list;
        for (std::size_t k = 1; k < cars.size(); ++k) {
            cops.push_back(static_cast<amr::AICar*>(cars[k].get()));
            cop_list.push_back(cars[k].get());
        }
        std::vector<amr::CarInput> inputs;
        for (const Script& s : run.script)
            for (int k = 0, n = static_cast<int>(std::lround(s.secs / dt)); k < n; ++k)
                inputs.push_back(amr::CarInput{s.th, s.br, s.st, false, false});
        auto half = [] { return 0.5; };
        double acc = 0.0, worst = 0.0;
        int first_bad = -1;
        for (const amr::CarInput& in : inputs) {
            const double px = player.x, py = player.y;  // game/main.py: the player first
            player.update(dt, in, world.on_road(player.x, player.y));
            amr::move_and_slide(player, world, px, py, true);
            amr::settle_height(player, world, dt);
            acc += dt;  // then game/town.py Town.update
            if (acc >= 0.05) {
                const double ctl = acc;
                acc = 0.0;
                for (amr::AICar* c : cops)
                    amr::drive_police(*c, g, world, player, cop_list, ctl, half, run.active, run.v_cap);
            }
            for (amr::AICar* c : cops) {
                const double qx = c->x, qy = c->y;
                c->update(dt, amr::CarInput{c->throttle, c->brake, c->steer, false, false}, world.on_road(c->x, c->y));
                amr::move_and_slide(*c, world, qx, qy, false);
                amr::settle_height(*c, world, dt);
            }
            for (std::size_t a = 0; a < cars.size(); ++a)
                for (std::size_t b = a + 1; b < cars.size(); ++b)
                    if (std::fabs(cars[a]->x - cars[b]->x) <= 6.0 && std::fabs(cars[a]->y - cars[b]->y) <= 6.0)
                        amr::collide_cars(*cars[a], *cars[b]);
            for (auto& c : cars) {
                const auto& r = rows[i++];
                const double d = std::hypot(c->x - r[2], c->y - r[3]);
                worst = std::fmax(worst, d);
                if (first_bad < 0 && d > 0.10) first_bad = static_cast<int>(r[0]);
            }
        }
        std::printf("police %-24s %zu cars, %zu steps: worst position %.4f m%s\n", name.c_str(), cars.size(),
                    inputs.size(), worst, first_bad < 0 ? "" : "  <- OFF");
        if (first_bad >= 0) {
            std::printf("    first off at step %d\n", first_bad);
            ++failed;
        }
    }
    return failed;
}

// The town's rules (not parity: the town rolls its own dice).
int check_rules(const amr::World& world, const amr::RoadGraph& g, amr::Junctions& jn) {
    int bad = 0;
    auto expect = [&](bool ok, const char* what) {
        std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok) ++bad;
    };
    const double dt = 1.0 / 60.0;
    std::printf("town rules:\n");
    {  // a parked player with two stars: police come (no more than the stars), box him in, and arrest him
        amr::Car player(331.25, -761.25, 0.0, amr::player_model(0));
        player.z = world.height_at(331.25f, -761.25f, 40.0f);
        amr::Town town(world, g, &jn, 3, 0);
        town.add_heat(1.6);
        int most = 0;
        double busted_at = -1;
        for (int k = 0; k < 60 * 60 && busted_at < 0; ++k) {
            town.update(dt, player);
            most = std::max(most, town.police_count());
            if (town.state() == amr::TownState::Busted) busted_at = k * dt;
        }
        expect(town.stars() == 2 && most >= 1 && most <= 2, "two stars bring one or two police, no more");
        expect(busted_at > 0, "a parked wanted player is busted");
        std::printf("       (busted after %.1f s)\n", busted_at);
        bool reset = false;
        for (int k = 0; k < 60 * 5 && !reset; ++k) {
            town.update(dt, player);
            reset = town.consume_reset();
        }
        expect(reset, "the game is told to reset the player after the arrest");
    }
    {  // hard knocks wreck the car, and damage takes power away
        amr::Car player(331.25, -761.25, 0.0, amr::player_model(0));
        amr::Town town(world, g, &jn, 3, 0);
        town.update(dt, player, 23.0);  // (23 - 3) * 1.2 = 24 damage
        expect(std::fabs(town.damage() - 24.0) < 1e-9, "damage is (impact - 3 m/s) x 1.2");
        for (int k = 0; k < 4; ++k) town.update(dt, player, 50.0);
        expect(town.state() == amr::TownState::Wrecked, "100 damage wrecks the car");
        expect(std::fabs(town.player_power() - 0.65) < 1e-9, "a wrecked car has 65% power");
    }
    {  // cop-free mode
        amr::Car player(331.25, -761.25, 0.0, amr::player_model(0));
        amr::Town town(world, g, &jn, 3, 0);
        town.add_heat(3.0);
        for (int k = 0; k < 60 * 8; ++k) town.update(dt, player);
        const int had = town.police_count();
        town.set_cops(false);
        town.add_heat(1.0);
        expect(had > 0 && town.police_count() == 0 && town.heat() == 0.0, "cop-free mode stands the police down");
    }
    return bad;
}

// A minute of town around a player parked downtown: the cars keep their numbers up, keep moving and keep to the
// roads (not a parity check: the town rolls its own dice).
int check_town(const amr::World& world, const amr::RoadGraph& g, amr::Junctions& jn) {
    amr::Car player(394.6, -835.8, 0.0, amr::player_model(0));
    player.z = world.height_at(394.6f, -835.8f, 0.0f);
    amr::Town town(world, g, &jn, 7, 16);
    const double dt = 1.0 / 60.0;
    double cars_sum = 0, speed_sum = 0;
    long samples = 0, on_road = 0, car_samples = 0;
    for (int step = 0; step < 60 * 60; ++step) {
        town.update(dt, player);
        if (step % 30 == 0 && step > 600) {
            cars_sum += town.cars().size();
            ++samples;
            for (const auto& c : town.cars()) {
                speed_sum += c->speed();
                on_road += world.on_road(c->x, c->y) ? 1 : 0;
                ++car_samples;
            }
        }
    }
    const double avg_cars = cars_sum / samples, avg_speed = speed_sum / car_samples;
    const double road_share = static_cast<double>(on_road) / car_samples;
    std::printf("town: %.1f cars on average (target %d here), %.1f m/s average speed, %.1f%% of the time on a road\n",
                avg_cars, town.local_target(player), avg_speed, 100.0 * road_share);
    const bool ok = avg_cars >= 0.75 * town.local_target(player) && avg_speed > 4.0 && road_share > 0.97;
    if (!ok) std::printf("  <- OFF\n");
    return ok ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) return 2;
    amr::MapData m;
    std::string err;
    if (!amr::load_map(argv[1], m, err) || m.graph_nodes.empty()) {
        std::printf("skipped: %s\n", err.empty() ? "the map has no road graph (an older bake)" : err.c_str());
        return 77;
    }
    const std::string dir = argv[2];
    if (!std::ifstream(dir + "/traffic.csv")) {
        std::printf("skipped: no %s/traffic.csv\n", dir.c_str());
        return 77;
    }
    amr::RoadGraph graph(m);
    amr::Junctions junctions(m, graph);
    amr::World world(std::move(m));
    std::printf("road graph: %zu nodes, %zu edges\n", graph.nodes.size(), graph.edges.size());
    int bad = check_paths(graph, dir + "/paths.csv");
    bad += check_nearest(graph, dir + "/nearest.csv");
    bad += check_traffic(world, graph, junctions, dir + "/traffic.csv");
    bad += check_police(world, graph, dir + "/police.csv");
    bad += check_rules(world, graph, junctions);
    bad += check_town(world, graph, junctions);
    return bad ? 1 : 0;
}
