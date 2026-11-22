#include "game/missions.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace amr {

namespace {
const Rgb8 kHubColour[4] = {{255, 150, 30}, {236, 52, 52}, {80, 220, 120}, {80, 150, 255}};
const char* kHubTitle[4] = {"Parcel Run", "Getaway", "Heritage Tour", "Pay Fines"};
constexpr double kHubRadius = 5.0, kDropRadius = 7.0, kCheckRadius = 10.0, kDropSpeed = 6.0;
constexpr int kFinePerStar = 100, kBustedFine = 100;
constexpr double kBannerTime = 3.5, kCruise = 12.0, kFragileDamage = 25.0, kTourRadius = 900.0;
constexpr double kSpawnW[3] = {0.2, 2.0, 3.0};  // lane, side, main (missions.py SPAWN_W)
const Rgb8 kRed{236, 52, 52}, kGrey{200, 200, 210}, kGreen{130, 250, 100};
constexpr double kAddrCell = 50.0;
long long cell_key(int gx, int gy) { return (static_cast<long long>(gx) << 32) ^ static_cast<unsigned>(gy); }
std::string upper(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}
}  // namespace

MissionManager::MissionManager(const World& world, const RoadGraph& graph, Town& town, unsigned seed)
    : world_(world), graph_(graph), town_(town), rng_(seed), hubs_(world.map().hubs) {
    const auto& a = world.map().addresses;
    for (int i = 0; i < static_cast<int>(a.size()); ++i)
        addr_cells_[cell_key(static_cast<int>(std::floor(a[i].x / kAddrCell)), static_cast<int>(std::floor(a[i].y / kAddrCell)))]
            .push_back(i);
}

void MissionManager::say(const std::string& text, Rgb8 colour) {
    banner_ = text;
    banner_colour_ = colour;
    banner_left_ = kBannerTime;
}

std::vector<int> MissionManager::hubs_in_use() const {  // with the cops off: no getaways and no fines
    std::vector<int> out;
    for (int i = 0; i < static_cast<int>(hubs_.size()); ++i)
        if (town_.cops() || (hubs_[i].role != 1 && hubs_[i].role != 3)) out.push_back(i);
    return out;
}

double MissionManager::route_len(double ax, double ay, double bx, double by) const {
    const int na = graph_.nearest_node(ax, ay, 120.0), nb = graph_.nearest_node(bx, by, 120.0);
    const std::vector<int> path = (na >= 0 && nb >= 0) ? graph_.shortest_path(na, nb) : std::vector<int>{};
    if (path.empty()) return std::hypot(bx - ax, by - ay) * 1.4;
    double total = 0, px = ax, py = ay;
    for (int n : path) {
        total += std::hypot(graph_.nodes[n].x - px, graph_.nodes[n].y - py);
        px = graph_.nodes[n].x;
        py = graph_.nodes[n].y;
    }
    return total + std::hypot(bx - px, by - py);
}

