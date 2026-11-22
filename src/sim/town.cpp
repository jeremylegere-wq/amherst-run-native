#include "sim/town.h"

#include <algorithm>
#include <cmath>

#include "sim/collision.h"

namespace amr {

namespace {
constexpr double kControlDt = 0.05, kManageDt = 0.25;
constexpr double kTrafficDespawn = 260.0, kPoliceDespawn = 320.0;
constexpr double kMaxHeat = 4.0;
constexpr double kPaceMin = 0.88, kPaceMax = 1.06;
constexpr double kSightRange = 140.0;
constexpr double kHitHeat = 0.6, kHitMinImpact = 4.0;
constexpr double kLoseGrace = 8.0, kHeatDecay = 0.15;
constexpr double kBustedRange = 8.0, kBustedSpeed = 4.0, kBustedTime = 2.5, kResetDelay = 3.5;
constexpr double kDamagePerMs = 1.2;
constexpr double kTrafficArea = 200.0, kDenseRoad = 4400.0;
constexpr int kMinTraffic = 3, kColours = 8;
constexpr double kSpawnMin = 150.0, kSpawnMax = 230.0;
constexpr double kTrafficW[3] = {0.3, 2.0, 3.0};  // lane, side, main (TRAFFIC_W)
constexpr double kPoliceW[3] = {0.0, 2.0, 3.0};   // (POLICE_W)
}  // namespace

Town::Town(const World& world, const RoadGraph& graph, Junctions* junctions, unsigned seed, int traffic, bool cops)
    : world_(world), graph_(graph), junctions_(junctions), rng_(seed), target_(traffic), cops_(cops) {}

void Town::reset() {
    cars_.clear();
    time_ = heat_ = damage_ = state_t_ = busted_t_ = hit_cd_ = spawn_cd_ = lose_t_ = 0.0;
    mng_acc_ = ctl_acc_ = 0.0;
    state_ = TownState::Playing;
    seen_ = filled_ = reset_flag_ = false;
    events.clear();
}

bool Town::consume_reset() {
    const bool f = reset_flag_;
    reset_flag_ = false;
    return f;
}

int Town::police_count() const {
    return static_cast<int>(std::count_if(cars_.begin(), cars_.end(), [](const auto& c) { return c->police; }));
}

int Town::stars() const { return static_cast<int>(std::ceil(heat_ - 1e-9)); }

void Town::set_cops(bool on) {
    cops_ = on;
    if (!cops_) {
        heat_ = 0.0;
        busted_t_ = 0.0;
        cars_.erase(std::remove_if(cars_.begin(), cars_.end(), [](const auto& c) { return c->police; }), cars_.end());
    }
}

void Town::add_heat(double amount) {
    if (!cops_) return;
    heat_ = std::max(0.0, std::min(kMaxHeat, heat_ + amount));
    lose_t_ = 0.0;
}

double Town::player_power() const { return 1.0 - 0.35 * std::min(1.0, damage_ / 100.0); }

void Town::hurt(double impact) {
    if (state_ == TownState::Playing) damage_ = std::min(100.0, damage_ + std::max(0.0, impact - 3.0) * kDamagePerMs);
}

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

// RoadGraph.random_spawn: a random spot rmin..rmax from the player, busier roads more likely.
bool Town::random_spawn(const Car& player, double rmin, double rmax, const double weights[3], int& eid, double& s) {
    const std::vector<int> cands = graph_.edges_near(player.x, player.y, rmax);
    if (cands.empty()) return false;
    std::vector<double> w;
    for (int i : cands) w.push_back(weights[graph_.edges[i].type] + 0.001);
    std::discrete_distribution<int> pick(w.begin(), w.end());
    std::uniform_real_distribution<double> u(0.1, 0.9);
    for (int t = 0; t < 12; ++t) {
        const int e = cands[pick(rng_)];
        const Edge& ed = graph_.edges[e];
        const double ss = u(rng_) * ed.length;
        const double d = std::hypot(ed.ax + ed.dx * ss - player.x, ed.ay + ed.dy * ss - player.y);
        if (rmin <= d && d <= rmax) {
            eid = e;
            s = ss;
            return true;
        }
    }
    return false;
}

bool Town::spawn_traffic(const Car& player, double rmin, double rmax) {
    int eid;
    double s;
    if (!random_spawn(player, rmin, rmax, kTrafficW, eid, s)) return false;
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

bool Town::spawn_police(const Car& player) {
    const double fx = player.fx(), fy = player.fy();
    bool have = false;
    LanePoint choice{};
    for (int k = 0; k < 6; ++k) {
        int eid;
        double s;
        if (!random_spawn(player, 70.0, 120.0, kPoliceW, eid, s)) continue;
        const LanePoint p = graph_.lane_point(eid, s);
        if (occupied(p.x, p.y, player)) continue;
        choice = p;
        have = true;
        if ((p.x - player.x) * fx + (p.y - player.y) * fy < 0) break;  // behind the player: take it
    }
    if (!have) return false;
    auto c = std::make_unique<AICar>(choice.x, choice.y, choice.heading, true);
    c->z = world_.height_at(static_cast<float>(choice.x), static_cast<float>(choice.y), 0.0f);
    c->path_t = std::uniform_real_distribution<double>(0.0, 1.0)(rng_);  // stagger route planning
    c->vx = std::cos(choice.heading) * 10.0;
    c->vy = std::sin(choice.heading) * 10.0;
    cars_.push_back(std::move(c));
    return true;
}

void Town::manage(const Car& player, bool fill) {
    if (graph_.edges.empty()) return;
    const double fx = player.fx(), fy = player.fy();
    std::vector<std::unique_ptr<AICar>> keep;
    for (auto& c : cars_) {
        const double dx = c->x - player.x, dy = c->y - player.y, d = std::hypot(dx, dy);
        bool gone;
        if (!c->police) {
            gone = c->dead || d > kTrafficDespawn;
        } else {
            gone = c->dead || d > kPoliceDespawn;
            if (heat_ <= 0.0 && state_ == TownState::Playing) {  // standing down: leave when out of the way
                c->stand_t += kManageDt;
                const bool behind = dx * fx + dy * fy < 0;
                gone = gone || d > 60.0 || (behind && d > 25.0) || c->stand_t > 6.0;
            }
        }
        if (!gone) keep.push_back(std::move(c));
    }
    cars_ = std::move(keep);
    seen_ = false;  // are the police watching the player?
    for (const auto& c : cars_) {
        if (!c->police) continue;
        const double d = std::hypot(c->x - player.x, c->y - player.y);
        if (d < 25.0 || (d < kSightRange && clear_line(world_, c->x, c->y, player.x, player.y))) {
            seen_ = true;
            break;
        }
    }
    const int n = static_cast<int>(cars_.size()) - police_count();
    int budget = local_target(player) - n;
    for (int tries = 0; budget > 0 && tries < (fill ? 40 : 8); ++tries)
        if (spawn_traffic(player, fill ? 30.0 : kSpawnMin, fill ? 190.0 : kSpawnMax)) --budget;
    if (state_ == TownState::Playing && heat_ > 0.0 && spawn_cd_ <= 0.0 && police_count() < std::min(4, stars()))
        if (spawn_police(player)) spawn_cd_ = 2.5;
}

void Town::control(double dt, const Car& player) {
    std::vector<const Car*> civ_others, cop_others;
    for (const auto& c : cars_) {
        civ_others.push_back(c.get());
        cop_others.push_back(c.get());
    }
    civ_others.push_back(&player);
    const bool active = heat_ > 0.0 && state_ == TownState::Playing;
    const double v_cap = 26.0 + 2.5 * std::min(4, stars());
    auto next = [&](int eid) { return graph_.choose_next(eid, rng_); };
    auto dice = [&] { return std::uniform_real_distribution<double>(0.0, 1.0)(rng_); };
    for (auto& c : cars_) {
        if (!c->police) drive_traffic(*c, graph_, civ_others, next, dt, junctions_, time_);
        else drive_police(*c, graph_, world_, player, cop_others, dt, dice, active, v_cap);
    }
}

void Town::on_crash(const Car* a, const Car* b, double impact, const Car& player) {
    if (a != &player && b != &player) return;
    const Car* other = a == &player ? b : a;
    if (state_ != TownState::Playing) return;
    hurt(impact);
    const auto* ai = dynamic_cast<const AICar*>(other);
    if (ai && !ai->police && impact > kHitMinImpact && hit_cd_ <= 0.0) {
        add_heat(kHitHeat);
        hit_cd_ = 1.5;
        events.push_back("hit");
    }
    if (events.size() > 20) events.erase(events.begin(), events.end() - 20);
}

void Town::end(TownState s, const char* event) {
    state_ = s;
    state_t_ = 0.0;
    events.push_back(event);
}

void Town::update_state(double dt, const Car& player) {
    if (state_ != TownState::Playing) {
        state_t_ += dt;
        if (state_t_ > kResetDelay) reset_flag_ = true;
        return;
    }
    if (heat_ > 0.0) {
        if (seen_) lose_t_ = 0.0;
        else lose_t_ += dt;
        if (lose_t_ > kLoseGrace) heat_ = std::max(0.0, heat_ - kHeatDecay * dt);
        bool near = false;
        for (const auto& c : cars_)
            if (c->police && std::hypot(c->x - player.x, c->y - player.y) < kBustedRange) near = true;
        if (near && player.speed() < kBustedSpeed) busted_t_ += dt;
        else busted_t_ = std::max(0.0, busted_t_ - 1.5 * dt);
        if (busted_t_ > kBustedTime) end(TownState::Busted, "busted");
    } else {
        busted_t_ = 0.0;
    }
    if (damage_ >= 100.0) end(TownState::Wrecked, "wrecked");
}

void Town::update(double dt, Car& player, double wall_impact) {
    time_ += dt;
    hit_cd_ = std::max(0.0, hit_cd_ - dt);
    spawn_cd_ = std::max(0.0, spawn_cd_ - dt);
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
        control(step, player);
    }
    if (wall_impact > 0.0) hurt(wall_impact);
    for (auto& c : cars_) {
        const double px = c->x, py = c->y;
        const bool on_road = c->police ? world_.on_road(c->x, c->y) : true;
        c->update(dt, CarInput{c->throttle, c->brake, c->steer, false, false}, on_road);
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
            if (std::fabs(a.x - b.x) > 6.0 || std::fabs(a.y - b.y) > 6.0) continue;
            const double imp = collide_cars(a, b);
            if (imp > 0.8) on_crash(&a, &b, imp, player);
        }
    update_state(dt, player);
}

}  // namespace amr
