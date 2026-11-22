#include "sim/ai.h"

#include <algorithm>
#include <cmath>

namespace amr {

namespace {
constexpr double kPi = 3.14159265358979323846;

void speed_control(AICar& car, double v_t) {  // hold a target speed; never brake below 1 m/s (that would reverse)
    const double vf = car.forward_speed();
    car.throttle = car.brake = false;
    if (v_t < 0.3) car.brake = vf > 1.0;
    else if (vf < v_t - 0.4) car.throttle = true;
    else if (vf > v_t + 1.2) car.brake = vf > 1.0;
}
}  // namespace

AICar::AICar(double x0, double y0, double h, bool is_police) : Car(x0, y0, h, sedan_model()), police(is_police) {
    mass = police ? 1.2 : 1.0;
}

double wrap_angle(double a) {
    double r = std::fmod(a + kPi, 2 * kPi);
    if (r < 0) r += 2 * kPi;
    return r - kPi;
}

double obstacle_limit(const Car& car, const std::vector<const Car*>& others, double look, double width, double gap,
                      double gain, double predict) {
    const double fx = car.fx(), fy = car.fy(), rx = car.rx(), ry = car.ry();
    if (look < 0) look = std::max(16.0, std::min(45.0, 10.0 + car.speed() * 2.2));
    double limit = 1e9;
    for (const Car* o : others) {
        if (o == &car) continue;
        const double dx = o->x - car.x, dy = o->y - car.y;
        if (std::fabs(dx) > look + 15.0 || std::fabs(dy) > look + 15.0) continue;
        const double lead = std::max(0.0, o->vx * fx + o->vy * fy);  // its speed along our direction
        const int n = predict != 0.0 ? 2 : 1;
        for (int k = 0; k < n; ++k) {
            const double t = k == 0 ? 0.0 : predict;
            const double px = dx + o->vx * t, py = dy + o->vy * t;
            const double f = px * fx + py * fy - car.speed() * t * (t > 0 ? 1.0 : 0.0);  // where we will be, too
            if (f < 0.5 || f > look || std::fabs(px * rx + py * ry) > width) continue;
            const double v = std::max(0.0, f - gap) * gain + (t == 0.0 ? lead : 0.0);
            if (v < limit) limit = v;
        }
    }
    return limit;
}

void drive_traffic(AICar& car, const RoadGraph& graph, const std::vector<const Car*>& others,
                   const std::function<int(int)>& next_edge, double dt) {
    if (car.edge < 0) {
        car.dead = true;
        return;
    }
    double lx = 0, ly = 0, s = 0, lat = 0;
    for (int pass = 0; pass < 2; ++pass) {
        const Edge& e = graph.edges[car.edge];
        lx = e.ax - e.dy * e.lane;  // the start of the lane line
        ly = e.ay + e.dx * e.lane;
        const double px = car.x - lx, py = car.y - ly;
        s = px * e.dx + py * e.dy;      // distance along the lane
        lat = -px * e.dy + py * e.dx;   // sideways offset (right is +)
        if (s > e.length - 1.0 && car.nxt >= 0) {
            car.edge = car.nxt;
            car.nxt = next_edge(car.edge);
            continue;
        }
        break;
    }
    if (std::fabs(lat) > 12.0 || s < -15.0) {
        car.dead = true;  // knocked far off its road
        return;
    }
    const Edge& e = graph.edges[car.edge];
    const double look = std::max(5.0, std::min(12.0, 4.0 + car.speed() * 0.55));
    const double st = s + look;
    double tx, ty;
    if (st <= e.length || car.nxt < 0) {
        const double t = std::min(st, e.length);
        tx = lx + e.dx * t;
        ty = ly + e.dy * t;
    } else {
        const Edge& n = graph.edges[car.nxt];
        const double over = std::min(st - e.length, n.length);
        tx = n.ax - n.dy * n.lane + n.dx * over;
        ty = n.ay + n.dx * n.lane + n.dy * over;
    }
    double v_t = e.cruise * car.pace;
    if (car.nxt >= 0) {  // slow for the corner into the next edge
        const Edge& n = graph.edges[car.nxt];
        const double ang = std::fabs(wrap_angle(std::atan2(n.dy, n.dx) - std::atan2(e.dy, e.dx)));
        const double vc = std::max(4.5, std::min(n.cruise * car.pace, v_t * (1.0 - 1.2 * ang / kPi)));
        v_t = std::min(v_t, std::sqrt(vc * vc + 2 * 7.0 * std::max(0.0, e.length - s - 2.0)));
    }
    const double err = wrap_angle(std::atan2(ty - car.y, tx - car.x) - car.heading);
    const double raw_steer = std::max(-1.0, std::min(1.0, err * 2.0));
    if (std::fabs(err) > 1.0) v_t = std::min(v_t, 5.0);
    const double v_obs = obstacle_limit(car, others);
    v_t = std::min(v_t, v_obs);
    const double v_jn = 1e9;  // junction rules (game/junctions.py) come later
    v_t = std::min(v_t, v_jn);

    // smooth steering: towards the target at a limited rate, faster at speed
    const double max_delta = (4.0 + car.speed() * 0.15) * dt;
    const double delta = std::max(-max_delta, std::min(max_delta, raw_steer - car.steer));
    car.steer = std::max(-1.0, std::min(1.0, car.steer + delta));
    // smooth speed target; stopping behind a stopped car is not smoothed, or followers rear-end the queue
    if (!car.has_v_smooth) {
        car.v_smooth = v_t;
        car.has_v_smooth = true;
    }
    car.v_smooth += (v_t - car.v_smooth) * (1.0 - std::exp(-6.0 * dt));
    speed_control(car, std::min({car.v_smooth, v_jn, v_obs}));

    if (car.speed() < 0.6 && v_t > 1.5) car.stuck += dt;
    else car.stuck = std::max(0.0, car.stuck - dt);
    if (!car.jn_hold) car.wait = v_t < 0.5 ? car.wait + dt : std::max(0.0, car.wait - dt);
    if (car.stuck > 6.0 || car.wait > 20.0) car.dead = true;
}

}  // namespace amr
