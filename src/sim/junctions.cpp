#include "sim/junctions.h"

#include <algorithm>
#include <cmath>

namespace amr {

namespace {
constexpr double kLookAhead = 90.0, kActRange = 45.0, kBrake = 4.0, kCreep = 5.0, kGap = 3.0;
constexpr double kStopHold = 0.8, kImpatient = 4.0;
constexpr double kSignalCycle = 16.0, kSignalGreen = 7.0;

// Target speed that brings a car to rest at the line (speed_control only brakes 1.2 m/s over target).
double stop_speed(double d_line) { return std::max(0.0, std::sqrt(2.0 * kBrake * std::max(0.0, d_line - 1.0)) - 1.2); }
double creep_speed(double d_line) { return std::sqrt(kCreep * kCreep + 2.0 * kBrake * std::max(0.0, d_line)); }
double pymod(double a, double b) {
    double r = std::fmod(a, b);
    if (r < 0) r += b;
    return r;
}
}  // namespace

int signal_light(double t, int ph) {
    t = pymod(t + kSignalCycle / 2.0 * ph, kSignalCycle);
    return t >= kSignalCycle / 2.0 ? 0 : (t < kSignalGreen ? 2 : 1);
}

Junctions::Junctions(const MapData& map, const RoadGraph& graph)
    : g_(graph), is_(map.jn_is), control_(map.jn_control), phase_(map.jn_phase), box_(map.jn_box),
      stop_at_(map.jn_stop_at) {
    if (is_.size() != graph.nodes.size() || control_.size() != graph.edges.size()) {  // an older bake: no rules
        is_.clear();
        control_.assign(graph.edges.size(), kPriority);
        phase_.assign(graph.edges.size(), 0);
    }
}

Junctions::Ahead Junctions::ahead(int eid) {
    auto it = ahead_.find(eid);
    if (it != ahead_.end()) return it->second;
    Ahead res;
    double extra = 0.0;
    int cur = eid;
    for (int k = 0; k < 64; ++k) {
        const Edge& e = g_.edges[cur];
        if (is_[e.b]) {
            res = {e.b, extra, cur};
            break;
        }
        int only = -1, count = 0;
        for (int c : g_.out[e.b])
            if (g_.edges[c].b != e.a) { only = c; ++count; }
        if (count != 1) break;  // a dead end (or a one-way oddity)
        cur = only;
        extra += g_.edges[cur].length;
        if (extra > kLookAhead) break;
    }
    ahead_[eid] = res;
    return res;
}

void Junctions::traffic_at(const AICar& car, int n, double ax, double ay, const std::vector<const Car*>& others,
                           bool& busy, double& first) const {
    const double nx = g_.nodes[n].x, ny = g_.nodes[n].y, box = box_[n];
    busy = false;
    first = 1e9;
    for (const Car* o : others) {
        if (o == &car) continue;
        const double dx = nx - o->x, dy = ny - o->y;
        if (std::fabs(dx) > kActRange || std::fabs(dy) > kActRange) continue;
        const double d = std::hypot(dx, dy);
        if (d < 0.5) { busy = true; continue; }
        const double ux = dx / d, uy = dy / d;
        if (ux * ax + uy * ay > 0.85) continue;  // the same approach as us: ahead in the queue or behind us
        const double closing = o->vx * ux + o->vy * uy;  // speed towards the junction centre
        if (d < box) {
            if (closing > -1.0 || o->speed() < 1.0) busy = true;  // in the junction and not on its way out
            continue;
        }
        if (closing > 1.5 && closing > 0.8 * o->speed()) first = std::min(first, (d - box) / closing);
    }
}

bool Junctions::oncoming(const AICar& car, int n, double ax, double ay, const std::vector<const Car*>& others) const {
    const double nx = g_.nodes[n].x, ny = g_.nodes[n].y, box = box_[n];
    for (const Car* o : others) {
        if (o == &car) continue;
        const double dx = nx - o->x, dy = ny - o->y;
        if (std::fabs(dx) > kActRange || std::fabs(dy) > kActRange) continue;
        const double d = std::hypot(dx, dy);
        if (d < 0.5) return true;
        const double ux = dx / d, uy = dy / d;
        if (ux * ax + uy * ay > -0.85) continue;  // not coming from straight ahead of us
        const double closing = o->vx * ux + o->vy * uy;
        if (closing > 1.5 && (d < box || (d - box) / closing < kGap)) return true;
    }
    return false;
}

bool Junctions::exit_blocked(const AICar& car, int n, int ie, const std::vector<const Car*>& others) const {
    if (car.edge != ie || car.nxt < 0) return false;
    const Edge& x = g_.edges[car.nxt];
    const double nx = g_.nodes[n].x, ny = g_.nodes[n].y, reach = box_[n] + 12.0;
    for (const Car* o : others) {
        if (o == &car || o->speed() > 3.0) continue;
        if (o->fx() * x.dx + o->fy() * x.dy < 0.3) continue;  // facing into the junction: waiting at its own line,
                                                              // not in our way out (no all-way dead-lock)
        const double dx = o->x - nx, dy = o->y - ny, along = dx * x.dx + dy * x.dy;
        if (0.0 < along && along < reach && std::fabs(-dx * x.dy + dy * x.dx) < 3.5) return true;
    }
    return false;
}

bool Junctions::turns_left(const AICar& car, int ie) const {
    if (car.edge != ie || car.nxt < 0) return false;
    const Edge& a = g_.edges[ie];
    const Edge& b = g_.edges[car.nxt];
    return a.dx * b.dy - a.dy * b.dx < -0.5;  // y grows south: a negative cross product turns left
}

double Junctions::limit(AICar& car, int eid, double s, const std::vector<const Car*>& others, double t, double dt) {
    car.jn_hold = false;
    if (!ok()) return 1e9;
    const Ahead res = ahead(eid);
    if (res.node < 0) return 1e9;
    const int n = res.node, ie = res.into, ctl = control_[ie];
    if (car.jn_pass == n) return 1e9;
    const Edge& ie_e = g_.edges[ie];
    if (ctl == kPriority) {  // the priority road: give way only when turning left
        const double d_line = g_.edges[eid].length - s + res.extra - stop_at_[n];
        if (d_line > 15.0 || d_line < -1.0) return 1e9;
        const bool left = turns_left(car, ie);
        const bool jammed = exit_blocked(car, n, ie, others);
        if (jammed || (left && oncoming(car, n, ie_e.dx, ie_e.dy, others))) {
            car.jn_hold = !jammed;  // a jammed exit counts as a jam (ai clears jams)
            return stop_speed(d_line);
        }
        if (d_line < 2.0) car.jn_pass = n;
        return left ? creep_speed(d_line) : 1e9;
    }
    if (car.jn_node != n) {  // a new junction: forget the last one's state
        car.jn_node = n;
        car.jn_stop_t = 0.0;
        car.jn_wait = 0.0;
        car.jn_stopped = false;
    }
    const double d_line = g_.edges[eid].length - s + res.extra - stop_at_[n];
    if (d_line > kActRange) return 1e9;
    const double v = car.speed();
    if (d_line < -1.0) {  // already over the line (pushed, or late): carry on
        car.jn_pass = n;
        return 1e9;
    }
    bool go = false, slow = false;
    const double waited = car.jn_wait;
    if (ctl == kSignal) {
        if (light(ie, t) == 2) {
            bool busy;
            double first;
            traffic_at(car, n, ie_e.dx, ie_e.dy, others, busy, first);
            go = !busy && !(turns_left(car, ie) && oncoming(car, n, ie_e.dx, ie_e.dy, others));
        } else {  // red or yellow: stop if we comfortably can
            go = v * v > 2.0 * 6.0 * std::max(0.0, d_line) && d_line < 12.0;
        }
    } else {
        const bool near_line = d_line < 1.5;
        if (ctl == kStop && !car.jn_stopped) {
            if (near_line && v < 0.8) {
                car.jn_stop_t += dt;
                if (car.jn_stop_t >= kStopHold) car.jn_stopped = true;
            }
            go = false;
        } else if (d_line < 15.0) {
            bool busy;
            double first;
            traffic_at(car, n, ie_e.dx, ie_e.dy, others, busy, first);
            if (ctl == kAllWay) {
                const double mine = d_line / std::max(v, 1.0);
                go = !busy && first > mine + 0.3;
            } else {
                go = !busy && first > kGap;
            }
            if (!go && waited > kImpatient && !busy) go = true;  // nobody is moving: someone has to go
            slow = go;
        } else {
            go = slow = true;  // far off: just ease off for the junction
        }
    }
    if (v < 0.8 && d_line < 3.0) car.jn_wait += dt;
    bool jammed = false;
    if (go && d_line < 4.0 && exit_blocked(car, n, ie, others)) {
        go = false;  // the way out is jammed: wait behind the line
        jammed = true;
    }
    if (go) {
        if (d_line < 2.0) {
            car.jn_pass = n;
            car.jn_stopped = false;
        }
        return slow ? creep_speed(d_line) : 1e9;
    }
    car.jn_hold = !jammed;
    return stop_speed(d_line);
}

}  // namespace amr
