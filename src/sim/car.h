// Arcade car physics: a straight port of the Python game's game/car.py (same constants, same order of
// operations), checked against its golden drives (tests/test_drive.cpp). Doubles, like Python.
//
// Map coordinates: x east, y SOUTH (the converter's y = -latitude). Heading 0 points east and grows clockwise
// seen from above, so steering right (+1) increases it.
#pragma once
#include <array>
#include <cmath>

namespace amr {

struct CarModel {
    const char* name;
    double length, width, mass, power;
};

// Player cars (game/car.py MODELS, PLAYER_MODELS): boxy retro SUV, compact crossover, mid-size SUV, quad cab pickup.
const CarModel& player_model(int index);
// The traffic and police cars' model (game/car.py "sedan").
const CarModel& sedan_model();

struct CarInput {
    bool throttle = false, brake = false;
    double steer = 0;  // -1 left .. +1 right
    bool handbrake = false, bumper = false;
};

class Car {
public:
    virtual ~Car() = default;
    Car(double x, double y, double heading, const CarModel& model);

    // One physics step (game/car.py Car.update). on_road: the surface under the car is a road or a lot.
    void update(double dt, const CarInput& in, bool on_road);

    double fx() const { return std::cos(heading); }
    double fy() const { return std::sin(heading); }
    double rx() const { return -std::sin(heading); }
    double ry() const { return std::cos(heading); }
    double speed() const { return std::hypot(vx, vy); }
    double forward_speed() const { return vx * fx() + vy * fy(); }
    std::array<std::array<double, 2>, 4> corners() const;
    int charge_level() const;

    double x, y, heading;
    double vx = 0, vy = 0;
    double z = 0;               // surface height under the car (ground or deck), metres
    double hop_z = 0, hop_vz = 0;
    int drift = 0;              // 0 not sliding, else -1 / +1
    double charge = 0, boost_t = 0;
    double power = 1.0;         // 1 = undamaged
    double length, width, mass, power_scale;

private:
    void hop_and_drift(double dt, double steer, bool bumper, double vf);
    bool bumper_prev_ = false;
};

}  // namespace amr
