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

Tagging::Tagging(const World& world, int count) {
    const MapData& m = world.map();
    std::vector<TagSpot> cand;
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
        }
    }
    Rng rng{20261123u};  // shuffle, then keep each one far enough from those already kept
    for (std::size_t k = cand.size(); k > 1; --k) std::swap(cand[k - 1], cand[static_cast<std::size_t>(rng.next() * k)]);
    for (const TagSpot& s : cand) {
        if (static_cast<int>(spots_.size()) >= count) break;
        bool far = true;
        for (const TagSpot& t : spots_) far = far && std::hypot(s.x - t.x, s.y - t.y) >= kSpacing;
        if (far) spots_.push_back(s);
    }
}

int Tagging::find_reach(const Car& car) const {
    int best = -1;
    double best_d = 1e9;
    for (int i = 0; i < static_cast<int>(spots_.size()); ++i) {
        const TagSpot& s = spots_[i];
        if (s.tagged) continue;
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

int Tagging::update(double dt, const Car& car, bool holding) {
    reach_ = car.speed() <= kMaxSpeed ? find_reach(car) : -1;
    if (reach_ < 0 || !holding) {
        progress_ = std::max(0.0, progress_ - dt * 2.0);  // let go: the can stops (and the bar drains)
        return -1;
    }
    progress_ += dt;
    if (progress_ < kSprayTime) return -1;
    progress_ = 0;
    spots_[reach_].tagged = true;
    ++tagged_;
    const int done = reach_;
    reach_ = -1;
    return done;
}

int Tagging::mark_tagged(double x, double y) {
    for (int i = 0; i < static_cast<int>(spots_.size()); ++i)
        if (!spots_[i].tagged && std::hypot(spots_[i].x - x, spots_[i].y - y) < 2.0) {
            spots_[i].tagged = true;
            ++tagged_;
            return i;
        }
    return -1;
}

MeshBuilder tag_mesh(const TagSpot& s, unsigned seed) {
    // crew palettes: (ink edge, back plate, letter fill, highlight), all this game's own
    static const std::uint8_t kPalettes[4][4][3] = {
        {{22, 18, 40}, {214, 48, 140}, {252, 222, 48}, {255, 255, 255}},   // magenta plate, yellow letters
        {{22, 18, 40}, {36, 110, 230}, {120, 240, 120}, {255, 255, 255}},  // blue plate, lime letters
        {{22, 18, 40}, {255, 150, 30}, {60, 220, 240}, {255, 255, 255}},   // orange plate, cyan letters
        {{22, 18, 40}, {120, 60, 200}, {255, 120, 170}, {255, 255, 255}},  // purple plate, pink letters
    };
    Rng rng{seed * 2654435761u + 17u};
    const auto& pal = kPalettes[static_cast<int>(rng.next() * 4) % 4];
    MeshBuilder m;
    m.alpha = flags::within(160.0f);
    const double ux = s.ny, uy = -s.nx;  // left to right, seen from outside
    const double w = s.width, hh = std::min(w * rng.uniform(0.34, 0.44), s.wall_h - 1.2);
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
    const int n = 3 + static_cast<int>(rng.next() * 2.99);
    struct Letter {
        double u, v, a, b, t;
    };
    std::vector<Letter> letters;
    for (int k = 0; k < n; ++k)
        letters.push_back({-w / 2 + (k + 0.5) * w / n, cv + rng.uniform(-0.12, 0.12) * hh, w / n * 0.5,
                           hh * 0.62 * rng.uniform(0.85, 1.15), tilt + rng.uniform(-0.3, 0.3)});
    for (const Letter& l : letters) {  // each letter's ink edge then its fill, so they stack like bubble letters
        blob(l.u, l.v, l.a + 0.16, l.b + 0.16, l.t, 12, pal[0]);
        blob(l.u, l.v, l.a, l.b, l.t, 12, pal[2]);
        blob(l.u - l.a * 0.35, l.v + l.b * 0.45, l.a * 0.2, l.b * 0.15, l.t, 8, pal[3]);  // a shine
    }
    return m;
}

}  // namespace amr
