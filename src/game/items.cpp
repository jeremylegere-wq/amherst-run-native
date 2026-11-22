#include "game/items.h"

#include <algorithm>
#include <cmath>

namespace amr {

namespace {
constexpr double kPuckSpeed = 34.0, kPuckLife = 7.0, kSeekerSpeed = 30.0, kSeekerLife = 9.0, kSeekerTurn = 3.2;
constexpr int kPuckBounces = 4;
constexpr double kShotHit = 1.9;
constexpr double kWeights[kItemCount] = {3.0, 2.0, 3.0, 3.0, 1.0, 1.5};
const char* kNames[kItemCount] = {"PUCK", "SEEKER", "OIL SLICK", "TURBO CAN", "GHOST MODE", "TRIPLE PUCK"};
const std::uint8_t kColours[kItemCount][3] = {{70, 210, 230}, {240, 70, 60}, {34, 30, 40},
                                              {255, 150, 30}, {236, 240, 255}, {70, 210, 230}};
constexpr double kPi = 3.14159265358979323846;

bool blocked(const World& w, double x, double y) { return w.in_building(x, y) || !w.in_bounds(x, y); }
}  // namespace

const char* item_name(Item k) { return k == Item::None ? "" : kNames[static_cast<int>(k)]; }
const std::uint8_t* item_colour(Item k) { return kColours[k == Item::None ? 0 : static_cast<int>(k)]; }

Item pick_item(std::mt19937& rng) {
    double total = 0;
    for (double w : kWeights) total += w;
    double r = std::uniform_real_distribution<double>(0.0, total)(rng);
    for (int k = 0; k < kItemCount; ++k) {
        r -= kWeights[k];
        if (r <= 0.0) return static_cast<Item>(k);
    }
    return Item::Triple;
}

std::vector<ItemBox> box_sites(const RoadGraph& g, double cx, double cy, double radius, double spacing, std::size_t limit) {
    std::vector<int> nodes;
    for (int n = 0; n < static_cast<int>(g.nodes.size()); ++n)
        if (g.out[n].size() >= 3) nodes.push_back(n);
    auto d2 = [&](int n) { return (g.nodes[n].x - cx) * (g.nodes[n].x - cx) + (g.nodes[n].y - cy) * (g.nodes[n].y - cy); };
    std::stable_sort(nodes.begin(), nodes.end(), [&](int a, int b) { return d2(a) < d2(b); });
    std::vector<ItemBox> out;
    for (int n : nodes) {
        const double x = g.nodes[n].x, y = g.nodes[n].y;
        if (std::hypot(x - cx, y - cy) > radius) break;
        bool apart = true;
        for (const ItemBox& b : out)
            if (std::hypot(x - b.x, y - b.y) < spacing) apart = false;
        if (apart) {
            out.push_back({x, y});
            if (out.size() >= limit) break;
        }
    }
    return out;
}

Shot fire_shot(Item kind, const Car& car, int owner) {
    const double speed = kind == Item::Puck ? kPuckSpeed : kSeekerSpeed;
    Shot s{kind, car.x + car.fx() * 3.2, car.y + car.fy() * 3.2, std::cos(car.heading) * speed,
           std::sin(car.heading) * speed, kind == Item::Puck ? kPuckLife : kSeekerLife, 0.0, owner, 0};
    return s;
}

Slick drop_slick(const Car& car, int owner) { return Slick{car.x - car.fx() * 3.4, car.y - car.fy() * 3.4, owner}; }

std::vector<std::pair<double, double>> orbit_points(const Car& car, int count, double t) {
    std::vector<std::pair<double, double>> out;
    for (int k = 0; k < count; ++k) {
        const double a = t * kOrbitSpin + k * 2 * kPi / 3;
        out.push_back({car.x + kOrbitRadius * std::cos(a), car.y + kOrbitRadius * std::sin(a)});
    }
    return out;
}

int step_shot(Shot& s, const World& w, double dt, const std::vector<std::pair<int, const Car*>>& rivals) {
    s.age += dt;
    s.life -= dt;
    if (s.life <= 0.0) return -1;
    if (s.kind == Item::Seeker && !rivals.empty()) {  // turn towards the nearest rival
        const Car* target = nullptr;
        double best = 0;
        for (const auto& r : rivals) {
            const double d = (r.second->x - s.x) * (r.second->x - s.x) + (r.second->y - s.y) * (r.second->y - s.y);
            if (!target || d < best) { target = r.second; best = d; }
        }
        const double want = std::atan2(target->y - s.y, target->x - s.x);
        double have = std::atan2(s.vy, s.vx);
        double turn = std::fmod(want - have + kPi, 2 * kPi);
        if (turn < 0) turn += 2 * kPi;
        turn -= kPi;
        have += std::max(-kSeekerTurn * dt, std::min(kSeekerTurn * dt, turn));
        s.vx = std::cos(have) * kSeekerSpeed;
        s.vy = std::sin(have) * kSeekerSpeed;
    }
    double nx = s.x + s.vx * dt, ny = s.y + s.vy * dt;
    if (blocked(w, nx, ny)) {
        if (s.kind != Item::Puck || s.bounces >= kPuckBounces) return -1;
        ++s.bounces;
        const bool flip_x = blocked(w, nx, s.y), flip_y = blocked(w, s.x, ny);
        if (flip_x) s.vx = -s.vx;
        if (flip_y) s.vy = -s.vy;
        if (!flip_x && !flip_y) {  // straight into a corner: back the way it came
            s.vx = -s.vx;
            s.vy = -s.vy;
        }
        nx = s.x + s.vx * dt;
        ny = s.y + s.vy * dt;
        if (blocked(w, nx, ny)) return -1;
    }
    s.x = nx;
    s.y = ny;
    for (const auto& r : rivals)
        if ((r.second->x - s.x) * (r.second->x - s.x) + (r.second->y - s.y) * (r.second->y - s.y) < kShotHit * kShotHit)
            return 1 + r.first;
    return 0;
}

}  // namespace amr