std::string MissionManager::address(double x, double y) const {
    const auto& m = world_.map();
    bool found = false;
    double best_d = 0, best_along = 0;
    bool best_left = false;
    std::uint32_t best_name = 0;
    for (const Road& r : m.roads) {
        if (r.name >= m.names.size() || m.names[r.name].empty()) continue;
        double along = 0;
        for (std::size_t i = 0; i + 1 < r.points.size(); ++i) {
            const double ax = r.points[i].x, ay = r.points[i].y, dx = r.points[i + 1].x - ax, dy = r.points[i + 1].y - ay;
            const double seg = std::hypot(dx, dy);
            double l2 = dx * dx + dy * dy;
            if (l2 == 0) l2 = 1.0;
            const double t = std::max(0.0, std::min(1.0, ((x - ax) * dx + (y - ay) * dy) / l2));
            const double d = std::hypot(x - ax - t * dx, y - ay - t * dy);
            if (!found || d < best_d) {
                found = true;
                best_d = d;
                best_name = r.name;
                best_along = along + t * seg;
                best_left = (dx * (y - ay) - dy * (x - ax)) >= 0;
            }
            along += seg;
        }
    }
    if (!found || best_d >= 40) return "the address";
    const std::string& street = m.names[best_name];
    // the nearest real address on that street within 45 m (game/addresses.py Addresses.near)
    double near_d = 45.0;
    const CivicAddress* real = nullptr;
    const int k = static_cast<int>(std::floor(45.0 / kAddrCell)) + 1;
    const int cx = static_cast<int>(std::floor(x / kAddrCell)), cy = static_cast<int>(std::floor(y / kAddrCell));
    for (int gx = cx - k; gx <= cx + k; ++gx)
        for (int gy = cy - k; gy <= cy + k; ++gy) {
            auto it = addr_cells_.find(cell_key(gx, gy));
            if (it == addr_cells_.end()) continue;
            for (int i : it->second) {
                const CivicAddress& a = m.addresses[i];
                if (m.names[a.street] != street) continue;
                const double d = std::hypot(a.x - x, a.y - y);
                if (d < near_d) { real = &a; near_d = d; }
            }
        }
    if (real) return real->number + " " + street;
    const int number = static_cast<int>(std::floor(best_along / 9.0)) * 2 + (best_left ? 2 : 1) + 10;
    return std::to_string(number) + " " + street;
}

bool MissionManager::road_point_near(double x, double y, double rmin, double rmax, double& px, double& py) {
    // RoadGraph.random_spawn with the job weights and 20 tries
    const std::vector<int> cands = graph_.edges_near(x, y, rmax);
    if (cands.empty()) return false;
    std::vector<double> w;
    for (int i : cands) w.push_back(kSpawnW[graph_.edges[i].type] + 0.001);
    std::discrete_distribution<int> pick(w.begin(), w.end());
    std::uniform_real_distribution<double> u(0.1, 0.9);
    for (int t = 0; t < 20; ++t) {
        const int e = cands[pick(rng_)];
        const Edge& ed = graph_.edges[e];
        const double s = u(rng_) * ed.length;
        const double d = std::hypot(ed.ax + ed.dx * s - x, ed.ay + ed.dy * s - y);
        if (rmin <= d && d <= rmax) {
            const LanePoint p = graph_.lane_point(e, s);
            px = p.x;
            py = p.y;
            return true;
        }
    }
    return false;
}

bool MissionManager::make_delivery(int h, Job& out) {
    const HubSpot& hub = hubs_[h];
    static const int kCounts[4] = {1, 2, 2, 3};
    const int n = kCounts[std::uniform_int_distribution<int>(0, 3)(rng_)];
    const bool fragile = std::uniform_real_distribution<double>(0.0, 1.0)(rng_) < 0.3;
    double px = hub.x, py = hub.y, total = 0;
    out = Job{};
    for (int k = 0; k < n; ++k) {
        double tx, ty;
        if (!road_point_near(px, py, 180.0, 600.0, tx, ty)) break;
        total += route_len(px, py, tx, ty);
        out.targets.push_back({tx, ty, kDropRadius, address(tx, ty), true});
        px = tx;
        py = ty;
    }
    if (out.targets.empty()) return false;
    const int count = static_cast<int>(out.targets.size());
    out.kind = Job::Delivery;
    out.title = fragile ? "Fragile Parcel" : (count == 1 ? "Parcel Run" : "Parcel Run x" + std::to_string(count));
    out.colour = kHubColour[0];
    out.time_limit = out.time_left = total / kCruise + 12.0 * count;
    out.reward = 120 + 60 * count + (fragile ? 80 : 0);
    out.fragile = fragile;
    return true;
}

