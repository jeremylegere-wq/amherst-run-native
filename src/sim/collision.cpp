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

}  // namespace amr
