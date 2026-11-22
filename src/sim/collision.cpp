#include "sim/collision.h"

#include <cmath>

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
}  // namespace

double move_and_slide(Car& car, const World& w, double prev_x, double prev_y, bool thorough) {
    if (!hits_static(car, w, thorough)) return 0.0;
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
