// Third-person camera behind the car (game/render3d.py ChaseCamera): its yaw lags the car a little so turns feel
// weighty, it pulls back and up at speed, rides overpasses with the car, pulls in rather than sit inside a building,
// and never dips under a hill behind the car. It looks at a point just ahead of the car.
#pragma once
#include <cmath>

#include "render/map_renderer.h"
#include "sim/car.h"
#include "world/world.h"

namespace amr {

class ChaseCamera {
public:
    explicit ChaseCamera(const Car& car) : yaw_(car.heading) {}

    void update(const Car& car, const World& world, double dt, bool snap = false) {
        const double pi = 3.14159265358979323846;
        double diff = std::fmod(car.heading - yaw_ + pi, 2 * pi);
        if (diff < 0) diff += 2 * pi;
        diff -= pi;
        yaw_ += diff * (snap ? 1.0 : 1.0 - std::exp(-5.0 * dt));
        const double sp = std::fmin(car.speed(), 35.0) / 35.0;
        const double dist = 8.0 + 3.0 * sp, height = 3.2 + 0.8 * sp;
        const double cs = std::cos(yaw_), sn = std::sin(yaw_);
        double d = dist, x = car.x, y = car.y;
        for (double f : {1.0, 0.75, 0.55, 0.4}) {  // shrink the follow distance if it would sit inside a building
            d = dist * f;
            x = car.x - cs * d;
            y = car.y - sn * d;
            if (!world.in_building(x, y)) break;
        }
        x_ = x;
        y_ = y;
        z_ = std::fmax(height + car.z, world.ground(static_cast<float>(x), static_cast<float>(y)) + 1.5);
        pitch_ = std::atan2(height - 1.0, d + 6.0);
    }

    Camera camera(float fovy) const {
        Camera c;
        c.fovy = fovy;
        c.eye = {static_cast<float>(x_), static_cast<float>(y_), static_cast<float>(z_)};
        const double cp = std::cos(pitch_);
        c.target = {static_cast<float>(x_ + std::cos(yaw_) * cp * 10.0), static_cast<float>(y_ + std::sin(yaw_) * cp * 10.0),
                    static_cast<float>(z_ - std::sin(pitch_) * 10.0)};
        return c;
    }

private:
    double yaw_, x_ = 0, y_ = 0, z_ = 0, pitch_ = 0.1;
};

}  // namespace amr
