#include "sim/town.h"

#include <algorithm>
#include <cmath>

#include "sim/collision.h"

namespace amr {

namespace {
constexpr double kControlDt = 0.05, kManageDt = 0.25;
constexpr double kTrafficDespawn = 260.0;
constexpr double kPaceMin = 0.88, kPaceMax = 1.06;
constexpr double kTrafficArea = 200.0, kDenseRoad = 4400.0;
constexpr int kMinTraffic = 3, kColours = 8;
constexpr double kSpawnMin = 150.0, kSpawnMax = 230.0;
double type_weight(int type) { return type == kMain ? 3.0 : (type == kSide ? 2.0 : 0.3); }  // TRAFFIC_W
}  // namespace

Town::Town(const World& world, const RoadGraph& graph, unsigned seed, int traffic)
    : world_(world), graph_(graph), rng_(seed), target_(traffic) {}

int Town::local_target(const Car& player) {
    if (target_ <= 0) return 0;
    const long long key = (static_cast<long long>(std::floor(player.x / 100)) << 32) ^
                          static_cast<unsigned>(static_cast<int>(std::floor(player.y / 100)));
    auto it = density_.find(key);
    if (it == density_.end()) {
        double total = 0;
        for (int i : graph_.edges_near(player.x, player.y, kTrafficArea))
            if (graph_.edges[i].type != kLane) total += graph_.edges[i].length;
        it = density_.emplace(key, std::min(1.0, total / kDenseRoad)).first;
    }
    return std::max(kMinTraffic, static_cast<int>(std::nearbyint(target_ * it->second)));  // round half to even
}

bool Town::occupied(double x, double y, const Car& player) const {
    if (world_.in_building(x, y)) return true;
    if (std::fabs(player.x - x) < 9.0 && std::fabs(player.y - y) < 9.0) return true;
    for (const auto& c : cars_)
        if (std::fabs(c->x - x) < 9.0 && std::fabs(c->y - y) < 9.0) return true;
    return false;
}

bool Town::spawn_traffic(const Car& player, double rmin, double rmax) {
    // RoadGraph.random_spawn: a random spot rmin..rmax away, busier roads more likely
    const std::vector<int> cands = graph_.edges_near(player.x, player.y, rmax);
    if (cands.empty()) return false;
    std::vector<double> w;
    for (int i : cands) w.push_back(type_weight(graph_.edges[i].type) + 0.001);
    std::discrete_distribution<int> pick(w.begin(), w.end());
    std::uniform_real_distribution<double> u(0.1, 0.9);
    int eid = -1;
    double s = 0;
    for (int t = 0; t < 12 && eid < 0; ++t) {
        const int e = cands[pick(rng_)];
        const Edge& ed = graph_.edges[e];
        const double ss = u(rng_) * ed.length;
        const double d = std::hypot(ed.ax + ed.dx * ss - player.x, ed.ay + ed.dy * ss - player.y);
        if (rmin <= d && d <= rmax) { eid = e; s = ss; }
    }
    if (eid < 0) return false;
    const LanePoint p = graph_.lane_point(eid, s);
    const float fx = static_cast<float>(p.x), fy = static_cast<float>(p.y);
    if (occupied(p.x, p.y, player) || world_.height_at(fx, fy, 999.0f) - world_.ground(fx, fy) > 0.3f)
        return false;  // (not on an overpass deck or ramp: start on the ground)
    auto c = std::make_unique<AICar>(p.x, p.y, p.heading);
    c->colour = std::uniform_int_distribution<int>(0, kColours - 1)(rng_);
    c->z = world_.height_at(fx, fy, 0.0f);
    c->edge = eid;
    c->nxt = graph_.choose_next(eid, rng_);
    c->pace = std::uniform_real_distribution<double>(kPaceMin, kPaceMax)(rng_);
    const double v = graph_.edges[eid].cruise * c->pace;
    c->vx = std::cos(p.heading) * v;
    c->vy = std::sin(p.heading) * v;
    cars_.push_back(std::move(c));
    return true;
}

void Town::manage(const Car& player, bool fill) {
    if (graph_.edges.empty()) return;
    cars_.erase(std::remove_if(cars_.begin(), cars_.end(),
                               [&](const std::unique_ptr<AICar>& c) {
                                   return c->dead || std::hypot(c->x - player.x, c->y - player.y) > kTrafficDespawn;
                               }),
                cars_.end());
    int budget = local_target(player) - static_cast<int>(cars_.size());
    for (int tries = 0; budget > 0 && tries < (fill ? 40 : 8); ++tries)
        if (spawn_traffic(player, fill ? 30.0 : kSpawnMin, fill ? 190.0 : kSpawnMax)) --budget;
}

void Town::update(double dt, Car& player) {
    if (!filled_) {
        manage(player, true);
        filled_ = true;
    }
    mng_acc_ += dt;
    if (mng_acc_ >= kManageDt) {
        mng_acc_ = 0.0;
        manage(player, false);
    }
    ctl_acc_ += dt;
    if (ctl_acc_ >= kControlDt) {
        const double step = ctl_acc_;
        ctl_acc_ = 0.0;
        std::vector<const Car*> others;
        for (const auto& c : cars_) others.push_back(c.get());
        others.push_back(&player);
        auto next = [&](int eid) { return graph_.choose_next(eid, rng_); };
        for (auto& c : cars_) drive_traffic(*c, graph_, others, next, step);
    }
    for (auto& c : cars_) {
        const double px = c->x, py = c->y;
        c->update(dt, CarInput{c->throttle, c->brake, c->steer, false, false}, true);
        move_and_slide(*c, world_, px, py, false);
        settle_height(*c, world_, dt);
    }
    std::vector<Car*> everyone;
    for (auto& c : cars_) everyone.push_back(c.get());
    everyone.push_back(&player);
    for (std::size_t i = 0; i < everyone.size(); ++i)
        for (std::size_t j = i + 1; j < everyone.size(); ++j) {
            Car& a = *everyone[i];
            Car& b = *everyone[j];
            if (std::fabs(a.x - b.x) <= 6.0 && std::fabs(a.y - b.y) <= 6.0) collide_cars(a, b);
        }
}

}  // namespace amr