bool MissionManager::make_tour(int h, Job& out) {
    const HubSpot& hub = hubs_[h];
    struct P {
        std::string label;
        double x, y;
    };
    std::vector<P> pts;
    for (const TourSpot& s : world_.map().tour_spots) {
        const double d_hub = std::hypot(s.x - hub.x, s.y - hub.y);
        bool apart = true;
        for (const P& q : pts)
            if (std::hypot(s.x - q.x, s.y - q.y) <= 40) apart = false;
        if (60 < d_hub && d_hub < kTourRadius && apart) pts.push_back({s.label, s.x, s.y});
    }
    std::vector<P> order;  // visit them nearest-first from the hub
    double cx = hub.x, cy = hub.y;
    while (!pts.empty() && order.size() < 5) {
        auto it = std::min_element(pts.begin(), pts.end(), [&](const P& a, const P& b) {
            return std::hypot(a.x - cx, a.y - cy) < std::hypot(b.x - cx, b.y - cy);
        });
        order.push_back(*it);
        cx = it->x;
        cy = it->y;
        pts.erase(it);
    }
    while (order.size() < 3) {
        double px, py;
        if (!road_point_near(cx, cy, 150.0, 400.0, px, py)) break;
        order.push_back({"Checkpoint", px, py});
        cx = px;
        cy = py;
    }
    if (order.size() < 2) return false;
    out = Job{};
    out.kind = Job::Tour;
    out.title = "Heritage Tour";
    out.colour = kHubColour[2];
    double total = 0, px = hub.x, py = hub.y;
    for (const P& p : order) {
        out.targets.push_back({p.x, p.y, kCheckRadius, p.label, false});
        total += route_len(px, py, p.x, p.y);
        px = p.x;
        py = p.y;
    }
    // calibrated on the real town: a careful drive with no traffic finishes with ~25% to spare
    out.time_limit = out.time_left = total / 11.0 + 12.0 * out.targets.size();
    out.reward = 200 + 40 * static_cast<int>(out.targets.size());
    return true;
}

void MissionManager::enter_hub(int h, const Car& /*player*/) {
    const HubSpot& hub = hubs_[h];
    if (hub.role == 3) {  // the court house
        if (town_.heat() <= 0) {
            say("NO FINES DUE", kGrey);
        } else {
            const int cost = kFinePerStar * town_.stars();
            if (cash_ >= cost) {
                cash_ -= cost;
                town_.add_heat(-10.0);
                say("FINE PAID  -$" + std::to_string(cost), kHubColour[3]);
            } else {
                say("FINE IS $" + std::to_string(cost), kRed);
            }
        }
        return;
    }
    if (town_.heat() > 0) {
        say("LOSE THE COPS FIRST", kRed);
        return;
    }
    Job j;
    bool ok = false;
    if (hub.role == 0) {
        ok = make_delivery(h, j);
    } else if (hub.role == 1) {
        town_.add_heat(2.6);
        j.kind = Job::Getaway;
        j.title = "Getaway";
        j.colour = kHubColour[1];
        j.time_limit = j.time_left = 120.0;
        j.reward = 450;
        ok = true;
    } else {
        ok = make_tour(h, j);
    }
    if (!ok) {
        say("NO JOBS RIGHT NOW", kGrey);
        return;
    }
    j.start_damage = town_.damage();
    job_ = {true, j};
    say(upper(j.title), j.colour);
}

void MissionManager::update(double dt, const Car& player) {
    const TownState st = town_.state();
    if (!banner_.empty() && st == TownState::Playing) {  // messages wait while BUSTED / WRECKED is on screen
        banner_left_ -= dt;
        if (banner_left_ <= 0) banner_.clear();
    }
    if (st != prev_state_ && st != TownState::Playing) {
        const bool lost = job_.has_value;
        job_.has_value = false;
        if (st == TownState::Busted) {
            const int fine = std::min(cash_, kBustedFine);
            cash_ -= fine;
            say(std::string(lost ? "JOB LOST  " : "") + (fine ? "FINED -$" + std::to_string(fine) : "NO CASH TO FINE"), kRed);
        } else if (lost) {
            say("JOB LOST", kRed);
        }
    }
    prev_state_ = st;
    if (st != TownState::Playing) return;
    if (job_.has_value) update_job(dt, player);
    else check_hubs(player);
}

