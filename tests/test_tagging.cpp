// Tagging (game/tagging.h) on the real map: the spots are on commercial walls that face a street and are spread
// out; a car stopped in front of one that holds TAG for the spray time tags it; too fast, behind the wall, out of
// reach or letting go does not; the tag's art is in front of the wall, facing out, within the spot.
//   test_tagging MAP      (exit 77 = skipped when the map is not there)
#include <cmath>
#include <cstdio>

#include "game/tagging.h"
#include "world/map_file.h"

namespace {
int bad = 0;
void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++bad;
}
// hold TAG (or not) for `secs` in 1/60 s steps; the spot finished, or -1
int hold(amr::Tagging& t, const amr::Car& car, double secs, bool holding = true) {
    int done = -1;
    for (int k = 0; k < static_cast<int>(secs * 60); ++k) {
        const int d = t.update(1.0 / 60, car, holding);
        if (d >= 0) done = d;
    }
    return done;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    amr::MapData m;
    std::string err;
    if (!amr::load_map(argv[1], m, err)) {
        std::printf("skipped: %s\n", err.c_str());
        return 77;
    }
    amr::World world(std::move(m));
    amr::Tagging tags(world);
    const auto& spots = tags.spots();
    std::printf("%zu tag spots\n", spots.size());
    check(spots.size() >= 30, "at least 30 tag spots in town");
    bool spread = true, homes = false, faces = true;
    for (std::size_t i = 0; i < spots.size(); ++i) {
        for (std::size_t j = i + 1; j < spots.size(); ++j)
            spread = spread && std::hypot(spots[i].x - spots[j].x, spots[i].y - spots[j].y) >= 70.0;
        const amr::Building& b = world.map().buildings[spots[i].building];
        homes = homes || b.detail.house != amr::kNotHouse || b.detail.landmark != amr::kNoLandmark;
        bool road = false;
        for (double d = 3.0; d <= 10.0; d += 1.0) road = road || world.on_road(spots[i].x + spots[i].nx * d, spots[i].y + spots[i].ny * d);
        faces = faces && road;
    }
    check(spread, "spots are at least 70 m apart");
    check(!homes, "no spot on a home or a landmark");
    check(faces, "every spot faces a street within 10 m");

    const amr::TagSpot s = spots[0];
    const double heading = std::atan2(s.nx, -s.ny);  // parked along the wall, 5 m out
    amr::Car car(s.x + s.nx * 5.0, s.y + s.ny * 5.0, heading, amr::player_model(0));
    check(hold(tags, car, 0.5) == -1 && tags.progress() > 0.4, "half a second of spraying: not done, the bar half full");
    hold(tags, car, 1.0, false);
    check(tags.progress() == 0.0, "letting go empties the bar");
    car.vx = 80.0 / 3.6 * std::cos(heading);
    car.vy = 80.0 / 3.6 * std::sin(heading);
    check(hold(tags, car, 2.0) == -1, "too fast (80 km/h): no tag");
    car.vx = car.vy = 0;
    amr::Car behind(s.x - s.nx * 5.0, s.y - s.ny * 5.0, heading, amr::player_model(0));
    check(hold(tags, behind, 2.0) == -1, "behind the wall: no tag");
    amr::Car far(s.x + s.nx * 20.0, s.y + s.ny * 20.0, heading, amr::player_model(0));
    check(hold(tags, far, 2.0) == -1, "20 m out: no tag");
    check(hold(tags, car, 1.2) == 0 && tags.spots()[0].tagged && tags.tagged() == 1, "parked in front, held: tagged");
    check(hold(tags, car, 2.0) == -1 && tags.tagged() == 1, "a tagged spot cannot be tagged again");

    const amr::MeshBuilder art = amr::tag_mesh(s, 3);
    bool out = !art.idx.empty(), facing = true;
    for (const auto& v : art.verts) {
        const double o = (v.x - s.x) * s.nx + (v.y - s.y) * s.ny, along = (v.x - s.x) * s.ny - (v.y - s.y) * s.nx;
        out = out && o > 0.05 && o < 0.4 && std::fabs(along) < s.width * 0.62 + 0.05 && v.z > s.base && v.z < s.base + s.wall_h;
    }
    for (std::size_t k = 0; k + 2 < art.idx.size(); k += 3) {
        const auto &a = art.verts[art.idx[k]], &b = art.verts[art.idx[k + 1]], &c = art.verts[art.idx[k + 2]];
        const amr::Vec3 n = amr::cross(amr::Vec3{b.x - a.x, b.y - a.y, b.z - a.z}, amr::Vec3{c.x - a.x, c.y - a.y, c.z - a.z});
        facing = facing && n.x * s.nx + n.y * s.ny >= -1e-6;
    }
    check(out, "the tag's art is just in front of the wall, within the spot and the wall's height");
    check(facing, "every triangle of it faces out of the wall");
    return bad ? 1 : 0;
}
