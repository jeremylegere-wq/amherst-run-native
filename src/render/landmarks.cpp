#include "render/landmarks.h"

#include <cmath>
#include <vector>

namespace amr {

namespace {
constexpr float kPi = 3.14159265f;
using C3 = std::uint8_t[3];

// A local frame: a forward along the heading, b to its right, z up from the ground at (x, y).
struct Frame {
    float x, y, g, fx, fy, rx, ry;
    Frame(float x0, float y0, float g0, float heading)
        : x(x0), y(y0), g(g0), fx(std::cos(heading)), fy(std::sin(heading)), rx(-std::sin(heading)), ry(std::cos(heading)) {}
    Vec3 P(float a, float b, float z) const { return {x + fx * a + rx * b, y + fy * a + ry * b, g + z}; }
};

void box(MeshBuilder& m, const Frame& f, float a0, float a1, float b0, float b1, float z0, float z1, const C3& side,
         const C3& top) {
    const Vec3 q[8] = {f.P(a0, b0, z0), f.P(a1, b0, z0), f.P(a1, b1, z0), f.P(a0, b1, z0),
                       f.P(a0, b0, z1), f.P(a1, b0, z1), f.P(a1, b1, z1), f.P(a0, b1, z1)};
    m.quad(q[4], q[5], q[6], q[7], top);
    m.quad(q[0], q[1], q[5], q[4], side);
    m.quad(q[1], q[2], q[6], q[5], side);
    m.quad(q[2], q[3], q[7], q[6], side);
    m.quad(q[3], q[0], q[4], q[7], side);
}

// A flat polygon (convex, or nearly), both faces.
void flat(MeshBuilder& m, const std::vector<Vec3>& pts, const C3& col) {
    if (pts.size() < 3) return;
    Vec3 n = normalize(cross(pts[1] - pts[0], pts[2] - pts[0]));
    for (int side = 0; side < 2; ++side) {
        std::vector<std::uint32_t> idx;
        for (const Vec3& p : pts) idx.push_back(m.add(p, side ? n * -1.0f : n, col));
        for (std::size_t k = 1; k + 1 < idx.size(); ++k) {
            if (side) m.tri(idx[0], idx[k + 1], idx[k]);
            else m.tri(idx[0], idx[k], idx[k + 1]);
        }
    }
}

// An eight-sided tapered tower section (game/render3d.py _oct), with its top if `cap`.
void oct(MeshBuilder& m, float x, float y, float g, float z0, float z1, float r0, float r1, const C3& col, bool cap) {
    Vec3 b[8], t[8];
    for (int k = 0; k < 8; ++k) {
        const float a = kPi / 8 + k * kPi / 4;
        b[k] = {x + r0 * std::cos(a), y + r0 * std::sin(a), g + z0};
        t[k] = {x + r1 * std::cos(a), y + r1 * std::sin(a), g + z1};
    }
    for (int k = 0; k < 8; ++k) m.quad(b[k], b[(k + 1) % 8], t[(k + 1) % 8], t[k], col);
    if (cap) {
        const Vec3 up{0, 0, 1};
        const std::uint32_t c = m.add({x, y, g + z1}, up, col);
        std::uint32_t idx[8];
        for (int k = 0; k < 8; ++k) idx[k] = m.add(t[k], up, col);
        for (int k = 0; k < 8; ++k) m.tri(c, idx[k], idx[(k + 1) % 8]);
    }
}

void lighthouse(MeshBuilder& m, const Prop& p, float g) {
    static const C3 white = {246, 246, 242}, red = {196, 44, 44}, granite = {146, 148, 150}, glass = {252, 232, 136};
    static const C3 mulch = {96, 68, 46}, blue = {26, 78, 142}, gold = {224, 184, 62}, plate = {242, 244, 246};
    static const C3 post = {124, 84, 48}, flag_red = {206, 36, 44}, flag_blue = {32, 92, 168};
    oct(m, p.x, p.y, g, 0.35f, 1.15f, 2.15f, 2.05f, granite, true);
    oct(m, p.x, p.y, g, 1.15f, 6.6f, 1.9f, 1.25f, white, false);
    oct(m, p.x, p.y, g, 6.6f, 6.82f, 1.55f, 1.55f, red, true);
    oct(m, p.x, p.y, g, 6.82f, 8.05f, 0.92f, 0.92f, glass, false);
    oct(m, p.x, p.y, g, 6.82f, 7.35f, 1.48f, 1.48f, red, false);      // gallery rail
    oct(m, p.x, p.y, g, 8.05f, 8.95f, 1.08f, 0.08f, red, false);      // cap
    const Frame f(p.x, p.y, g, 0.0f);
    box(m, f, -0.08f, 0.08f, -0.08f, 0.08f, 8.95f, 9.45f, red, red);  // spire
    // the border welcome site (the only lighthouse on the map is there): porch, planter, sign, flags
    const Frame s(p.x, p.y, g, p.heading);
    box(m, s, -3.2f, 3.2f, -3.6f, 3.6f, 0.0f, 0.35f, granite, mulch);
    box(m, s, 1.35f, 2.35f, -0.65f, 0.65f, 0.35f, 2.95f, white, white);
    box(m, s, 2.35f, 2.4f, -0.38f, 0.38f, 0.35f, 2.55f, red, red);
    box(m, s, 1.3f, 2.45f, -0.75f, 0.75f, 2.95f, 3.2f, red, red);
    const float sa = 6.5f, sb = 4.2f;
    for (float b : {sb - 1.85f, sb + 1.85f}) box(m, s, sa - 0.14f, sa + 0.14f, b - 0.12f, b + 0.12f, 0.0f, 3.15f, post, post);
    box(m, s, sa - 0.09f, sa + 0.09f, sb - 1.75f, sb + 1.75f, 1.15f, 2.85f, blue, blue);
    box(m, s, sa - 0.12f, sa + 0.12f, sb - 1.78f, sb + 1.78f, 2.85f, 3.05f, gold, gold);
    box(m, s, sa - 0.11f, sa + 0.11f, sb - 1.35f, sb + 1.35f, 1.55f, 2.45f, plate, plate);
    const float flags[2][2] = {{-2.6f, -5.4f}, {-2.8f, 1.2f}};
    for (int k = 0; k < 2; ++k) {
        const float fa = flags[k][0], fb = flags[k][1];
        box(m, s, fa - 0.06f, fa + 0.06f, fb - 0.06f, fb + 0.06f, 0.0f, 8.2f, white, white);
        box(m, s, fa - 0.02f, fa + 0.02f, fb + 0.06f, fb + 1.55f, 7.05f, 8.0f, k ? flag_blue : flag_red, k ? flag_blue : flag_red);
    }
}

void cenotaph(MeshBuilder& m, const Prop& p, float g) {
    static const C3 granite = {186, 186, 196}, bronze = {64, 104, 90};
    const Frame f(p.x, p.y, g, 0.0f);
    box(m, f, -1.4f, 1.4f, -1.4f, 1.4f, 0.0f, 0.8f, granite, granite);
    // the tapered shaft and the bronze top: four-sided frustums
    auto frustum = [&](float r0, float r1, float z0, float z1, const C3& col) {
        const Vec3 q[8] = {f.P(-r0, -r0, z0), f.P(r0, -r0, z0), f.P(r0, r0, z0), f.P(-r0, r0, z0),
                           f.P(-r1, -r1, z1), f.P(r1, -r1, z1), f.P(r1, r1, z1), f.P(-r1, r1, z1)};
        m.quad(q[4], q[5], q[6], q[7], col);
        m.quad(q[0], q[1], q[5], q[4], col);
        m.quad(q[1], q[2], q[6], q[5], col);
        m.quad(q[2], q[3], q[7], q[6], col);
        m.quad(q[3], q[0], q[4], q[7], col);
    };
    frustum(0.8f, 0.55f, 0.8f, 4.4f, granite);
    frustum(0.35f, 0.2f, 4.4f, 6.0f, bronze);
}

void dam(MeshBuilder& m, const Prop& p, float g) {
    static const C3 concrete = {176, 174, 166}, dark = {132, 130, 122}, moss = {114, 126, 102};
    static const C3 deep = {36, 78, 108}, fall = {145, 202, 228}, foam = {232, 244, 248};
    static const C3 timber = {134, 92, 56}, steel = {64, 72, 78}, red = {184, 62, 54};
    const Frame f(p.x, p.y, g, p.heading);  // a = downstream, b = along the crest, z = up from the river
    const float H = p.h, hb = 50.0f / 2, s0 = -hb, s1 = hb - 8.0f;  // (the dam on the map is 50 m long)
    flat(m, {f.P(0.6f, s0 - 2, 0.04f), f.P(14.0f, s0 - 2, 0.04f), f.P(14.0f, s1 + 2, 0.04f), f.P(0.6f, s1 + 2, 0.04f)}, deep);
    flat(m, {f.P(2.1f, s0, 0.08f), f.P(4.8f, s0, 0.08f), f.P(4.8f, s1, 0.08f), f.P(2.1f, s1, 0.08f)}, foam);
    box(m, f, -2.2f, 0.4f, s0, s1, 0.0f, H, concrete, concrete);              // the spillway
    flat(m, {f.P(0.45f, s0, H), f.P(0.45f, s1, H), f.P(1.6f, s1, 0.1f), f.P(1.6f, s0, 0.1f)}, fall);  // water curtain
    flat(m, {f.P(-2.2f, s0, H + 0.02f), f.P(-6.0f, s0, H + 0.02f), f.P(-6.0f, s1, H + 0.02f), f.P(-2.2f, s1, H + 0.02f)}, deep);
    box(m, f, -2.5f, 0.5f, s1, s1 + 2.3f, 0.0f, H + 0.4f, dark, dark);        // the sluice
    flat(m, {f.P(0.55f, s1 + 0.3f, H - 0.1f), f.P(0.55f, s1 + 2.0f, H - 0.1f), f.P(3.6f, s1 + 2.0f, 0.2f), f.P(3.6f, s1 + 0.3f, 0.2f)}, foam);
    for (float b0 : {s1 + 2.3f, s1 + 3.8f, s1 + 5.3f}) box(m, f, -2.8f, 1.0f, b0, b0 + 0.9f, 0.0f, H + 1.6f, concrete, concrete);
    box(m, f, -3.0f, 1.2f, s1 + 2.1f, s1 + 6.4f, H + 1.4f, H + 1.75f, steel, steel);   // catwalk
    for (float b : {s1 + 3.35f, s1 + 4.85f}) {
        box(m, f, -1.1f, -0.8f, b - 0.15f, b + 0.15f, H + 1.75f, H + 2.75f, steel, steel);
        box(m, f, -1.3f, -0.6f, b - 0.35f, b + 0.35f, H + 2.65f, H + 2.8f, red, red);  // handwheels
    }
    const float b0 = s1 + 6.2f, b1 = s1 + 8.0f;  // the fish ladder down the bank, with timber baffles
    for (float b : {b0, b1}) flat(m, {f.P(-2.0f, b, 0.0f), f.P(16.0f, b, 0.0f), f.P(16.0f, b, 1.4f), f.P(-2.0f, b, H + 0.6f)}, moss);
    flat(m, {f.P(-2.0f, b0, H - 0.2f), f.P(16.0f, b0, 0.7f), f.P(16.0f, b1, 0.7f), f.P(-2.0f, b1, H - 0.2f)}, fall);
    for (int k = 0; k < 9; ++k) {
        const float t = (k + 0.5f) / 9, a = -1.2f + t * 16.4f, z = H - 0.1f - t * (H - 1.0f);
        flat(m, {f.P(a + 0.35f, b0, z - 0.15f), f.P(a + 0.35f, b1, z - 0.15f), f.P(a - 0.15f, b1, z + 0.45f), f.P(a - 0.15f, b0, z + 0.45f)}, timber);
    }
}

void horse(MeshBuilder& m, const Prop& p, float g) {
    // each part a side profile (a forward, z up) pushed out sideways from b0 to b1 (game/render3d.py _draw_horse)
    const C3 coat = {p.colour.r ? p.colour.r : std::uint8_t(30), p.colour.g ? p.colour.g : std::uint8_t(28),
                     p.colour.b ? p.colour.b : std::uint8_t(34)};
    const C3 leg = {static_cast<std::uint8_t>(std::max(0, coat[0] - 12)), static_cast<std::uint8_t>(std::max(0, coat[1] - 12)),
                    static_cast<std::uint8_t>(std::max(0, coat[2] - 12))};
    const Frame f(p.x, p.y, g + 0.35f, p.heading);  // all four hooves off the ground (lift 0.35 m on the map)
    using Prof = std::vector<std::pair<float, float>>;
    auto slab = [&](const Prof& prof, float b0, float b1, const C3& col) {
        std::vector<Vec3> near, far;
        for (const auto& az : prof) {
            near.push_back(f.P(az.first, b1, az.second));
            far.push_back(f.P(az.first, b0, az.second));
        }
        flat(m, near, col);
        flat(m, far, col);
        for (std::size_t k = 0; k < prof.size(); ++k) {
            const std::size_t j = (k + 1) % prof.size();
            flat(m, {far[k], far[j], near[j], near[k]}, col);
        }
    };
    const Prof front_reach = {{0.75f, 1.2f}, {0.92f, 1.15f}, {1.58f, 0.45f}, {1.46f, 0.36f}};
    const Prof hind_stretch = {{-0.78f, 1.2f}, {-0.56f, 1.2f}, {-1.42f, 0.46f}, {-1.58f, 0.52f}};
    const Prof front_fold = {{0.52f, 1.2f}, {0.72f, 1.15f}, {0.98f, 0.76f}, {0.74f, 0.52f}, {0.64f, 0.6f}, {0.8f, 0.8f}};
    const Prof hind_gather = {{-0.62f, 1.2f}, {-0.44f, 1.15f}, {-0.04f, 0.62f}, {-0.16f, 0.52f}};
    slab(front_reach, 0.14f, 0.26f, leg);    // right side legs
    slab(hind_stretch, 0.14f, 0.26f, leg);
    slab(front_fold, -0.26f, -0.14f, leg);   // left side legs
    slab(hind_gather, -0.26f, -0.14f, leg);
    slab({{-0.94f, 1.56f}, {-1.06f, 1.64f}, {-1.62f, 1.26f}, {-1.56f, 1.14f}}, -0.05f, 0.05f, coat);              // tail
    slab({{-0.95f, 1.1f}, {0.85f, 1.05f}, {1.0f, 1.45f}, {0.75f, 1.7f}, {-0.85f, 1.66f}, {-1.06f, 1.42f}}, -0.27f, 0.27f, coat);
    slab({{0.58f, 1.42f}, {0.96f, 1.36f}, {1.36f, 1.96f}, {1.1f, 2.22f}}, -0.15f, 0.15f, coat);                   // neck
    slab({{1.05f, 2.02f}, {1.32f, 2.28f}, {1.82f, 1.86f}, {1.74f, 1.68f}}, -0.12f, 0.12f, coat);                   // head
}
}  // namespace

bool add_landmark(MeshBuilder& m, const std::string& kind, const Prop& p, float g) {
    if (kind == "lighthouse") lighthouse(m, p, g);
    else if (kind == "cenotaph") cenotaph(m, p, g);
    else if (kind == "dam") dam(m, p, g);
    else if (kind == "horse") horse(m, p, g);
    else return false;
    return true;
}

}  // namespace amr