void MissionManager::check_hubs(const Car& player) {
    for (int h : hubs_in_use()) {
        const double d = std::hypot(player.x - hubs_[h].x, player.y - hubs_[h].y);
        if (d < kHubRadius && player.speed() < kDropSpeed) {
            if (inside_ != h) {
                inside_ = h;
                enter_hub(h, player);
            }
            return;
        }
    }
    if (inside_ >= 0 && std::hypot(player.x - hubs_[inside_].x, player.y - hubs_[inside_].y) > kHubRadius + 5.0)
        inside_ = -1;
}

void MissionManager::update_job(double dt, const Car& player) {
    Job& m = job_.value;
    m.time_left -= dt;
    m.elapsed += dt;
    if (m.kind == Job::Getaway) {
        if (town_.heat() <= 0.0 && m.elapsed > 2.0) {
            complete();
            return;
        }
    } else if (m.stage < m.targets.size()) {
        const JobTarget& t = m.targets[m.stage];
        if (std::hypot(player.x - t.x, player.y - t.y) < t.radius && (!t.stop || player.speed() < kDropSpeed)) {
            ++m.stage;
            if (m.stage >= m.targets.size()) {
                complete();
                return;
            }
            say(m.kind == Job::Delivery ? "DELIVERED!" : "CHECKPOINT!", m.colour);
        }
    }
    if (m.fragile && town_.damage() - m.start_damage > kFragileDamage) fail("PARCEL SMASHED");
    else if (m.time_left <= 0.0) fail("OUT OF TIME");
}

void MissionManager::complete() {
    const Job& m = job_.value;
    const int pay = m.reward + 2 * static_cast<int>(std::max(0.0, m.time_left));
    cash_ += pay;
    ++completed_;
    job_.has_value = false;
    say("JOB DONE  +$" + std::to_string(pay), kGreen);
}

void MissionManager::fail(const std::string& reason) {
    job_.has_value = false;
    say(reason, kRed);
}

void MissionManager::abort() {
    job_.has_value = false;
    inside_ = -1;
}

bool MissionManager::current_target(double& x, double& y) const {
    if (!job_.has_value || job_.value.stage >= job_.value.targets.size()) return false;
    x = job_.value.targets[job_.value.stage].x;
    y = job_.value.targets[job_.value.stage].y;
    return true;
}

std::string MissionManager::objective() const {
    if (!job_.has_value) return "";
    const Job& m = job_.value;
    if (m.kind == Job::Getaway) return "Lose the police!";
    const JobTarget& t = m.targets[std::min(m.stage, m.targets.size() - 1)];
    const std::string n = std::to_string(m.stage + 1) + "/" + std::to_string(m.targets.size());
    if (m.kind == Job::Delivery)
        return std::string(m.fragile ? "Fragile! " : "") + "Deliver to " + t.label + " (" + n + ") - stop at the marker";
    return "Checkpoint " + n + ": " + t.label;
}

std::string MissionManager::hint(const Car& player) const {
    for (int h : hubs_in_use()) {
        const HubSpot& hub = hubs_[h];
        if (std::hypot(player.x - hub.x, player.y - hub.y) < 30.0) {
            if (hub.role == 3) {
                const int stars = town_.stars();
                return stars ? hub.label + ": fine $" + std::to_string(kFinePerStar * stars) : hub.label + ": no fines due";
            }
            return hub.label + ": drive into the marker for " + kHubTitle[hub.role];
        }
    }
    return "";
}

std::vector<Marker> MissionManager::markers() const {
    std::vector<Marker> out;
    if (!job_.has_value) {
        for (int h : hubs_in_use()) out.push_back({hubs_[h].x, hubs_[h].y, kHubRadius, kHubColour[hubs_[h].role], true});
    } else if (job_.value.stage < job_.value.targets.size()) {
        const JobTarget& t = job_.value.targets[job_.value.stage];
        out.push_back({t.x, t.y, t.radius, job_.value.colour, false});
    }
    return out;
}

}  // namespace amr
