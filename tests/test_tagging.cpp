// Tagging (game/tagging.h) on the real map: the spots are on commercial walls that face a street and are spread
// out; a car stopped in front of one that holds TAG for the spray time tags it; too fast, behind the wall, out of
// reach or letting go does not; the tag's art is in front of the wall, facing out, within the spot.
// Rival walls pay more to cover; big pieces take three sprays and grow; the rivals go over a far wall of the
// player's every 2.5 minutes; a save's owners come back.
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
// hold TAG (or not) for `secs` in 1/60 s steps; the last event that was not None
amr::TagEvent hold(amr::Tagging& t, const amr::Car& car, double secs, bool holding = true) {
    amr::TagEvent last;
    for (int k = 0; k < static_cast<int>(secs * 60); ++k) {
        const amr::TagEvent e = t.update(1.0 / 60, car, holding);
        if (e.kind != amr::TagEvent::None) last = e;
    }
    return last;
}
amr::Car parked_at(const amr::TagSpot& s, double out = 5.0) {  // along the wall, `out` metres in front of it
    return amr::Car(s.x + s.nx * out, s.y + s.ny * out, std::atan2(s.nx, -s.ny), amr::player_model(0));
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
    int pieces = 0;
    for (const auto& s : spots) pieces += s.piece;
    std::printf("%zu walls (%d big pieces), %d held by the rivals\n", spots.size(), pieces, tags.rivals());
    check(spots.size() >= 35 && pieces >= 3, "at least 35 walls, 3 of them big pieces");
    check(tags.rivals() >= 8 && tags.mine() == 0, "the rivals start with some walls; the player with none");
    bool spread = true, homes = false, faces = true, long_pieces = true;
    for (std::size_t i = 0; i < spots.size(); ++i) {
        for (std::size_t j = i + 1; j < spots.size(); ++j)
            spread = spread && std::hypot(spots[i].x - spots[j].x, spots[i].y - spots[j].y) >= 70.0;
        const amr::Building& b = world.map().buildings[spots[i].building];
        homes = homes || b.detail.house != amr::kNotHouse || b.detail.landmark != amr::kNoLandmark;
        bool road = false;
        for (double d = 3.0; d <= 10.0; d += 1.0) road = road || world.on_road(spots[i].x + spots[i].nx * d, spots[i].y + spots[i].ny * d);
        faces = faces && road;
        if (spots[i].piece) long_pieces = long_pieces && spots[i].width >= 10.0 && spots[i].owner == amr::Crew::None;
    }
    check(spread, "walls are at least 70 m apart");
    check(!homes, "no wall on a home or a landmark");
    check(faces, "every wall faces a street within 10 m");
    check(long_pieces, "big pieces are on long walls (10 m wide or more) and start blank");

    int blank = -1, rival = -1, piece = -1;
    for (int i = 0; i < static_cast<int>(spots.size()); ++i) {
        if (spots[i].piece && piece < 0) piece = i;
        else if (!spots[i].piece && spots[i].owner == amr::Crew::None && blank < 0) blank = i;
        else if (spots[i].owner == amr::Crew::Rival && rival < 0) rival = i;
    }
    amr::Car car = parked_at(spots[blank]);
    check(hold(tags, car, 0.5).kind == amr::TagEvent::None && tags.progress() > 0.4, "half a second: not done, the bar half full");
    hold(tags, car, 1.0, false);
    check(tags.progress() == 0.0, "letting go empties the bar");
    amr::Car fast = car;
    fast.vx = 80.0 / 3.6 * std::cos(fast.heading);
    fast.vy = 80.0 / 3.6 * std::sin(fast.heading);
    check(hold(tags, fast, 2.0).kind == amr::TagEvent::None, "too fast (80 km/h): no tag");
    const amr::TagSpot& bs = spots[blank];
    amr::Car behind(bs.x - bs.nx * 5.0, bs.y - bs.ny * 5.0, car.heading, amr::player_model(0));
    check(hold(tags, behind, 2.0).kind == amr::TagEvent::None, "behind the wall: no tag");
    check(hold(tags, parked_at(bs, 20.0), 2.0).kind == amr::TagEvent::None, "20 m out: no tag");
    amr::TagEvent e = hold(tags, car, 1.2);
    check(e.kind == amr::TagEvent::Done && e.spot == blank && e.pay == amr::Tagging::kPay && tags.mine() == 1,
          "parked in front of a blank wall, held: tagged, $25");
    check(hold(tags, car, 2.0).kind == amr::TagEvent::None && tags.mine() == 1, "a wall of the player's cannot be tagged again");

    e = hold(tags, parked_at(spots[rival]), 1.2);
    check(e.kind == amr::TagEvent::Done && e.was == amr::Crew::Rival && e.pay == amr::Tagging::kCoverPay &&
          spots[rival].owner == amr::Crew::Player, "covering a rival tag: $50, and the wall is the player's");

    const amr::Car at_piece = parked_at(spots[piece]);
    e = hold(tags, at_piece, 1.0);
    check(e.kind == amr::TagEvent::Pass && spots[piece].passes == 1 && spots[piece].owner == amr::Crew::None,
          "a big piece: the first spray is one pass, not done");
    hold(tags, at_piece, 1.0);
    e = hold(tags, at_piece, 1.0);
    check(e.kind == amr::TagEvent::Done && e.pay == amr::Tagging::kPiecePay && spots[piece].owner == amr::Crew::Player,
          "the third spray finishes it: $100");

    // the rivals strike: with three walls, after 150 s, one of the player's far away goes back to them
    amr::Tagging strike(world);
    std::vector<int> mine_far;
    for (int i = 0; i < static_cast<int>(strike.spots().size()) && mine_far.size() < 3; ++i)
        if (!strike.spots()[i].piece) mine_far.push_back(strike.set_owner(strike.spots()[i].x, strike.spots()[i].y, amr::Crew::Player));
    const amr::Car away(strike.spots()[mine_far[0]].x + 5000, strike.spots()[mine_far[0]].y, 0, amr::player_model(0));
    const int rivals_before = strike.rivals();
    e = hold(strike, away, 149.0, false);
    check(e.kind == amr::TagEvent::None, "no strike before 2.5 minutes");
    e = hold(strike, away, 2.0, false);
    check(e.kind == amr::TagEvent::RivalStrike && strike.rivals() == rivals_before + 1 && strike.mine() == 2 &&
          strike.spots()[e.spot].owner == amr::Crew::Rival, "after 2.5 minutes the rivals take one of the player's walls");

    amr::Tagging saved(world);  // a saved game: owners come back by position
    saved.clear_owners();
    check(saved.rivals() == 0 && saved.mine() == 0, "clear_owners: every wall blank");
    check(saved.set_owner(spots[blank].x + 0.5, spots[blank].y, amr::Crew::Player) == blank && saved.mine() == 1,
          "a saved wall is found again by where it is");
    check(saved.set_owner(spots[0].x + 50, spots[0].y + 50, amr::Crew::Rival) == -1, "a saved wall with none there is dropped");

    for (amr::Crew crew : {amr::Crew::Player, amr::Crew::Rival}) {
        const amr::TagSpot& s = spots[blank];
        const amr::MeshBuilder art = amr::tag_mesh(s, 3, crew);
        bool out = !art.idx.empty(), facing = true;
        for (const auto& v : art.verts) {
            const double o = (v.x - s.x) * s.nx + (v.y - s.y) * s.ny, along = (v.x - s.x) * s.ny - (v.y - s.y) * s.nx;
            out = out && o > 0.05 && o < 0.5 && std::fabs(along) < s.width * 0.62 + 0.05 && v.z > s.base && v.z < s.base + s.wall_h;
        }
        for (std::size_t k = 0; k + 2 < art.idx.size(); k += 3) {
            const auto &a = art.verts[art.idx[k]], &b = art.verts[art.idx[k + 1]], &c = art.verts[art.idx[k + 2]];
            const amr::Vec3 n = amr::cross(amr::Vec3{b.x - a.x, b.y - a.y, b.z - a.z}, amr::Vec3{c.x - a.x, c.y - a.y, c.z - a.z});
            facing = facing && n.x * s.nx + n.y * s.ny >= -1e-6;
        }
        check(out && facing, crew == amr::Crew::Player ? "the player's tag sits on the wall, facing out"
                                                       : "the rivals' tag sits on the wall, facing out");
    }
    const std::size_t third = amr::tag_mesh(spots[piece], 3, amr::Crew::Player, 1.0 / 3).verts.size();
    const std::size_t whole = amr::tag_mesh(spots[piece], 3, amr::Crew::Player, 1.0).verts.size();
    check(third < whole, "a piece a third done shows fewer letters than the finished one");
    return bad ? 1 : 0;
}
