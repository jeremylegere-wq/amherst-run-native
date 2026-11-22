// The map loader and the height queries.
//   test_world                     unit checks on hand-made bytes (always run)
//   test_world MAP GOLDEN_CSV      plus parity: every sample in the Python game's golden heights.csv
//                                  (tools/golden.py in amherst-run) must come out the same here
// Exit 77 (skipped) when the map or golden file is not there (fetch them first; see README).
#include <SDL.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "world/map_file.h"
#include "world/world.h"

static int failures = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                      \
        }                                                                    \
    } while (0)

static void put32(std::vector<std::uint8_t>& b, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}

static void unit_checks() {
    amr::MapData m;
    std::string err;
    std::vector<std::uint8_t> bad = {'N', 'O', 'P', 'E', 0, 0, 0, 0, 0, 0, 0, 0};
    CHECK(!amr::parse_map(bad.data(), bad.size(), m, err));
    std::vector<std::uint8_t> v2 = {'A', 'M', 'R', '1'};
    put32(v2, 2);
    put32(v2, 0);
    CHECK(!amr::parse_map(v2.data(), v2.size(), m, err) && err.find("version 2") != std::string::npos);
    std::vector<std::uint8_t> empty = {'A', 'M', 'R', '1'};
    put32(empty, 1);
    put32(empty, 1);
    for (char c : std::string("ZZZZ")) empty.push_back(static_cast<std::uint8_t>(c));  // an unknown chunk is skipped
    put32(empty, 3);
    empty.insert(empty.end(), {1, 2, 3});
    CHECK(amr::parse_map(empty.data(), empty.size(), m, err));
    std::vector<std::uint8_t> cut(empty.begin(), empty.end() - 2);
    CHECK(!amr::parse_map(cut.data(), cut.size(), m, err));

    // a deck: 6 m above the ground along x = 0..40, ramps to 0 at its ends
    amr::Span sp;
    sp.width = 8;
    sp.points = {{-20, 0, 0}, {0, 0, 6}, {40, 0, 6}, {60, 0, 0}};
    sp.x0 = -24.3f; sp.x1 = 64.3f; sp.y0 = -4.3f; sp.y1 = 4.3f;
    CHECK(std::fabs(amr::span_height(sp, 20, 1) - 6) < 1e-5);
    CHECK(std::fabs(amr::span_height(sp, -10, 0) - 3) < 1e-5);
    CHECK(amr::span_height(sp, 20, 6) < 0);  // beside it

    // the drawn ground: corners are the grid's heights, and it is flat within each of a cell's two triangles
    amr::TerrainGrid t;
    t.x0 = 0; t.y0 = 0; t.step = 10; t.nx = 2; t.ny = 2;
    t.h = {0, 4, 2, 10};  // (0,0)=0 (1,0)=4 (0,1)=2 (1,1)=10
    CHECK(std::fabs(t.mesh_height(0, 0) - 0) < 1e-5);
    CHECK(std::fabs(t.mesh_height(10, 10) - 10) < 1e-5);
    CHECK(std::fabs(t.mesh_height(5, 5) - 5) < 1e-5);        // on the diagonal: halfway from 0 to 10
    CHECK(std::fabs(t.mesh_height(10, 0) - 4) < 1e-5);
    CHECK(std::fabs(t.mesh_height(8, 2) - (0 + 4 * 0.8f + 6 * 0.2f)) < 1e-5);   // in the (a, b, c) triangle
    CHECK(std::fabs(t.mesh_height(5, 5) - t.height(5, 5)) > 0.4);              // where it differs from bilinear
}

static int parity(const char* map_path, const char* golden_path) {
    amr::MapData m;
    std::string err;
    if (!amr::load_map(map_path, m, err)) {
        std::printf("skipped parity: %s\n", err.c_str());
        return 77;
    }
    std::ifstream f(golden_path);
    if (!f) {
        std::printf("skipped parity: no %s\n", golden_path);
        return 77;
    }
    amr::World w(std::move(m));
    std::string line;
    std::getline(f, line);  // header
    int n = 0, bad = 0;
    double worst_g = 0, worst_h = 0;
    while (std::getline(f, line)) {
        double x, y, z, g, h;
        char c;
        std::istringstream s(line);
        if (!(s >> x >> c >> y >> c >> z >> c >> g >> c >> h)) continue;
        const double dg = std::fabs(w.ground(static_cast<float>(x), static_cast<float>(y)) - g);
        const double dh = std::fabs(w.height_at(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)) - h);
        worst_g = std::fmax(worst_g, dg);
        worst_h = std::fmax(worst_h, dh);
        if (dg > 0.02 || dh > 0.03) {
            if (bad < 10) std::printf("  at %.2f, %.2f (hint %.2f): ground %.4f want %.4f, surface off by %.4f\n",
                                      x, y, z, w.ground(static_cast<float>(x), static_cast<float>(y)), g, dh);
            ++bad;
        }
        ++n;
    }
    std::printf("parity: %d samples, worst ground error %.4f m, worst surface error %.4f m, %d over tolerance\n",
                n, worst_g, worst_h, bad);
    return (n > 0 && bad == 0) ? 0 : 1;
}

int main(int argc, char** argv) {
    unit_checks();
    if (failures) return 1;
    std::printf("map loader: unit checks passed\n");
    if (argc >= 3) return parity(argv[1], argv[2]);
    return 0;
}
