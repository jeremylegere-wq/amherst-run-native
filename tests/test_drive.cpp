// Car physics and collisions against the Python game's golden drives (amherst-run tools/golden.py drive.csv):
// the same inputs from the same start must give the same path.
//   test_drive MAP DRIVE_CSV      (exit 77 = skipped when the files are not there)
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "sim/car.h"
#include "sim/collision.h"
#include "world/map_file.h"
#include "world/world.h"

namespace {

struct Row {
    int step;
    amr::CarInput in;
    double x, y, z, heading, vx, vy;
};

constexpr double kDt = 1.0 / 60.0;
constexpr double kPosTol = 0.02;     // metres (measured: under 1 mm)
constexpr double kHeadTol = 0.001;   // radians
constexpr double kSpeedTol = 0.02;   // m/s

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::printf("usage: test_drive MAP DRIVE_CSV\n");
        return 2;
    }
    amr::MapData m;
    std::string err;
    if (!amr::load_map(argv[1], m, err)) {
        std::printf("skipped: %s\n", err.c_str());
        return 77;
    }
    std::ifstream f(argv[2]);
    if (!f) {
        std::printf("skipped: no %s\n", argv[2]);
        return 77;
    }
    amr::World world(std::move(m));
    std::map<std::string, std::vector<Row>> runs;
    std::vector<std::string> order;
    std::string line;
    std::getline(f, line);
    while (std::getline(f, line)) {
        std::istringstream s(line);
        std::string name, cell;
        std::getline(s, name, ',');
        std::vector<double> v;
        while (std::getline(s, cell, ',')) v.push_back(std::stod(cell));
        if (v.size() != 12) continue;
        Row r;
        r.step = static_cast<int>(v[0]);
        r.in.throttle = v[1] != 0; r.in.brake = v[2] != 0; r.in.steer = v[3];
        r.in.handbrake = v[4] != 0; r.in.bumper = v[5] != 0;
        r.x = v[6]; r.y = v[7]; r.z = v[8]; r.heading = v[9]; r.vx = v[10]; r.vy = v[11];
        if (!runs.count(name)) order.push_back(name);
        runs[name].push_back(r);
    }
    int failed = 0;
    for (const auto& name : order) {
        const auto& rows = runs[name];
        const Row& s0 = rows.front();
        amr::Car car(s0.x, s0.y, s0.heading, amr::player_model(0));
        car.z = s0.z;
        double worst_pos = 0, worst_head = 0, worst_speed = 0, worst_z = 0;
        int first_bad = -1;
        for (std::size_t i = 1; i < rows.size(); ++i) {
            const Row& r = rows[i];
            const double px = car.x, py = car.y;  // exactly as game/main.py steps the player
            car.update(kDt, r.in, world.on_road(car.x, car.y));
            amr::move_and_slide(car, world, px, py, true);
            amr::settle_height(car, world, kDt);
            const double dp = std::hypot(car.x - r.x, car.y - r.y), dh = std::fabs(car.heading - r.heading);
            const double ds = std::hypot(car.vx - r.vx, car.vy - r.vy), dz = std::fabs(car.z - r.z);
            worst_pos = std::fmax(worst_pos, dp);
            worst_head = std::fmax(worst_head, dh);
            worst_speed = std::fmax(worst_speed, ds);
            worst_z = std::fmax(worst_z, dz);
            if (first_bad < 0 && (dp > kPosTol || dh > kHeadTol || ds > kSpeedTol)) first_bad = r.step;
        }
        std::printf("%-30s %4zu steps: worst position %.4f m, heading %.5f rad, velocity %.4f m/s, height %.4f m%s\n",
                    name.c_str(), rows.size() - 1, worst_pos, worst_head, worst_speed, worst_z,
                    first_bad < 0 ? "" : "  <- OFF");
        if (first_bad >= 0) {
            std::printf("    first off at step %d\n", first_bad);
            ++failed;
        }
    }
    return failed ? 1 : 0;
}
