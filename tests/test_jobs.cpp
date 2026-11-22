// Jobs against the Python game's golden file (amherst-run tools/golden.py jobs.tsv): street addresses for drop
// points, route lengths (which size delivery time limits) and the Heritage Tours from each tour hub. Plus the job
// rules on the real map: a tour pays, a getaway brings the police, fines need cash.
//   test_jobs MAP GOLDEN_DIR      (exit 77 = skipped when the files are not there)
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "game/missions.h"
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
}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) return 2;
    amr::MapData m;
    std::string err;
    if (!amr::load_map(argv[1], m, err) || m.hubs.empty()) {
        std::printf("skipped: %s\n", err.empty() ? "the map has no job hubs (an older bake)" : err.c_str());
        return 77;
    }
    std::ifstream f(std::string(argv[2]) + "/jobs.tsv");
    if (!f) {
        std::printf("skipped: no jobs.tsv\n");
        return 77;
    }
    amr::RoadGraph graph(m);
    amr::Junctions junctions(m, graph);
    amr::World world(std::move(m));
    amr::Town town(world, graph, &junctions, 1, 0);
    amr::MissionManager jobs(world, graph, town);
    int addr_n = 0, addr_bad = 0, route_n = 0, route_bad = 0, tour_n = 0, tour_bad = 0;
    std::string line;
    while (std::getline(f, line)) {
        const auto c = split(line, '\t');
        if (c[0] == "address" && c.size() == 4) {
            const std::string got = jobs.address(std::stod(c[1]), std::stod(c[2]));
            if (got != c[3]) {
                if (addr_bad < 5) std::printf("  address at %s,%s: '%s', want '%s'\n", c[1].c_str(), c[2].c_str(), got.c_str(), c[3].c_str());
                ++addr_bad;
            }
            ++addr_n;
        } else if (c[0] == "route" && c.size() == 6) {
            const double got = jobs.route_len(std::stod(c[1]), std::stod(c[2]), std::stod(c[3]), std::stod(c[4]));
            if (std::fabs(got - std::stod(c[5])) > 0.01) {
                if (route_bad < 5) std::printf("  route: %.4f, want %s\n", got, c[5].c_str());
                ++route_bad;
            }
            ++route_n;
        } else if (c[0] == "tour" && c.size() == 5) {
            int hub = -1;
            for (int i = 0; i < static_cast<int>(jobs.all_hubs().size()); ++i)
                if (jobs.all_hubs()[i].role == 2 && jobs.all_hubs()[i].label == c[1]) hub = i;
            amr::Job j;
            bool ok = hub >= 0 && jobs.make_tour(hub, j);
            const auto want = split(c[2], '|');
            ok = ok && want.size() == j.targets.size() && std::fabs(j.time_limit - std::stod(c[3])) < 0.01 &&
                 j.reward == std::stoi(c[4]);
            for (std::size_t k = 0; ok && k < want.size(); ++k) {
                const auto t = split(want[k], '@');
                ok = t[0] == j.targets[k].label && std::fabs(std::stod(t[1]) - j.targets[k].x) < 1e-3 &&
                     std::fabs(std::stod(t[2]) - j.targets[k].y) < 1e-3;
            }
            if (!ok) {
                std::printf("  tour from %s differs\n", c[1].c_str());
                ++tour_bad;
            }
            ++tour_n;
        }
    }
    std::printf("addresses: %d, %d differ\nroutes: %d, %d differ\ntours: %d, %d differ\n", addr_n, addr_bad, route_n,
                route_bad, tour_n, tour_bad);
    int bad = addr_bad + route_bad + tour_bad;

    // the job rules: drive into a hub and stop, finish a tour by visiting its checkpoints
    auto run_into = [&](int role) {
        for (int i = 0; i < static_cast<int>(jobs.all_hubs().size()); ++i)
            if (jobs.all_hubs()[i].role == role) return i;
        return -1;
    };
    const double dt = 1.0 / 60.0;
    const int tour_hub = run_into(2);
    const amr::HubSpot& th = jobs.all_hubs()[tour_hub];
    amr::Car player(th.x, th.y, 0.0, amr::player_model(0));
    jobs.update(dt, player);
    const bool started = jobs.job() && jobs.job()->kind == amr::Job::Tour;
    int stops = 0;
    while (jobs.job() && stops < 10) {  // "drive" to each checkpoint in turn
        double x, y;
        if (!jobs.current_target(x, y)) break;
        player.x = x;
        player.y = y;
        jobs.update(dt, player);
        ++stops;
    }
    const bool paid = !jobs.job() && jobs.cash() > 0 && jobs.completed() == 1;
    std::printf("job rules:\n  %s stopping in a tour hub starts a Heritage Tour\n  %s visiting its checkpoints pays $%d\n",
                started ? "ok  " : "FAIL", paid ? "ok  " : "FAIL", jobs.cash());
    bad += !started + !paid;
    const int bank = run_into(1);
    player.x = jobs.all_hubs()[bank].x;
    player.y = jobs.all_hubs()[bank].y;
    jobs.update(dt, player);
    const bool getaway = jobs.job() && jobs.job()->kind == amr::Job::Getaway && town.stars() == 3;
    std::printf("  %s a bank starts a Getaway with three stars\n", getaway ? "ok  " : "FAIL");
    bad += !getaway;
    return bad ? 1 : 0;
}
