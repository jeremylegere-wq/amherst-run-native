#include "render/car_mesh.h"

namespace amr {

namespace {
const CarShape kShapes[] = {
    {"sedan", 4.2, 1.8, 0.3, 0.95, 1.62, -1.3, 0.85, -1.0, 0.4, 1.35, false, false, 0, 0},
    {"fj", 4.6, 1.9, 0.42, 1.2, 1.95, -2.0, 1.0, -1.95, 0.8, 1.45, false, true, 0, 0},
    {"crv", 4.6, 1.85, 0.34, 1.12, 1.8, -2.0, 0.9, -1.7, 0.2, 1.45, false, false, 0, 0},
    {"sorento", 4.8, 1.9, 0.38, 1.18, 1.88, -2.1, 0.9, -1.9, 0.3, 1.5, false, false, 0, 0},
    {"wagon", 4.2, 1.65, 0.3, 0.92, 1.5, -2.0, 0.75, -1.95, 0.45, 1.3, false, false, 0, 0},
    {"ram", 5.9, 2.0, 0.5, 1.2, 2.05, -0.5, 1.55, -0.3, 0.95, 1.9, true, false, -2.95, -0.55},
};
const std::uint8_t kWheel[3] = {26, 24, 34}, kHub[3] = {60, 60, 68}, kBedFloor[3] = {24, 24, 30};
const std::uint8_t kHeadlight[3] = {255, 246, 180};

void set(std::uint8_t d[3], int r, int g, int b) {
    d[0] = static_cast<std::uint8_t>(r); d[1] = static_cast<std::uint8_t>(g); d[2] = static_cast<std::uint8_t>(b);
}

// A box in the car's frame: a along the car, b across (+ is right), z up; sides one colour, the top another.
void box(MeshBuilder& m, double a0, double a1, double b0, double b1, double z0, double z1, const std::uint8_t* side,
         const std::uint8_t* top) {
    auto P = [](double a, double b, double z) { return Vec3{static_cast<float>(a), static_cast<float>(b), static_cast<float>(z)}; };
    const Vec3 q[8] = {P(a0, b0, z0), P(a1, b0, z0), P(a1, b1, z0), P(a0, b1, z0),
                       P(a0, b0, z1), P(a1, b0, z1), P(a1, b1, z1), P(a0, b1, z1)};
    m.quad(q[4], q[5], q[6], q[7], top);
    m.quad(q[0], q[1], q[5], q[4], side);
    m.quad(q[1], q[2], q[6], q[5], side);
    m.quad(q[2], q[3], q[7], q[6], side);
    m.quad(q[3], q[0], q[4], q[7], side);
}

// A frustum: the rectangle (a0..a1, b0..b1) at z0 narrowing to (c0..c1, d0..d1) at z1 (the glass cabin).
void frustum(MeshBuilder& m, double a0, double a1, double b0, double b1, double c0, double c1, double d0, double d1,
             double z0, double z1, const std::uint8_t* side, const std::uint8_t* top) {
    auto P = [](double a, double b, double z) { return Vec3{static_cast<float>(a), static_cast<float>(b), static_cast<float>(z)}; };
    const Vec3 q[8] = {P(a0, b0, z0), P(a1, b0, z0), P(a1, b1, z0), P(a0, b1, z0),
                       P(c0, d0, z1), P(c1, d0, z1), P(c1, d1, z1), P(c0, d1, z1)};
    m.quad(q[4], q[5], q[6], q[7], top);
    m.quad(q[0], q[1], q[5], q[4], side);
    m.quad(q[1], q[2], q[6], q[5], side);
    m.quad(q[2], q[3], q[7], q[6], side);
    m.quad(q[3], q[0], q[4], q[7], side);
}
}  // namespace

const CarShape& car_shape(const std::string& model) {
    for (const CarShape& s : kShapes)
        if (model == s.name) return s;
    return kShapes[0];
}

CarLook player_look(int index) {
    CarLook l;
    switch (((index % 4) + 4) % 4) {
        case 0: set(l.body, 240, 214, 66); set(l.roof, 246, 246, 240); set(l.glass, 60, 84, 130); break;
        case 1: set(l.body, 238, 238, 234); set(l.roof, 238, 238, 234); set(l.glass, 70, 96, 150); break;
        case 2: set(l.body, 128, 134, 144); set(l.roof, 128, 134, 144); set(l.glass, 60, 84, 130); break;
        default: set(l.body, 40, 42, 50); set(l.roof, 40, 42, 50); set(l.glass, 74, 96, 140); break;
    }
    set(l.tail, 222, 40, 40);
    return l;
}

CarLook traffic_look(int colour) {
    static const int kBodies[8][3] = {{196, 60, 60}, {60, 120, 200}, {80, 160, 90}, {200, 200, 205},
                                      {70, 70, 80},   {210, 130, 50}, {150, 90, 170}, {120, 190, 190}};
    CarLook l;
    const int* b = kBodies[((colour % 8) + 8) % 8];
    set(l.body, b[0], b[1], b[2]);
    set(l.roof, b[0], b[1], b[2]);
    set(l.glass, 70, 100, 170);
    set(l.tail, 200, 40, 40);
    return l;
}

CarLook police_look(int flash) {
    CarLook l;
    set(l.body, 236, 236, 240);
    set(l.roof, 22, 22, 30);
    set(l.glass, 50, 70, 120);
    set(l.tail, 232, 40, 40);
    l.lightbar = flash;
    return l;
}

MeshBuilder build_car(const CarShape& s, const CarLook& look) {
    MeshBuilder m;
    const double hl = s.length / 2, hw = s.width / 2, wr = s.ride + 0.42, wa = s.wheel;
    for (double side : {-1.0, 1.0}) {  // the wheels stick out a little either side
        const double lo = side > 0 ? hw - 0.3 : -hw - 0.14, hi = side > 0 ? hw + 0.14 : -hw + 0.3;
        for (double a : {wa, -wa}) box(m, a - 0.46, a + 0.46, lo, hi, 0.0, wr, kWheel, kWheel);
    }
    box(m, -hl + 0.08, hl - 0.08, -hw + 0.08, hw - 0.08, s.ride, s.belt, look.body, look.body);
    if (s.bed) {  // pickup bed: side rails, tailgate and a dark floor
        const double top = s.belt + 0.32;
        box(m, s.bed0, s.bed1, -hw + 0.08, -hw + 0.24, s.belt, top, look.body, look.body);
        box(m, s.bed0, s.bed1, hw - 0.24, hw - 0.08, s.belt, top, look.body, look.body);
        box(m, s.bed0, s.bed0 + 0.14, -hw + 0.08, hw - 0.08, s.belt, top, look.body, look.body);
        box(m, s.bed0 + 0.14, s.bed1, -hw + 0.24, hw - 0.24, s.belt, s.belt + 0.03, kBedFloor, kBedFloor);
    }
    frustum(m, s.cab0, s.cab1, -hw + 0.1, hw - 0.1, s.top0, s.top1, -hw + 0.34, hw - 0.34, s.belt, s.roof, look.glass,
            look.roof);
    if (s.spare) box(m, -hl - 0.34, -hl + 0.06, -0.42, 0.42, s.ride + 0.3, s.ride + 1.1, kWheel, kHub);
    if (look.lightbar) {  // red and blue, swapping (game/render3d.py: six times a second)
        std::uint8_t red[3], blue[3];
        const bool left_on = look.lightbar == 1;
        set(red, left_on ? 240 : 110, left_on ? 40 : 20, left_on ? 40 : 20);
        set(blue, left_on ? 24 : 70, left_on ? 36 : 110, left_on ? 100 : 255);
        box(m, -0.45, 0.15, -0.58, 0.0, s.roof, s.roof + 0.2, red, red);
        box(m, -0.45, 0.15, 0.0, 0.58, s.roof, s.roof + 0.2, blue, blue);
    }
    const double lz = s.ride + 0.25;  // tail lights at the back, headlights at the front
    for (double b0 : {-hw + 0.1, hw - 0.5}) {
        box(m, -hl - 0.02, -hl + 0.06, b0, b0 + 0.4, lz, lz + 0.26, look.tail, look.tail);
        box(m, hl - 0.06, hl + 0.02, b0, b0 + 0.4, lz, lz + 0.26, kHeadlight, kHeadlight);
    }
    return m;
}

}  // namespace amr
