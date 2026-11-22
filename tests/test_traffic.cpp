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

int check_traffic(const amr::World& world, const amr::RoadGraph& g, const std::string& path) {
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
        double acc = 0.0, worst = 0.0;
        int first_bad = -1;
        std::vector<std::vector<int>> got_edges(cars.size()), want_edges(cars.size());  // each car's edge sequence
        auto note = [](std::vector<int>& seq, int e) { if (seq.empty() || seq.back() != e) seq.push_back(e); };
        for (; i < rows.size();) {
            acc += dt;  // game/town.py Town.update, for these cars only
            if (acc >= 0.05) {
                const double ctl = acc;
                acc = 0.0;
                for (auto& c : cars) amr::drive_traffic(*c, g, others, next, ctl);
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
                if (first_bad < 0 && d > 0.05) first_bad = r.step;  // on/off throttle decisions amplify millimetres
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

// A minute of town around a player parked downtown: the cars keep their numbers up, keep moving and keep to the
// roads (not a parity check: the town rolls its own dice).
int check_town(const amr::World& world, const amr::RoadGraph& g) {
    amr::Car player(394.6, -835.8, 0.0, amr::player_model(0));
    player.z = world.height_at(394.6f, -835.8f, 0.0f);
    amr::Town town(world, g, 7, 16);
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
    amr::World world(std::move(m));
    std::printf("road graph: %zu nodes, %zu edges\n", graph.nodes.size(), graph.edges.size());
    int bad = check_paths(graph, dir + "/paths.csv");
    bad += check_nearest(graph, dir + "/nearest.csv");
    bad += check_traffic(world, graph, dir + "/traffic.csv");
    bad += check_town(world, graph);
    return bad ? 1 : 0;
}
