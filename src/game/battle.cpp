#include "game/battle.h"

#include <algorithm>
#include <cmath>

namespace amr {

namespace {
constexpr double kHitSafe = 1.5, kSpinTime = 1.2, kSpinRate = 9.0, kRamSpeed = 11.0;
constexpr double kGhostTime = 6.0, kTurboTime = 2.0;
constexpr double kArenaX = 416.0, kArenaY = -880.0, kArenaRadius = 650.0;  // downtown, by Victoria Square
}  // namespace

Battle::Battle(const World& world, const RoadGraph& g, Mode mode, int players, unsigned seed)
    : world_(world), graph_(g), mode_(mode), n_(std::max(2, std::min(4, players))), rng_(seed), racers_(n_) {
    const MapData& m = world.map();
    double cx = kArenaX, cy = kArenaY;
    if (mode_ == Mode::Balloon) {
        bool downtown = false;  // on another map: the start point, wherever junctions are
        for (std::size_t n = 0; n < g.nodes.size() && !downtown; ++n)
            if (g.out[n].size() >= 3 && std::hypot(g.nodes[n].x - kArenaX, g.nodes[n].y - kArenaY) < 300.0) downtown = true;
        if (!downtown) { cx = m.spawn_x; cy = m.spawn_y; }
        std::vector<int> nodes;  // two junctions facing each other downtown
        for (int i = 0; i < static_cast<int>(g.nodes.size()); ++i) {
            const double d = std::hypot(g.nodes[i].x - cx, g.nodes[i].y - cy);
            if (!g.out[i].empty() && 90.0 < d && d < 260.0) nodes.push_back(i);
        }
        std::shuffle(nodes.begin(), nodes.end(), rng_);
        for (int i : nodes) {
            const double x = g.nodes[i].x, y = g.nodes[i].y;
            bool apart = true;
            for (std::size_t k = 0; k < sx_.size(); ++k)
                if (std::hypot(x - sx_[k], y - sy_[k]) <= 140.0) apart = false;
            if (apart) { sx_.push_back(x); sy_.push_back(y); sh_.push_back(std::atan2(cy - y, cx - x)); }
            if (static_cast<int>(sx_.size()) == n_) break;
        }
        while (static_cast<int>(sx_.size()) < n_) {
            sx_.push_back(m.spawn_x + 6.0 * sx_.size());
            sy_.push_back(m.spawn_y);
            sh_.push_back(m.spawn_heading);
        }
        boxes_ = box_sites(g, cx, cy, kArenaRadius, kBoxSpacing);
        banner_ = "BALLOON BATTLE!";
    } else {
        if (!m.sprint_targets.empty()) {
            const TourSpot& t = m.sprint_targets[std::uniform_int_distribution<std::size_t>(0, m.sprint_targets.size() - 1)(rng_)];
            tx_ = t.x; ty_ = t.y; target_label_ = t.label;
        } else {
            tx_ = m.spawn_x; ty_ = m.spawn_y; target_label_ = "THE START";
        }
        std::vector<int> nodes;  // start 1.2-2.6 km from the finish
        for (int i = 0; i < static_cast<int>(g.nodes.size()); ++i) {
            const double d = std::hypot(g.nodes[i].x - tx_, g.nodes[i].y - ty_);
            if (!g.out[i].empty() && 1200.0 < d && d < 2600.0) nodes.push_back(i);
        }
        if (nodes.empty())
            for (int i = 0; i < static_cast<int>(g.nodes.size()); ++i)
                if (!g.out[i].empty()) nodes.push_back(i);
        const int i = nodes[std::uniform_int_distribution<std::size_t>(0, nodes.size() - 1)(rng_)];
        const Edge& e = g.edges[g.out[i][0]];
        const double h = std::atan2(e.dy, e.dx), x = g.nodes[i].x + e.dx * 8.0, y = g.nodes[i].y + e.dy * 8.0;
        const double rx = -std::sin(h), ry = std::cos(h);
        const double sides[4] = {-1.6, 1.6, -1.6, 1.6};
        for (int k = 0; k < n_; ++k) {
            sx_.push_back(x + rx * sides[k]);
            sy_.push_back(y + ry * sides[k]);
            sh_.push_back(h);
        }
        const double reach = std::hypot(sx_[0] - tx_, sy_[0] - ty_) / 2 + 300.0;
        boxes_ = box_sites(g, (sx_[0] + tx_) / 2, (sy_[0] + ty_) / 2, reach, 110.0);
        banner_ = "RACE TO " + target_label_;
    }
}

void Battle::start(int i, double& x, double& y, double& heading) const {
    x = sx_[i];
    y = sy_[i];
    heading = sh_[i];
}

void Battle::adjust(int i, bool& throttle, bool& brake, double& steer, bool& handbrake, bool& bumper) const {
    const Racer& r = racers_[i];
    if (r.out || r.spin > 0.0 || winner_ != -2) {
        throttle = false;
        brake = r.out || winner_ != -2;
        steer = 0.0;
        handbrake = bumper = false;
    }
}

void Battle::roulette(Racer& r, double dt) {
    if (r.roulette <= 0.0) return;
    const int before = static_cast<int>(r.roulette * 12);
    r.roulette -= dt;
    if (static_cast<int>(std::max(0.0, r.roulette) * 12) != before) {
        r.shown = static_cast<Item>(before % kItemCount);
        sounds.push_back("tick");
    }
    if (r.roulette <= 0.0) {
        r.roulette = 0.0;
        r.item = pick_item(rng_);
        r.shown = r.item;
        if (r.item == Item::Triple) r.orbit = 3;
        sounds.push_back("item");
    }
}

void Battle::use_item(int i, Car& car) {
    Racer& r = racers_[i];
    switch (r.item) {
        case Item::None: return;
        case Item::Puck:
        case Item::Seeker:
            shots_.push_back(fire_shot(r.item, car, i));
            sounds.push_back("launch");
            r.item = Item::None;
            break;
        case Item::Triple:
            if (r.orbit > 0) {
                shots_.push_back(fire_shot(Item::Puck, car, i));
                sounds.push_back("launch");
                --r.orbit;
            }
            if (r.orbit <= 0) r.item = Item::None;
            break;
        case Item::Slick:
            slicks_.push_back(drop_slick(car, i));
            r.item = Item::None;
            break;
        case Item::Turbo:
            car.boost_t = std::max(car.boost_t, kTurboTime);
            car.vx += car.fx() * 4.0;
            car.vy += car.fy() * 4.0;
            sounds.push_back("boost");
            r.item = Item::None;
            break;
        case Item::Ghost:
            r.ghost = kGhostTime;
            sounds.push_back("boost");
            r.item = Item::None;
            break;
    }
    if (r.item == Item::None) r.shown = Item::None;
}

bool Battle::hurt(int i, bool spin) {
    Racer& r = racers_[i];
    if (r.ghost > 0.0 || r.safe > 0.0 || r.out) return false;
    if (spin) {
        r.spin = kSpinTime;
        sounds.push_back("spin");
    }
    r.safe = kHitSafe;
    if (mode_ == Mode::Balloon) {
        --r.balloons;
        sounds.push_back("pop");
        if (r.balloons <= 0) r.out = true;
    }
    return true;
}

void Battle::update(double dt, std::vector<Car*>& cars, const std::vector<bool>& fire,
                    const std::vector<const Car*>& town) {
    time_ += dt;
    for (int i = 0; i < n_; ++i) {
        Car& car = *cars[i];
        Racer& r = racers_[i];
        r.safe = std::max(0.0, r.safe - dt);
        if (r.spin > 0.0) {  // spinning out
            r.spin = std::max(0.0, r.spin - dt);
            car.heading += kSpinRate * dt;
            const double damp = std::exp(-2.5 * dt);
            car.vx *= damp;
            car.vy *= damp;
        }
        if (r.ghost > 0.0) {
            r.ghost = std::max(0.0, r.ghost - dt);
            car.boost_t = std::max(car.boost_t, 0.1);
        }
        roulette(r, dt);
        const bool f = i < static_cast<int>(fire.size()) && fire[i];
        if (f && !r.fire_held && !r.out && winner_ == -2) use_item(i, car);
        r.fire_held = f;
    }
    for (ItemBox& b : boxes_) {  // drive through a box to get an item
        if (!b.present()) {
            b.wait = std::max(0.0, b.wait - dt);
            continue;
        }
        for (int i = 0; i < n_; ++i) {
            Racer& r = racers_[i];
            if (r.out || r.item != Item::None || r.roulette > 0.0) continue;
            if ((cars[i]->x - b.x) * (cars[i]->x - b.x) + (cars[i]->y - b.y) * (cars[i]->y - b.y) < kBoxReach * kBoxReach) {
                b.wait = kBoxRespawn;
                r.roulette = kRouletteTime;
                break;
            }
        }
    }
    std::vector<Shot> keep;  // shots: fly, hit, get blocked by orbiting pucks, or break on a town car
    for (Shot& s : shots_) {
        std::vector<std::pair<int, const Car*>> rivals;
        for (int j = 0; j < n_; ++j) {
            const bool shielded = racers_[j].orbit && j != s.owner;
            if (!racers_[j].out && (j != s.owner || s.age > kOwnerSafe * 2) && !shielded) rivals.push_back({j, cars[j]});
        }
        const int bounces = s.bounces;
        const int res = step_shot(s, world_, dt, rivals);
        if (s.bounces > bounces) sounds.push_back("ricochet");
        if (res < 0) continue;
        if (res > 0) {
            hurt(res - 1, true);
            continue;
        }
        bool blocked = false;
        for (int j = 0; j < n_ && !blocked; ++j) {
            Racer& r = racers_[j];
            const double reach = kOrbitRadius + kOrbitBlock / 2;
            if (r.orbit && j != s.owner &&
                (cars[j]->x - s.x) * (cars[j]->x - s.x) + (cars[j]->y - s.y) * (cars[j]->y - s.y) < reach * reach) {
                blocked = true;  // one of the circling pucks takes the hit
                --r.orbit;
                if (r.orbit <= 0 && r.item == Item::Triple) r.item = r.shown = Item::None;
            }
        }
        for (const Car* c : town)
            if ((c->x - s.x) * (c->x - s.x) + (c->y - s.y) * (c->y - s.y) < 4.0) blocked = true;
        if (blocked) sounds.push_back("ricochet");
        else keep.push_back(s);
    }
    shots_ = std::move(keep);
    std::vector<Slick> still;  // slicks: whoever drives over one spins out
    for (Slick& sl : slicks_) {
        sl.age += dt;
        sl.life -= dt;
        if (sl.life <= 0.0) continue;
        bool hit = false;
        for (int j = 0; j < n_; ++j) {
            if (j == sl.owner && sl.age < kSlickSafe) continue;
            const double d2 = (cars[j]->x - sl.x) * (cars[j]->x - sl.x) + (cars[j]->y - sl.y) * (cars[j]->y - sl.y);
            if (d2 < kSlickRadius * kSlickRadius && !racers_[j].out && hurt(j, true)) hit = true;
        }
        if (!hit) still.push_back(sl);
    }
    slicks_ = std::move(still);
    for (int i = 0; i < n_; ++i)  // rams: the faster car pops the other's balloon (any ram while boosted or ghosted)
        for (int j = i + 1; j < n_; ++j) {
            const Car& a = *cars[i];
            const Car& b = *cars[j];
            if (racers_[i].out || racers_[j].out) continue;
            const double dx = b.x - a.x, dy = b.y - a.y, d = std::hypot(dx, dy);
            if (d > 3.4 || d < 1e-6) continue;
            const double ux = dx / d, uy = dy / d, va = a.vx * ux + a.vy * uy, vb = -(b.vx * ux + b.vy * uy);
            const int att[2] = {i, j}, vic[2] = {j, i};
            const double v_att[2] = {va, vb}, v_vic[2] = {vb, va};
            for (int k = 0; k < 2; ++k) {
                const bool strong = racers_[att[k]].ghost > 0.0 || cars[att[k]]->boost_t > 0.0;
                if (v_att[k] > v_vic[k] && (strong || v_att[k] + v_vic[k] > kRamSpeed)) {
                    hurt(vic[k], strong);
                    break;
                }
            }
        }
    const bool racing = winner_ == -2;
    if (winner_ == -2) {
        if (mode_ == Mode::Balloon) {
            int alive = 0, last = -1;
            for (int i = 0; i < n_; ++i)
                if (!racers_[i].out) { ++alive; last = i; }
            if (alive <= 1) winner_ = alive ? last : -1;
        } else {
            for (int i = 0; i < n_; ++i)
                if (std::hypot(cars[i]->x - tx_, cars[i]->y - ty_) < kFinish) { winner_ = i; break; }
        }
    }
    if (racing && winner_ != -2) sounds.push_back("win");
    if (banner_left_ > 0) {
        banner_left_ -= dt;
        if (banner_left_ <= 0) banner_.clear();
    }
}

}  // namespace amr
