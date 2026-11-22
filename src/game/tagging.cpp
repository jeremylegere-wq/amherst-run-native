#include "game/tagging.h"

#include <algorithm>
#include <cmath>

namespace amr {

namespace {

constexpr double kSpacing = 70.0;  // metres between spots

// A little deterministic random source (the same spots and tags every run).
struct Rng {
    unsigned s;
    double next() {
        s = s * 1664525u + 1013904223u;
        return ((s >> 8) & 0xFFFFFF) / 16777216.0;
    }
    double uniform(double a, double b) { return a + (b - a) * next(); }
};

}  // namespace

Tagging::Tagging(const World& world, int count, int pieces) {
    const MapData& m = world.map();
    std::vector<TagSpot> cand;
    std::vector<double> lens;  // each candidate wall's length
    for (int i = 0; i < static_cast<int>(m.buildings.size()); ++i) {
        const Building& b = m.buildings[i];
        if (b.detail.house != kNotHouse || b.detail.landmark != kNoLandmark || b.height < 3.5f || b.points.size() < 3) continue;
        const auto& p = b.points;
        float area = 0, hi = -1e9f, cx = 0, cy = 0;
        for (std::size_t k = 0; k < p.size(); ++k) {
            const Point2 q = p[(k + 1) % p.size()];
            area += p[k].x * q.y - q.x * p[k].y;
            hi = std::max(hi, world.ground(p[k].x, p[k].y));
            cx += p[k].x;
            cy += p[k].y;
        }
        hi = std::max(hi, world.ground(cx / p.size(), cy / p.size()));
        for (std::size_t k = 0; k < p.size(); ++k) {
            const Point2 a = p[k], c = p[(k + 1) % p.size()];
            const double dx = c.x - a.x, dy = c.y - a.y, L = std::hypot(dx, dy);
            if (L < 6.0) continue;
            const double nx = area > 0 ? dy / L : -dy / L, ny = area > 0 ? -dx / L : dx / L;
            const double mx = (a.x + c.x) / 2, my = (a.y + c.y) / 2;
            bool faces = false;  // a street within 3-10 m straight out, nothing built in between
            for (double d = 3.0; d <= 10.0 && !faces; d += 1.0) {
                if (world.in_building(mx + nx * d * 0.5, my + ny * d * 0.5)) break;
                faces = world.on_road(mx + nx * d, my + ny * d);
            }
            if (!faces) continue;
            TagSpot s;
            s.x = mx;
            s.y = my;
            s.nx = nx;
            s.ny = ny;
            s.width = std::min(L - 1.6, 5.5);
            s.base = hi;
            s.wall_h = b.height;
            s.building = i;
            cand.push_back(s);
            lens.push_back(L);
        }
    }
    Rng rng{20261123u};  // shuffle, then keep each one far enough from those already kept
    std::vector<std::size_t> order(cand.size());
    for (std::size_t k = 0; k < order.size(); ++k) order[k] = k;
    for (std::size_t k = order.size(); k > 1; --k) std::swap(order[k - 1], order[static_cast<std::size_t>(rng.next() * k)]);
    auto far_enough = [&](const TagSpot& s) {
        for (const TagSpot& t : spots_)
            if (std::hypot(s.x - t.x, s.y - t.y) < kSpacing) return false;
        return true;
    };
    int n_pieces = 0;  // the big pieces first: walls of 14 m or more get an 11 m piece
    for (std::size_t k : order) {
        if (n_pieces >= pieces) break;
        if (lens[k] < 14.0 || !far_enough(cand[k])) continue;
        TagSpot s = cand[k];
        s.piece = true;
        s.width = std::min(lens[k] - 2.0, 11.0);
        spots_.push_back(s);
        ++n_pieces;
    }
    int n_tags = 0;
    for (std::size_t k : order) {
        if (n_tags >= count) break;
        if (!far_enough(cand[k])) continue;
        spots_.push_back(cand[k]);
        ++n_tags;
    }
    for (std::size_t k = 0; k < spots_.size(); ++k)  // the rival crew holds about a third of the plain walls
        if (!spots_[k].piece && k % 3 == 1) spots_[k].owner = Crew::Rival;
}

int Tagging::mine() const {
    int n = 0;
    for (const TagSpot& s : spots_) n += s.owner == Crew::Player;
    return n;
}

int Tagging::rivals() const {
    int n = 0;
    for (const TagSpot& s : spots_) n += s.owner == Crew::Rival;
    return n;
}

void Tagging::clear_owners() {
    for (TagSpot& s : spots_) {
        s.owner = Crew::None;
        s.passes = 0;
    }
}

int Tagging::set_owner(double x, double y, Crew crew) {
    for (int i = 0; i < static_cast<int>(spots_.size()); ++i)
        if (std::hypot(spots_[i].x - x, spots_[i].y - y) < 2.0) {
            spots_[i].owner = crew;
            spots_[i].passes = 0;
            return i;
        }
    return -1;
}

int Tagging::find_reach(const Car& car) const {
    int best = -1;
    double best_d = 1e9;
    for (int i = 0; i < static_cast<int>(spots_.size()); ++i) {
        const TagSpot& s = spots_[i];
        if (s.owner == Crew::Player) continue;
        const double rx = car.x - s.x, ry = car.y - s.y;
        const double out = rx * s.nx + ry * s.ny, along = rx * s.ny - ry * s.nx;
        if (out < 0.5 || out > kReach || std::fabs(along) > s.width / 2 + 4.0) continue;  // in front of the wall
        if (out < best_d) {
            best_d = out;
            best = i;
        }
    }
    return best;
}

TagEvent Tagging::update(double dt, const Car& car, bool holding) {
    TagEvent ev;
    rival_t_ += dt;  // the rivals strike back now and then, somewhere the player is not
    if (rival_t_ >= kRivalEvery) {
        rival_t_ = 0;
        if (mine() >= 3) {
            std::vector<int> far;
            for (int i = 0; i < static_cast<int>(spots_.size()); ++i)
                if (spots_[i].owner == Crew::Player && !spots_[i].piece &&
                    std::hypot(spots_[i].x - car.x, spots_[i].y - car.y) > 200.0)
                    far.push_back(i);
            if (!far.empty()) {
                const int i = far[(strike_n_++ * 7) % far.size()];
                spots_[i].owner = Crew::Rival;
                ev.kind = TagEvent::RivalStrike;
                ev.spot = i;
            }
        }
    }
    reach_ = car.speed() <= kMaxSpeed ? find_reach(car) : -1;
    if (reach_ < 0 || !holding) {
        progress_ = std::max(0.0, progress_ - dt * 2.0);  // let go: the can stops (and the bar drains)
        return ev;
    }
    progress_ += dt;
    if (progress_ < kSprayTime) return ev;
    progress_ = 0;
    TagSpot& s = spots_[reach_];
    ev.spot = reach_;
    if (s.piece && ++s.passes < kPieceSprays) {
        ev.kind = TagEvent::Pass;
        return ev;
    }
    ev.kind = TagEvent::Done;
    ev.was = s.owner;
    ev.pay = s.piece ? kPiecePay : s.owner == Crew::Rival ? kCoverPay : kPay;
    s.owner = Crew::Player;
    s.passes = 0;
    reach_ = -1;
    return ev;
}

MeshBuilder tag_mesh(const TagSpot& s, unsigned seed, Crew crew, double fraction) {
    // the two crews' colours: (ink edge, back plate, letter fill, highlight), this game's own
    static const std::uint8_t kPlayer[4][3] = {{22, 18, 40}, {214, 48, 140}, {252, 222, 48}, {255, 255, 255}};
    static const std::uint8_t kRival[4][3] = {{12, 14, 12}, {40, 110, 70}, {230, 50, 40}, {255, 220, 200}};
    Rng rng{seed * 2654435761u + 17u};
    const auto& pal = crew == Crew::Rival ? kRival : kPlayer;
    MeshBuilder m;
    m.alpha = flags::within(160.0f);
    const double ux = s.ny, uy = -s.nx;  // left to right, seen from outside
    const double w = s.width, hh = std::min(w * (s.piece ? rng.uniform(0.22, 0.28) : rng.uniform(0.34, 0.44)), s.wall_h - 1.2);
    if (hh < 0.6) return m;
    const double z0 = rng.uniform(0.45, 0.8), cv = z0 + hh / 2, tilt = rng.uniform(-0.12, 0.12);
    int layer = 0;
    // a chunky rounded blob (render3d.py _blob's shape: a squircle with a little wobble), fanned from its middle
    auto blob = [&](double cu, double cvv, double a, double b, double t, int n, const std::uint8_t rgb[3]) {
        const double out = 0.1 + 0.006 * layer++;
        const Vec3 nrm{static_cast<float>(s.nx), static_cast<float>(s.ny), 0};
        auto P = [&](double u, double v) {
            return Vec3{static_cast<float>(s.x + ux * u + s.nx * out), static_cast<float>(s.y + uy * u + s.ny * out),
                        static_cast<float>(s.base + v)};
        };
        const double ct = std::cos(t), sn = std::sin(t);
        const std::uint32_t centre = m.add(P(cu, cvv), nrm, rgb);
        std::vector<std::uint32_t> ring;
        for (int k = 0; k < n; ++k) {
            const double q = 2 * 3.14159265358979 * k / n, c = std::cos(q), si = std::sin(q), j = rng.uniform(0.92, 1.08);
            const double x = a * std::copysign(std::pow(std::fabs(c), 0.55), c) * j;
            const double y = b * std::copysign(std::pow(std::fabs(si), 0.55), si) * j;
            ring.push_back(m.add(P(cu + x * ct - y * sn, std::max(0.05, cvv + x * sn + y * ct)), nrm, rgb));
        }
        for (int k = 0; k < n; ++k) {  // wound to face out of the wall (the renderer culls the back)
            const std::uint32_t i0 = ring[k], i1 = ring[(k + 1) % n];
            const Vec3 a3{m.verts[i0].x - m.verts[centre].x, m.verts[i0].y - m.verts[centre].y, m.verts[i0].z - m.verts[centre].z};
            const Vec3 b3{m.verts[i1].x - m.verts[centre].x, m.verts[i1].y - m.verts[centre].y, m.verts[i1].z - m.verts[centre].z};
            if (dot(cross(a3, b3), nrm) >= 0) m.tri(centre, i0, i1);
            else m.tri(centre, i1, i0);
        }
    };
    blob(0, cv, w * 0.6, hh * 0.86, tilt, 16, pal[0]);   // the ink edge round the plate
    blob(0, cv, w * 0.56, hh * 0.78, tilt, 16, pal[1]);  // the back plate
    const int n = s.piece ? 5 + static_cast<int>(rng.next() * 2.99) : 3 + static_cast<int>(rng.next() * 2.99);
    struct Letter {
        double u, v, a, b, t;
    };
    std::vector<Letter> letters;
    for (int k = 0; k < n; ++k)
        letters.push_back({-w / 2 + (k + 0.5) * w / n, cv + rng.uniform(-0.12, 0.12) * hh, w / n * 0.5,
                           hh * 0.62 * rng.uniform(0.85, 1.15), tilt + rng.uniform(-0.3, 0.3)});
    const int shown = std::max(1, static_cast<int>(std::ceil(n * std::min(1.0, fraction) - 1e-9)));
    letters.resize(static_cast<std::size_t>(shown));  // a piece in progress: the first letters so far
    for (const Letter& l : letters) {  // each letter's ink edge then its fill, so they stack like bubble letters
        blob(l.u, l.v, l.a + 0.16, l.b + 0.16, l.t, 12, pal[0]);
        blob(l.u, l.v, l.a, l.b, l.t, 12, pal[2]);
        blob(l.u - l.a * 0.35, l.v + l.b * 0.45, l.a * 0.2, l.b * 0.15, l.t, 8, pal[3]);  // a shine
    }
    return m;
}

}  // namespace amr
