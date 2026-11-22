#include "sim/train.h"

#include <algorithm>
#include <cmath>

namespace amr {

namespace {
constexpr double kFirstDelay[2] = {50.0, 90.0}, kGap[2] = {240.0, 420.0};
constexpr int kCarUnits[2] = {6, 11};
constexpr double kPi = 3.14159265358979323846;
}  // namespace

Train::Train(const MapData& map, unsigned seed, bool enabled) : rng_(seed * 104729u + 11u) {
    if (enabled) chain_ = map.train_line;
    cum_.push_back(0.0);
    for (std::size_t i = 0; i + 1 < chain_.size(); ++i)
        cum_.push_back(cum_.back() + std::hypot(chain_[i + 1].x - chain_[i].x, chain_[i + 1].y - chain_[i].y));
    total_ = cum_.back();
    lo_ = map.train_lo;
    hi_ = map.train_hi;
    wait_ = std::uniform_real_distribution<double>(kFirstDelay[0], kFirstDelay[1])(rng_);
}

void Train::point_at(double s, double& x, double& y, double& heading) const {
    s = std::max(0.0, std::min(total_, s));
    std::size_t lo = 0, hi = cum_.size() - 1;
    while (hi - lo > 1) {
        const std::size_t mid = (lo + hi) / 2;
        if (cum_[mid] <= s) lo = mid;
        else hi = mid;
    }
    const GraphNode& a = chain_[lo];
    const GraphNode& b = chain_[lo + 1];
    double seg = cum_[lo + 1] - cum_[lo];
    if (seg == 0) seg = 1.0;
    const double t = (s - cum_[lo]) / seg;
    x = a.x + (b.x - a.x) * t;
    y = a.y + (b.y - a.y) * t;
    heading = std::atan2(b.y - a.y, b.x - a.x);
}

void Train::place(double s, int dir, int cars) {
    active_ = true;
    s_ = s;
    dir_ = dir;
    cars_ = cars;
    cache_ok_ = false;
}

void Train::update(double dt) {
    if (total_ <= 0 || hi_ - lo_ < 200.0) return;
    cache_ok_ = false;
    if (!active_) {
        wait_ -= dt;
        if (wait_ <= 0.0) {
            active_ = true;
            cars_ = std::uniform_int_distribution<int>(kCarUnits[0], kCarUnits[1])(rng_);
            dir_ = std::uniform_int_distribution<int>(0, 1)(rng_) ? 1 : -1;
            s_ = dir_ > 0 ? lo_ : hi_;  // the front starts at the entry end
        }
        return;
    }
    s_ += dir_ * kSpeed * dt;
    const double tail = s_ - dir_ * length();
    const bool past = dir_ > 0 ? (s_ > hi_ && tail > hi_) : (s_ < lo_ && tail < lo_);
    if (past) {
        active_ = false;
        wait_ = std::uniform_real_distribution<double>(kGap[0], kGap[1])(rng_);
    }
}

const std::vector<TrainUnit>& Train::units() {
    if (cache_ok_) return cache_;
    cache_.clear();
    cache_ok_ = true;
    if (!active_) return cache_;
    const int n = 2 + 2 * cars_;
    for (int i = 0; i < n; ++i) {
        const double s = s_ - dir_ * (i + 0.5) * kUnitLen;
        if (s < lo_ - kUnitLen || s > hi_ + kUnitLen) continue;
        double x, y, h;
        point_at(s, x, y, h);
        if (dir_ < 0) h += kPi;  // the front faces down the line
        cache_.push_back({x, y, h, i == 0 ? TrainUnit::Cab : (i < 2 ? TrainUnit::Loco : TrainUnit::Freight), i});
    }
    return cache_;
}

double Train::push_out(Car& car) {
    if (!active_) return 0.0;
    for (const TrainUnit& u : units()) {
        const double dx = car.x - u.x, dy = car.y - u.y;
        if (dx * dx + dy * dy > 100.0) continue;
        const double fx = std::cos(u.heading), fy = std::sin(u.heading), rx = -std::sin(u.heading), ry = std::cos(u.heading);
        double a = dx * fx + dy * fy, b = dx * rx + dy * ry;
        const double ha = kUnitLen / 2 + 1.2, hb = kHalfW + 1.2;  // the car's half sizes added
        if (std::fabs(a) < ha && std::fabs(b) < hb) {
            if (hb - std::fabs(b) < ha - std::fabs(a)) b = std::copysign(hb, b);  // nearest edge: the side
            else a = std::copysign(ha, a);
            car.x = u.x + fx * a + rx * b;
            car.y = u.y + fy * a + ry * b;
            const double hit = car.speed();
            car.vx *= 0.2;
            car.vy *= 0.2;
            return hit;
        }
    }
    return 0.0;
}

}  // namespace amr
