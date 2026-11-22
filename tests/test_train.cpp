// The train against the Python game's golden file (amherst-run tools/golden.py train.tsv): where its blocks are for
// fixed train positions, and how it shoves cars out of the way. Then its timetable: the first train comes within
// 50-90 s, runs the whole stretch and leaves.
//   test_train MAP GOLDEN_DIR      (exit 77 = skipped when the files are not there)
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "sim/car.h"
#include "sim/train.h"
#include "world/map_file.h"

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
    if (!amr::load_map(argv[1], m, err) || m.train_line.empty()) {
        std::printf("skipped: %s\n", err.empty() ? "the map has no train line" : err.c_str());
        return 77;
    }
    std::ifstream f(std::string(argv[2]) + "/train.tsv");
    if (!f) {
        std::printf("skipped: no train.tsv\n");
        return 77;
    }
    amr::Train train(m);
    int units = 0, unit_bad = 0, pushes = 0, push_bad = 0, hits = 0;
    std::string line;
    const char* kinds[3] = {"cab", "loco", "car"};
    while (std::getline(f, line)) {
        const auto c = split(line, '\t');
        train.place(std::stod(c[1]), std::stoi(c[2]), std::stoi(c[3]));
        if (c[0] == "unit") {
            bool ok = false;
            for (const amr::TrainUnit& u : train.units())
                if (u.idx == std::stoi(c[4]))
                    ok = std::fabs(u.x - std::stod(c[5])) < 1e-3 && std::fabs(u.y - std::stod(c[6])) < 1e-3 &&
                         std::fabs(std::remainder(u.heading - std::stod(c[7]), 6.283185307179586)) < 1e-5 &&
                         c[8] == kinds[u.kind];
            if (!ok) ++unit_bad;
            ++units;
        } else if (c[0] == "push") {
            amr::Car car(std::stod(c[4]), std::stod(c[5]), 0.3, amr::player_model(0));
            car.vx = 5.0;
            car.vy = -2.0;
            const double hit = train.push_out(car);
            const bool ok = std::fabs(car.x - std::stod(c[6])) < 1e-3 && std::fabs(car.y - std::stod(c[7])) < 1e-3 &&
                            std::fabs(car.vx - std::stod(c[8])) < 1e-4 && std::fabs(hit - std::stod(c[10])) < 1e-4;
            if (!ok) ++push_bad;
            if (hit > 0) ++hits;
            ++pushes;
        }
    }
    std::printf("train blocks: %d, %d differ\npush-outs: %d (%d hit), %d differ\n", units, unit_bad, pushes, hits, push_bad);
    int bad = unit_bad + push_bad;
    amr::Train t(m, 3);  // the timetable
    double first = -1, gone = -1;
    for (int k = 0; k < 60 * 60 * 6 && gone < 0; ++k) {
        t.update(1.0 / 60.0);
        if (first < 0 && t.active()) first = k / 60.0;
        if (first >= 0 && !t.active()) gone = k / 60.0;
    }
    const double run = (t.hi() - t.lo()) / amr::Train::kSpeed;
    std::printf("timetable: first train at %.1f s, gone %.1f s later (the stretch takes %.1f s at 16 m/s)\n", first,
                gone - first, run);
    const bool ok = first >= 50 && first <= 90 && gone - first > run && gone - first < run + 25 && t.wait() >= 240;
    std::printf("  %s the first train comes within 50-90 s, runs the stretch and leaves; the next waits 4-7 min\n",
                ok ? "ok  " : "FAIL");
    bad += !ok;
    return bad ? 1 : 0;
}
