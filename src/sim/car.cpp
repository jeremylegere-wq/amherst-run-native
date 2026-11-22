#include "sim/car.h"

#include <algorithm>

namespace amr {

namespace {
constexpr double kAccel = 18.0, kBrake = 32.0, kReverseMax = 9.0;
constexpr double kMaxSpeedRoad = 38.0, kMaxSpeedGrass = 15.0;
constexpr double kDrag = 0.05, kRolling = 2.5;
constexpr double kGripRoad = 9.0, kGripGrass = 4.5, kGripHandbrake = 1.2;
constexpr double kSteerRate = 2.4;
constexpr double kHopSpeed = 4.6, kHopGravity = 24.0;
constexpr double kDriftMinSpeed = 9.0, kDriftGrip = 2.3, kDriftSlide = 0.4, kDriftTurn = 1.45;
constexpr double kChargeSteps[3][2] = {{0.8, 0.6}, {1.6, 1.1}, {2.6, 1.7}};  // (slide s, boost s)
constexpr double kBoostSpeed = 1.28, kBoostAccel = 34.0, kBoostKick = 3.0;

const CarModel kModels[4] = {
    {"fj", 4.6, 1.9, 1.25, 1.0},
    {"crv", 4.6, 1.85, 1.1, 1.02},
    {"sorento", 4.8, 1.9, 1.3, 1.03},
    {"ram", 5.9, 2.0, 1.65, 0.97},
};

double clamp01(double v) { return std::max(0.0, std::min(1.0, v)); }
double copysign1(double v) { return std::copysign(1.0, v); }
}  // namespace

const CarModel& player_model(int index) { return kModels[((index % 4) + 4) % 4]; }

Car::Car(double x0, double y0, double h, const CarModel& m)
    : x(x0), y(y0), heading(h), length(m.length), width(m.width), mass(m.mass), power_scale(m.power) {}

std::array<std::array<double, 2>, 4> Car::corners() const {
    const double f0 = fx(), f1 = fy(), r0 = rx(), r1 = ry(), hl = length / 2, hw = width / 2;
    const double ab[4][2] = {{hl, -hw}, {hl, hw}, {-hl, hw}, {-hl, -hw}};
    std::array<std::array<double, 2>, 4> out{};
    for (int i = 0; i < 4; ++i)
        out[i] = {x + f0 * ab[i][0] + r0 * ab[i][1], y + f1 * ab[i][0] + r1 * ab[i][1]};
    return out;
}

int Car::charge_level() const {
    int n = 0;
    for (const auto& s : kChargeSteps)
        if (charge >= s[0]) ++n;
    return n;
}

void Car::hop_and_drift(double dt, double steer, bool bumper, double vf) {
    const bool pressed = bumper && !bumper_prev_;
    bumper_prev_ = bumper;
    if (pressed && hop_z <= 0.0 && hop_vz <= 0.0 && std::fabs(vf) > 3.0) {
        hop_vz = kHopSpeed;
        if (std::fabs(steer) > 0.3 && vf > kDriftMinSpeed && !drift) {
            drift = steer > 0 ? 1 : -1;
            charge = 0.0;
        }
    }
    if (hop_vz != 0.0 || hop_z > 0.0) {
        hop_vz -= kHopGravity * dt;
        hop_z += hop_vz * dt;
        if (hop_z <= 0.0) hop_z = hop_vz = 0.0;
    }
    if (!drift && bumper && hop_z <= 0.0 && vf > kDriftMinSpeed && std::fabs(steer) > 0.6) {
        drift = steer > 0 ? 1 : -1;  // holding the bumper while turning also starts a slide
        charge = 0.0;
    }
    if (drift) {
        if (!bumper || vf < kDriftMinSpeed * 0.6) {
            const int level = charge_level();
            if (level) {
                boost_t = kChargeSteps[level - 1][1];
                vx += fx() * kBoostKick;
                vy += fy() * kBoostKick;
            }
            drift = 0;
            charge = 0.0;
        } else {
            const double inward = std::max(0.0, steer * drift);
            charge = std::min(kChargeSteps[2][0] + 0.4, charge + dt * (1.0 + 0.5 * inward));
        }
    }
}

void Car::update(double dt, const CarInput& in, bool on_road) {
    double f0 = fx(), f1 = fy(), r0 = rx(), r1 = ry();
    double vf = vx * f0 + vy * f1;
    double vl = vx * r0 + vy * r1;
    hop_and_drift(dt, in.steer, in.bumper, vf);
    const bool boosting = boost_t > 0.0;
    if (boosting) boost_t = std::max(0.0, boost_t - dt);
    double max_speed = (on_road ? kMaxSpeedRoad : kMaxSpeedGrass) * power * power_scale;
    if (boosting) max_speed *= kBoostSpeed;

    // engine and brakes
    if (boosting)
        vf += kBoostAccel * (1.0 - clamp01(vf / max_speed)) * dt;
    else if (in.throttle)
        vf += kAccel * power * (1.0 - clamp01(vf / max_speed)) * dt;
    if (in.brake) {
        if (vf > 0.5) vf = std::max(0.0, vf - kBrake * dt);
        else vf = std::max(-kReverseMax, vf - kAccel * 0.6 * dt);
    }
    if (!in.throttle && !in.brake) {
        const double roll = kRolling * dt;
        vf = std::fabs(vf) <= roll ? 0.0 : vf - std::copysign(roll, vf);
    }
    vf -= vf * kDrag * dt;
    if (vf > max_speed) vf -= (vf - max_speed) * 3.0 * dt;  // off-road speed cap, bleeds off quickly

    // sideways grip: the handbrake lets the back end slide
    double grip = in.handbrake ? kGripHandbrake : (on_road ? kGripRoad : kGripGrass);
    if (drift) grip = std::min(grip, kDriftGrip);
    vl *= std::exp(-grip * dt);

    // steering: less turn at speed, none when stationary, flipped in reverse
    double turn = in.steer * kSteerRate / (1.0 + std::fabs(vf) / 16.0) * std::min(1.0, std::fabs(vf) / 3.0);
    if (in.handbrake) turn *= 1.5;
    if (drift)  // slide direction is locked; steering in tightens it, out widens it
        turn = drift * (1.0 + 0.45 * in.steer * drift) * kSteerRate * kDriftTurn / (1.0 + std::fabs(vf) / 16.0);
    const double dtheta = turn * copysign1(std::fabs(vf) > 0.01 ? vf : 1.0) * dt;
    heading += dtheta;
    if (drift) vl -= vf * dtheta * kDriftSlide;  // the velocity lags behind the nose: the car visibly slides

    f0 = fx(); f1 = fy(); r0 = rx(); r1 = ry();
    vx = f0 * vf + r0 * vl;
    vy = f1 * vf + r1 * vl;
    x += vx * dt;
    y += vy * dt;
}

}  // namespace amr
