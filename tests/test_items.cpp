// Battle items and rules. Against the Python game's golden file (amherst-run tools/golden.py items.tsv): the item box
// spots downtown and the flight of a puck (bouncing off buildings) and a seeker (homing on a rival) step by step.
// Then the battle rules on the real map: shots and rams pop balloons, the last car with balloons wins, a slick spins
// you out, a VS race is won at the beacon.
//   test_items MAP GOLDEN_DIR      (exit 77 = skipped when the files are not there)
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "game/battle.h"
#include "game/items.h"
#include "sim/roadgraph.h"
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
int bad = 0;
void expect(bool ok, const char* what) {
    std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++bad;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) return 2;
    amr::MapData m;
    std::string err;
    if (!amr::load_map(argv[1], m, err) || m.graph_nodes.empty()) {
        std::printf("skipped: %s\n", err.empty() ? "the map has no road graph" : err.c_str());
        return 77;
    }
    std::ifstream f(std::string(argv[2]) + "/items.tsv");
    if (!f) {
        std::printf("skipped: no items.tsv\n");
        return 77;
    }
    amr::RoadGraph graph(m);
    amr::World world(std::move(m));

    // golden: box spots, and shots
    std::vector<std::pair<double, double>> want_boxes;
    std::map<std::string, std::vector<std::vector<std::string>>> shots;
    std::vector<std::string> order;
    std::string line;
    while (std::getline(f, line)) {
        const auto c = split(line, '\t');
        if (c[0] == "box") want_boxes.push_back({std::stod(c[1]), std::stod(c[2])});
        if (c[0] == "shot") {
            if (!shots.count(c[1])) order.push_back(c[1]);
            shots[c[1]].push_back(c);
        }
    }
    const auto boxes = amr::box_sites(graph, 416.0, -880.0, 650.0);
    bool same = boxes.size() == want_boxes.size();
    for (std::size_t k = 0; same && k < boxes.size(); ++k)
        same = std::fabs(boxes[k].x - want_boxes[k].first) < 1e-3 && std::fabs(boxes[k].y - want_boxes[k].second) < 1e-3;
    std::printf("item boxes: %zu, %s\n", boxes.size(), same ? "the same spots" : "DIFFER");
    if (!same) ++bad;
    struct Spec {
        amr::Item kind;
        double x, y, h;
        bool rival;
        double rx, ry;
    };
    const std::map<std::string, Spec> spec = {  // the same as golden.py SHOTS
        {"puck_off_the_walls", {amr::Item::Puck, 395.0, -836.0, 0.3, false, 0, 0}},
        {"puck_down_the_street", {amr::Item::Puck, 331.25, -761.25, 2.36, false, 0, 0}},
        {"seeker_after_a_rival", {amr::Item::Seeker, 331.25, -761.25, 2.0, true, 300.0, -730.0}},
    };
    for (const auto& name : order) {
        const Spec& sp = spec.at(name);
        amr::Car car(sp.x, sp.y, sp.h, amr::player_model(0));
        amr::Car rival(sp.rx, sp.ry, 0.0, amr::player_model(1));
        amr::Shot s = amr::fire_shot(sp.kind, car, 0);
        std::vector<std::pair<int, const amr::Car*>> rivals;
        if (sp.rival) rivals.push_back({1, &rival});
        double worst = 0;
        bool ok = true;
        for (const auto& row : shots[name]) {
            const int res = amr::step_shot(s, world, 1.0 / 60.0, rivals);
            worst = std::fmax(worst, std::hypot(s.x - std::stod(row[3]), s.y - std::stod(row[4])));
            const std::string got = res < 0 ? "gone" : (res > 0 ? "hit" : "fly");
            if (got != row[5]) ok = false;
        }
        ok = ok && worst < 1e-3;
        std::printf("shot %-22s %zu steps, worst %.6f m, %s (%d bounces)\n", name.c_str(), shots[name].size(), worst,
                    ok ? "same ending" : "DIFFERS", s.bounces);
        if (!ok) ++bad;
    }

    // the battle rules
    std::printf("battle rules:\n");
    const double dt = 1.0 / 60.0;
    {
        amr::Battle b(world, graph, amr::Battle::Mode::Balloon, 2, 5);
        std::vector<amr::Car> cars;
        for (int i = 0; i < 2; ++i) {
            double x, y, h;
            b.start(i, x, y, h);
            cars.emplace_back(x, y, h, amr::player_model(i));
        }
        std::vector<amr::Car*> ptr{&cars[0], &cars[1]};
        const double d = std::hypot(cars[0].x - cars[1].x, cars[0].y - cars[1].y);
        expect(d > 140.0 && b.boxes().size() > 10, "balloon battle: two starts 140 m+ apart downtown, item boxes about");
        // a puck fired point blank pops a balloon and spins the rival out
        cars[0].x = cars[1].x - 6.0 * std::cos(cars[0].heading);
        cars[0].y = cars[1].y - 6.0 * std::sin(cars[0].heading);
        cars[1].heading = cars[0].heading;
        amr::Racer& r0 = const_cast<amr::Racer&>(b.racers()[0]);
        r0.item = amr::Item::Puck;
        b.update(dt, ptr, {true, false}, {});
        for (int k = 0; k < 30; ++k) b.update(dt, ptr, {false, false}, {});
        expect(b.racers()[1].balloons == 2 && b.racers()[1].spin > 0.0, "a puck pops a balloon and spins the rival out");
        // a hard ram pops another (after the 1.5 s of safety)
        for (int k = 0; k < 120; ++k) b.update(dt, ptr, {false, false}, {});
        cars[0].vx = std::cos(cars[0].heading) * 14.0;
        cars[0].vy = std::sin(cars[0].heading) * 14.0;
        cars[0].x = cars[1].x - 3.0 * std::cos(cars[0].heading);
        cars[0].y = cars[1].y - 3.0 * std::sin(cars[0].heading);
        cars[1].vx = cars[1].vy = 0;
        b.update(dt, ptr, {false, false}, {});
        expect(b.racers()[1].balloons == 1, "a ram at 14 m/s pops a balloon");
        // an oil slick right under the rival
        for (int k = 0; k < 120; ++k) b.update(dt, ptr, {false, false}, {});
        cars[0].x = cars[1].x + 3.4 * std::cos(cars[0].heading);
        cars[0].y = cars[1].y + 3.4 * std::sin(cars[0].heading);
        cars[0].vx = cars[0].vy = 0;
        r0.item = amr::Item::Slick;
        r0.fire_held = false;
        b.update(dt, ptr, {true, false}, {});
        expect(b.racers()[1].out && b.winner() == 0, "the last balloon popped: player 1 wins");
    }
    {
        amr::Battle b(world, graph, amr::Battle::Mode::Sprint, 2, 9);
        std::vector<amr::Car> cars;
        for (int i = 0; i < 2; ++i) {
            double x, y, h;
            b.start(i, x, y, h);
            cars.emplace_back(x, y, h, amr::player_model(i));
        }
        std::vector<amr::Car*> ptr{&cars[0], &cars[1]};
        const double d = std::hypot(cars[0].x - b.target_x(), cars[0].y - b.target_y());
        expect(d > 1100.0 && d < 2700.0 && std::hypot(cars[0].x - cars[1].x, cars[0].y - cars[1].y) < 4.0,
               "VS race: side by side, 1.2-2.6 km from a landmark");
        std::printf("       (race to %s, %.0f m)\n", b.target_label().c_str(), d);
        cars[1].x = b.target_x() + 10.0;
        cars[1].y = b.target_y();
        b.update(dt, ptr, {false, false}, {});
        expect(b.winner() == 1, "first within 15 m of the beacon wins");
    }
    return bad ? 1 : 0;
}
