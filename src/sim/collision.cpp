#include "sim/collision.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace amr {

namespace {
constexpr double kBoundsMargin = 12.0;  // how far past the map edge a car may drive
constexpr double kBounce = 0.2, kScrape = 0.97, kBounceFull = 0.3;
constexpr double kSlopePull = 4.0;      // m/s^2 per unit of slope (arcade-soft gravity)

bool hits_static(const Car& car, const World& w, bool thorough) {
    const auto c = car.corners();
    auto bad = [&](double x, double y) { return w.in_building(x, y) || !w.in_bounds(x, y, kBoundsMargin); };
    for (const auto& p : c)
        if (bad(p[0], p[1])) return true;
    if (!thorough) return false;
    for (int i = 0; i < 4; ++i) {
        const auto& a = c[i];
        const auto& b = c[(i + 1) % 4];
        if (bad((a[0] + b[0]) / 2, (a[1] + b[1]) / 2)) return true;
    }
    return bad(car.x, car.y);
}
// Push the car off any tree trunk or post it overlaps and bounce the velocity going into it (collision.py
// _push_out_props). Pushing out, not undoing the move: steering can swing the car's side into a trunk.
double push_out_props(Car& car, const World& w) {
    std::vector<const Prop*> props;
    w.props_near(car.x, car.y, 4.0, [&](const Prop& p) { props.push_back(&p); });
    if (props.empty()) return 0.0;
    const double hl = car.length / 2, hw = car.width / 2;
    double impact = 0.0;
    for (int pass = 0; pass < 2; ++pass) {
        bool moved = false;
        const double fx = car.fx(), fy = car.fy(), rx = car.rx(), ry = car.ry();
        for (const Prop* p : props) {
            const double dx = p->x - car.x, dy = p->y - car.y;
            const double a = dx * fx + dy * fy, b = dx * rx + dy * ry;          // prop centre in car coordinates
            const double qa = std::max(-hl, std::min(hl, a)), qb = std::max(-hw, std::min(hw, b));
            const double ea = a - qa, eb = b - qb, d = std::hypot(ea, eb);
            const double cr = p->cr;
            if (d >= cr) continue;
            double na, nb, push;
            if (d < 1e-6) {  // trunk centre inside the car: leave by the nearest side
                if (hw - std::fabs(b) < hl - std::fabs(a)) {
                    na = 0.0; nb = std::copysign(1.0, b != 0 ? b : 1.0); push = hw - std::fabs(b) + cr;
                } else {
                    na = std::copysign(1.0, a != 0 ? a : 1.0); nb = 0.0; push = hl - std::fabs(a) + cr;
                }
            } else {
                na = ea / d; nb = eb / d; push = cr - d;
            }
            const double wx = fx * na + rx * nb, wy = fy * na + ry * nb;  // from the car towards the prop
            car.x -= wx * push;
            car.y -= wy * push;
            const double vn = car.vx * wx + car.vy * wy;
            if (vn > 0) {
                car.vx -= (1 + kBounce) * vn * wx;
                car.vy -= (1 + kBounce) * vn * wy;
                impact = std::max(impact, vn);
            }
            moved = true;
        }
        if (!moved) break;
    }
    return impact;
}

}  // namespace

double move_and_slide(Car& car, const World& w, double prev_x, double prev_y, bool thorough) {
    const double prop_impact = push_out_props(car, w);
    if (!hits_static(car, w, thorough)) return prop_impact;
    const double nx = car.x, ny = car.y, vx = car.vx, vy = car.vy;
    car.x = nx; car.y = prev_y;  // keep the x movement, undo y
    if (!hits_static(car, w, thorough)) {
        car.vy = -vy * kBounce;
        car.vx = vx * kScrape;
        return std::fabs(vy);
    }
    car.x = prev_x; car.y = ny;  // keep the y movement, undo x
    if (!hits_static(car, w, thorough)) {
        car.vx = -vx * kBounce;
        car.vy = vy * kScrape;
        return std::fabs(vx);
    }
    car.x = prev_x; car.y = prev_y;  // blocked both ways: go back
    const double impact = std::hypot(vx, vy);
    if (hits_static(car, w, thorough)) {  // already wedged: let the car crawl out instead of freezing
        car.x = nx; car.y = ny;
        car.vx = vx * 0.5; car.vy = vy * 0.5;
        return 0.0;
    }
    car.vx = -vx * kBounceFull;
    car.vy = -vy * kBounceFull;
    return impact;
}

void settle_height(Car& car, const World& w, double dt) {
    const double target = w.height_at(static_cast<float>(car.x), static_cast<float>(car.y), static_cast<float>(car.z));
    car.z += (target - car.z) * std::fmin(1.0, 12.0 * dt);
    if (std::fabs(car.z - w.ground(static_cast<float>(car.x), static_cast<float>(car.y))) < 0.5) {
        double nx, ny, nz;
        w.ground_normal(car.x, car.y, nx, ny, nz);
        const double k = kSlopePull * dt / std::fmax(nz, 0.2);
        car.vx += nx * k;
        car.vy += ny * k;
    }
}

double collide_cars(Car& a, Car& b) {
    constexpr double kLevelGap = 2.6, kRestitution = 0.35;
    if (std::fabs(a.x - b.x) > 6.0 || std::fabs(a.y - b.y) > 6.0 || std::fabs(a.z - b.z) > kLevelGap) return 0.0;
    auto radius = [](const Car& c) { return c.width / 2 + 0.25; };
    auto circles = [&](const Car& c, double out[2][2]) {
        const double off = std::max(0.4, c.length / 2 - radius(c));
        out[0][0] = c.x + c.fx() * off; out[0][1] = c.y + c.fy() * off;
        out[1][0] = c.x - c.fx() * off; out[1][1] = c.y - c.fy() * off;
    };
    double ca[2][2], cb[2][2];
    circles(a, ca);
    circles(b, cb);
    const double limit = radius(a) + radius(b);
    bool hit = false;
    double pen = 0, nx = 0, ny = 0;
    for (auto& p : ca)
        for (auto& q : cb) {
            const double dx = p[0] - q[0], dy = p[1] - q[1], d2 = dx * dx + dy * dy;
            if (d2 < limit * limit) {
                const double d = std::sqrt(d2), pn = limit - d;
                if (!hit || pn > pen) {
                    hit = true;
                    pen = pn;
                    if (d < 1e-6) { nx = 1.0; ny = 0.0; } else { nx = dx / d; ny = dy / d; }
                }
            }
        }
    if (!hit) return 0.0;  // the normal points from b towards a
    const double ma = a.mass, mb = b.mass, tot = ma + mb;
    a.x += nx * pen * mb / tot; a.y += ny * pen * mb / tot;
    b.x -= nx * pen * ma / tot; b.y -= ny * pen * ma / tot;
    const double rel = (a.vx - b.vx) * nx + (a.vy - b.vy) * ny;
    if (rel >= 0) return 0.0;  // already separating
    const double j = -(1.0 + kRestitution) * rel / (1.0 / ma + 1.0 / mb);
    a.vx += j * nx / ma; a.vy += j * ny / ma;
    b.vx -= j * nx / mb; b.vy -= j * ny / mb;
    return -rel;
}

}  // namespace amr
