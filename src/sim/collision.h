// Car against the world: buildings and the map edge (game/collision.py move_and_slide), and easing onto the
// ground or an overpass deck with the downhill pull (settle_height). Trees and poles come with the props.
#pragma once
#include "sim/car.h"
#include "world/world.h"

namespace amr {

// Resolve a move that already happened (car.x/y are new; prev_x/y where it was). Slides along walls instead of
// stopping dead. Returns the impact speed in m/s (0 if nothing was hit).
double move_and_slide(Car& car, const World& world, double prev_x, double prev_y, bool thorough);

// Ease the car onto the surface under it and pull it downhill (uphill slows it, downhill speeds it up).
void settle_height(Car& car, const World& world, double dt);

}  // namespace amr
