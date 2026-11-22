#include "render/building_detail.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace amr {
namespace {

// the street style's colours (game/render3d.py STYLES["street"]: "house", "windows", "cornice", "slate", ...)
constexpr std::uint8_t kFoundation[3] = {150, 146, 140};
constexpr std::uint8_t kTrim[3] = {250, 250, 244};
constexpr std::uint8_t kGlass[3] = {52, 84, 150};
constexpr std::uint8_t kDoors[4][3] = {{196, 44, 48}, {36, 70, 150}, {40, 120, 84}, {250, 250, 244}};
constexpr std::uint8_t kGarageDoor[3] = {236, 234, 226};
constexpr std::uint8_t kPorchWood[3] = {204, 168, 118};
constexpr std::uint8_t kPorchRoof[3] = {72, 76, 92};
constexpr std::uint8_t kSigns[5][3] = {{30, 96, 72}, {150, 36, 44}, {32, 60, 130}, {214, 170, 52}, {24, 24, 30}};
constexpr std::uint8_t kWindows[3] = {40, 60, 118};
constexpr std::uint8_t kCorniceRgb[3] = {242, 224, 186};
constexpr std::uint8_t kSlate[3] = {64, 70, 104};
constexpr std::uint8_t kClockFace[3] = {252, 250, 236};
constexpr std::uint8_t kBrick[3] = {176, 72, 56};
constexpr std::uint8_t kChimneyCap[3] = {70, 60, 58};

constexpr float kWallPiece = 12.0f;  // long walls get their window ribbons in pieces this long (render3d WALL_PIECE)

// A quad a-b-c-d turned so its front faces `out`.
void face(MeshBuilder& m, Vec3 a, Vec3 b, Vec3 c, Vec3 d, Vec3 out, const std::uint8_t rgb[3]) {
    if (dot(cross(b - a, d - a), out) < 0) m.quad(d, c, b, a, rgb);
    else m.quad(a, b, c, d, rgb);
}

// A flat convex polygon (a fan) turned to face `out`.
void polygon(MeshBuilder& m, std::vector<Vec3> p, Vec3 out, const std::uint8_t rgb[3]) {
    if (p.size() < 3) return;
    if (dot(cross(p[1] - p[0], p[2] - p[0]), out) < 0) std::reverse(p.begin(), p.end());
    const Vec3 n = normalize(cross(p[1] - p[0], p[2] - p[0]));
    const std::uint32_t first = m.add(p[0], n, rgb);
    for (std::size_t k = 1; k < p.size(); ++k) m.add(p[k], n, rgb);
    for (std::uint32_t k = 1; k + 1 < p.size(); ++k) m.tri(first, first + k, first + k + 1);
}

// A local frame: origin, forward (a) and right (b) directions on the ground, heights from z_base.
struct Frame {
    float x = 0, y = 0, fx = 1, fy = 0, rx = 0, ry = 1, z_base = 0;
    Vec3 P(float a, float b, float z) const { return {x + fx * a + rx * b, y + fy * a + ry * b, z_base + z}; }
    Vec3 F() const { return {fx, fy, 0}; }
    Vec3 R() const { return {rx, ry, 0}; }
};

Frame heading_frame(float x, float y, float axis, float z_base) {  // render3d _Frame: right = (-sin, cos)
    const float c = std::cos(axis), s = std::sin(axis);
    return {x, y, c, s, -s, c, z_base};
}

// An upright box a0..a1 along, b0..b1 across, z0..z1 (no bottom).
void box(MeshBuilder& m, const Frame& f, float a0, float a1, float b0, float b1, float z0, float z1,
         const std::uint8_t side[3], const std::uint8_t top[3]) {
    const Vec3 F = f.F(), R = f.R(), up{0, 0, 1};
    face(m, f.P(a1, b0, z0), f.P(a1, b1, z0), f.P(a1, b1, z1), f.P(a1, b0, z1), F, side);
    face(m, f.P(a0, b0, z0), f.P(a0, b1, z0), f.P(a0, b1, z1), f.P(a0, b0, z1), F * -1.0f, side);
    face(m, f.P(a0, b1, z0), f.P(a1, b1, z0), f.P(a1, b1, z1), f.P(a0, b1, z1), R, side);
    face(m, f.P(a0, b0, z0), f.P(a1, b0, z0), f.P(a1, b0, z1), f.P(a0, b0, z1), R * -1.0f, side);
    face(m, f.P(a0, b0, z1), f.P(a1, b0, z1), f.P(a1, b1, z1), f.P(a0, b1, z1), up, top);
}

// A box whose top (t*) is smaller than its bottom (b*): a spire, a tower cap.
void frustum(MeshBuilder& m, const Frame& f, const float b[4], const float t[4], float z0, float z1,
             const std::uint8_t rgb[3]) {
    const Vec3 F = f.F(), R = f.R(), up{0, 0, 0.3f};
    face(m, f.P(b[1], b[2], z0), f.P(b[1], b[3], z0), f.P(t[1], t[3], z1), f.P(t[1], t[2], z1), F + up, rgb);
    face(m, f.P(b[0], b[2], z0), f.P(b[0], b[3], z0), f.P(t[0], t[3], z1), f.P(t[0], t[2], z1), F * -1.0f + up, rgb);
    face(m, f.P(b[0], b[3], z0), f.P(b[1], b[3], z0), f.P(t[1], t[3], z1), f.P(t[0], t[3], z1), R + up, rgb);
    face(m, f.P(b[0], b[2], z0), f.P(b[1], b[2], z0), f.P(t[1], t[2], z1), f.P(t[0], t[2], z1), R * -1.0f + up, rgb);
    if (t[1] - t[0] > 0.2f) face(m, f.P(t[0], t[2], z1), f.P(t[1], t[2], z1), f.P(t[1], t[3], z1), f.P(t[0], t[3], z1), {0, 0, 1}, rgb);
}

// One wall of the outline: u runs along it from its start, `out` away from the building, z up from `hi`.
struct Wall {
    float x0, y0, ux, uy, nx, ny, L, hi;
    Vec3 at(float u, float out, float z) const { return {x0 + ux * u + nx * out, y0 + uy * u + ny * out, hi + z}; }
    Vec3 n() const { return {nx, ny, 0}; }
    void rect(MeshBuilder& m, float u0, float u1, float z0, float z1, const std::uint8_t rgb[3], float out) const {
        face(m, at(u0, out, z0), at(u1, out, z0), at(u1, out, z1), at(u0, out, z1), n(), rgb);
    }
    Frame frame() const { return {x0, y0, ux, uy, nx, ny, hi}; }  // a: along the wall, b: out from it
};

std::vector<Wall> walls_of(const Building& b, float hi) {
    const auto& p = b.points;
    float area = 0;
    for (std::size_t k = 0; k < p.size(); ++k) {
        const Point2 q = p[(k + 1) % p.size()];
        area += p[k].x * q.y - q.x * p[k].y;
    }
    std::vector<Wall> out;
    for (std::size_t k = 0; k < p.size(); ++k) {
        const Point2 a = p[k], c = p[(k + 1) % p.size()];
        const float dx = c.x - a.x, dy = c.y - a.y, L = std::hypot(dx, dy);
        const float l = L > 1e-6f ? L : 1.0f;
        const float nx = area > 0 ? dy / l : -dy / l, ny = area > 0 ? -dx / l : dx / l;  // outward (render3d)
        out.push_back({a.x, a.y, dx / l, dy / l, nx, ny, L, hi});
    }
    return out;
}

void porch(MeshBuilder& m, const Wall& w, float fh, float low) {  // render3d _draw_porch
    const float a = w.L * 0.1f, b = w.L * 0.9f, d = 1.8f, roof_z = 2.9f;
    const Frame f = w.frame();
    box(m, f, a, b, 0.0f, d, low, fh, kPorchWood, kPorchWood);                       // the deck
    for (float u : {a + 0.15f, (a + b) / 2, b - 0.15f})
        box(m, f, u - 0.09f, u + 0.09f, d - 0.24f, d - 0.06f, fh, roof_z, kTrim, kTrim);  // posts
    // the shed roof: from the wall (higher) out past the posts
    const float r0 = a - 0.2f, r1 = b + 0.2f, o1 = d + 0.2f;
    const Vec3 up{0, 0, 1}, out = w.n(), along{w.ux, w.uy, 0};
    face(m, f.P(r0, 0, roof_z + 0.55f), f.P(r1, 0, roof_z + 0.55f), f.P(r1, o1, roof_z + 0.25f), f.P(r0, o1, roof_z + 0.25f),
         up + out * 0.15f, kPorchRoof);
    face(m, f.P(r0, o1, roof_z), f.P(r1, o1, roof_z), f.P(r1, o1, roof_z + 0.25f), f.P(r0, o1, roof_z + 0.25f), out, kPorchRoof);
    face(m, f.P(r0, 0, roof_z), f.P(r1, 0, roof_z), f.P(r1, o1, roof_z), f.P(r0, o1, roof_z), up * -1.0f, kPorchRoof);
    for (float s : {-1.0f, 1.0f}) {
        const float u = s < 0 ? r0 : r1;
        polygon(m, {f.P(u, 0, roof_z), f.P(u, o1, roof_z), f.P(u, o1, roof_z + 0.25f), f.P(u, 0, roof_z + 0.55f)}, along * s, kPorchRoof);
    }
}

void house_wall(MeshBuilder& m, const Building& b, int index, int k, const Wall& w, float low) {  // _draw_house_wall
    const BuildingDetail& d = b.detail;
    const float L = w.L, h = b.height;
    if (L < 1.5f) return;
    const float fh = d.house == kMiniHome ? 0.35f : 0.75f;  // foundation / skirting
    w.rect(m, 0, L, low, fh, kFoundation, 0.03f);
    const bool front = d.front == k;
    if (d.house == kGarage) {
        if (front || L >= 5.0f) {
            const float gw = std::min(2.6f, L - 0.8f);
            w.rect(m, (L - gw) / 2, (L + gw) / 2, 0.05f, 2.2f, (d.flags & kOwnDoor) ? &d.door_rgb.r : kGarageDoor, 0.05f);
        }
        return;
    }
    float door_u = -1, side = 0;
    if (front) {
        const float du = d.door * L;
        if (du > 0.6f && du < L - 0.6f) door_u = du;
    }
    const float dtop = std::min(fh + 2.1f, h - 0.2f);  // the door stays under the eaves
    if (door_u >= 0) {
        const std::uint8_t* door_rgb = (d.flags & kOwnDoor) ? &d.door_rgb.r : kDoors[index % 4];
        if (d.flags & kSidelights) {  // narrow glass panes either side
            side = 0.42f;
            w.rect(m, door_u - 0.6f - side, door_u + 0.6f + side, fh, dtop + 0.15f, kTrim, 0.05f);
            for (float s0 : {-1.0f, 1.0f}) {
                const float p = door_u + s0 * 0.54f, q = door_u + s0 * (0.54f + side - 0.08f);
                w.rect(m, std::min(p, q), std::max(p, q), fh + 0.35f, dtop - 0.05f, kGlass, 0.07f);
            }
        } else {
            w.rect(m, door_u - 0.6f, door_u + 0.6f, fh, dtop + 0.15f, kTrim, 0.05f);
        }
        w.rect(m, door_u - 0.48f, door_u + 0.48f, fh, dtop, door_rgb, 0.07f);
        if ((d.flags & kPlaque) && dtop + 1.2f < h) w.rect(m, door_u - 0.32f, door_u + 0.32f, dtop + 0.45f, dtop + 1.1f, &d.plaque.r, 0.07f);
    }
    const int n = std::max(1, static_cast<int>((L - 0.8f) / 2.7f));
    const float ww = d.house == kMiniHome ? 1.2f : 0.95f;
    const float sill = fh + (d.house == kMiniHome ? 0.9f : 0.85f);
    for (int row = 0; row < d.storeys; ++row) {
        if (front && (d.flags & kNoFrontWindows)) break;  // e.g. a meeting house: door only
        float z = sill + row * 2.8f, wh = d.window_h;
        if (z + wh > h - 0.25f) {  // low walls: a shorter window, set lower (front only, as Python)
            if (!front) break;
            z = std::max(fh + 0.3f, std::min(z, h - 0.25f - 1.0f));
            wh = std::min(1.3f, h - 0.25f - z);
            if (wh < 0.6f) break;
        }
        for (int c = 0; c < n; ++c) {
            const float uc = L * (c + 0.5f) / n;
            if (door_u >= 0 && row == 0 && std::fabs(uc - door_u) < 1.4f + side) continue;
            w.rect(m, uc - ww / 2 - 0.12f, uc + ww / 2 + 0.12f, z - 0.12f, z + wh + 0.12f, kTrim, 0.05f);
            w.rect(m, uc - ww / 2, uc + ww / 2, z, z + wh, kGlass, 0.07f);
            if (d.flags & kShutters) {
                for (float s0 : {-1.0f, 1.0f}) {
                    const float p = uc + s0 * (ww / 2 + 0.16f), q = p + s0 * 0.42f;
                    w.rect(m, std::min(p, q), std::max(p, q), z - 0.08f, z + wh + 0.08f, &d.shutters.r, 0.07f);
                }
            }
        }
    }
    if ((d.flags & kPorch) && (front || d.wrap == k)) porch(m, w, fh, low);
}

void shop_front(MeshBuilder& m, const Building& b, int index, const Wall& w) {  // _draw_shop_front
    const float L = w.L, h = b.height;
    const int bays = std::max(1, static_cast<int>(L / 3.6f));
    const float bw = L / bays;
    const int door_bay = (index * 7) % bays;
    if (h >= 4.2f) w.rect(m, 0.15f, L - 0.15f, 3.0f, 3.75f, kSigns[index % 5], 0.06f);  // the sign band
    const std::uint8_t* door = kDoors[index % 4];
    for (int k = 0; k < bays; ++k) {
        const float a = k * bw + 0.35f, c = (k + 1) * bw - 0.35f;
        if (k == door_bay) {
            const float mid = (a + c) / 2;
            w.rect(m, a, mid - 0.55f, 0.5f, 2.7f, kGlass, 0.06f);
            w.rect(m, mid - 0.5f, mid + 0.5f, 0.0f, 2.6f, door, 0.06f);
            w.rect(m, mid + 0.55f, c, 0.5f, 2.7f, kGlass, 0.06f);
        } else {
            w.rect(m, a, c, 0.5f, 2.8f, kGlass, 0.06f);
        }
    }
    const int storeys = b.detail.shop_storeys ? b.detail.shop_storeys : std::max(1, static_cast<int>((h + 0.6f) / 3.8f));
    const int n = std::max(1, static_cast<int>(L / 2.5f));
    float z = 4.3f;
    for (int row = 0; row + 1 < storeys; ++row, z += 3.6f) {
        if (z + 1.7f > h - 0.4f) break;
        for (int k = 0; k < n; ++k) {
            const float uc = L * (k + 0.5f) / n;
            w.rect(m, uc - 0.6f, uc + 0.6f, z - 0.15f, z + 1.85f, kCorniceRgb, 0.05f);
            w.rect(m, uc - 0.45f, uc + 0.45f, z, z + 1.7f, kGlass, 0.07f);
        }
    }
}

// Long walls in render3d's 12 m pieces: calls fn(u0, u1) for each piece.
template <class Fn>
void pieces(float L, Fn fn) {
    const int n = L > kWallPiece * 1.25f ? static_cast<int>(std::ceil(L / kWallPiece)) : 1;
    for (int k = 0; k < n; ++k) fn(L * k / n, L * (k + 1) / n);
}

void ribbons(MeshBuilder& m, const Wall& w, float h) {  // _draw_windows: a ribbon of glass per 3.2 m storey
    pieces(w.L, [&](float u0, float u1) {
        const float Lp = u1 - u0;
        if (Lp < 2.6f) return;
        const float inset = std::min(0.9f, Lp * 0.15f);
        for (float z = 1.1f; z + 1.3f < h - 0.4f; z += 3.2f) w.rect(m, u0 + inset, u1 - inset, z, z + 1.3f, kWindows, 0.05f);
    });
}

void arched(MeshBuilder& m, const Wall& w, float h) {  // _draw_arched_windows: civic sandstone
    pieces(w.L, [&](float u0, float u1) {
        const float Lp = u1 - u0;
        if (Lp < 3.0f) return;
        const int n = std::max(1, static_cast<int>((Lp - 1.0f) / 2.8f));
        const float step = (Lp - 1.0f) / n;
        for (float z = 1.2f; z + 2.4f < h - 0.9f; z += 4.0f) {
            for (int k = 0; k < n; ++k) {
                const float uc = u0 + 0.5f + step * (k + 0.5f);
                std::vector<Vec3> p = {w.at(uc - 0.55f, 0.05f, z), w.at(uc + 0.55f, 0.05f, z), w.at(uc + 0.55f, 0.05f, z + 1.7f)};
                for (float a : {0.6f, 1.2f, 1.94f, 2.54f})
                    p.push_back(w.at(uc + 0.55f * std::cos(a), 0.05f, z + 1.7f + 0.55f * std::sin(a)));
                p.push_back(w.at(uc - 0.55f, 0.05f, z + 1.7f));
                polygon(m, p, w.n(), kWindows);
            }
        }
    });
}

// The ridge of a four-cornered gable/hip roof: centre, along-ridge unit, half length, half width (render3d _chimney).
struct Ridge {
    float cx, cy, ux, uy, hl, hw;
};
bool ridge_of(const Building& b, Ridge& r) {
    const auto& p = b.points;
    if (p.size() != 4) return false;
    r.cx = (p[0].x + p[1].x + p[2].x + p[3].x) / 4;
    r.cy = (p[0].y + p[1].y + p[2].y + p[3].y) / 4;
    const float e1x = p[1].x - p[0].x, e1y = p[1].y - p[0].y, e2x = p[2].x - p[1].x, e2y = p[2].y - p[1].y;
    const float l1 = std::hypot(e1x, e1y), l2 = std::hypot(e2x, e2y);
    const bool across = (b.flags & 1) != 0;
    const bool first = (l1 >= l2) != across;  // the ridge runs along the long side unless "across"
    const float ex = first ? e1x : e2x, ey = first ? e1y : e2y, L = std::max(1e-6f, first ? l1 : l2);
    r.ux = ex / L;
    r.uy = ey / L;
    r.hl = (first ? l1 : l2) / 2;
    r.hw = (first ? l2 : l1) / 2;
    return true;
}

void chimney(MeshBuilder& m, const Building& b, float hi) {
    Ridge r;
    if (!ridge_of(b, r)) return;
    const float at = b.detail.chimney, s = 0.375f;
    const Frame f{r.cx + r.ux * r.hl * at * 0.8f, r.cy + r.uy * r.hl * at * 0.8f, r.ux, r.uy, -r.uy, r.ux, hi};
    const float z0 = b.height + b.roof_height - 0.4f, z1 = b.height + b.roof_height + 1.1f;
    box(m, f, -s, s, -s, s, z0, z1, kBrick, kChimneyCap);
}

void dormers(MeshBuilder& m, const Building& b, float hi) {  // _draw_dormers: on the street-facing slope
    Ridge r;
    if (!ridge_of(b, r)) return;
    const float h = b.height, rh = b.roof_height, hw = r.hw, hl = r.hl;
    if (rh < 1.6f || hw < 2.0f || !(b.roof == kGable || b.roof == kHip || b.roof == kMansard)) return;
    const float side = b.detail.dormer_side < 0 ? -1.0f : 1.0f;
    const float ux = r.ux, uy = r.uy, vx = -uy * side, vy = ux * side;
    float vf, z0, ztop, vb;
    if (b.roof == kMansard) {
        vf = hw - 0.2f;
        z0 = h + 0.2f;
        ztop = h + std::min(rh - 0.2f, 1.9f);
        vb = hw - std::min({rh * 0.55f, hw * 0.5f, hl * 0.5f}) - 0.1f;
    } else {
        vf = hw - 0.9f;
        z0 = h + rh * (1 - vf / hw);
        ztop = z0 + 1.55f;
        if (ztop + 0.6f > h + rh) return;  // roof too shallow for a dormer
        vb = std::max(0.2f, hw * (1 - (ztop + 0.4f - h) / rh));
    }
    auto P = [&](float u, float v, float z) { return Vec3{r.cx + ux * u + vx * v, r.cy + uy * u + vy * v, hi + z}; };
    auto roof_z = [&](float v) { return b.roof == kMansard ? ztop : h + rh * (1 - v / hw); };
    const std::uint8_t wall[3] = {b.wall.r, b.wall.g, b.wall.b};
    const std::uint8_t roof[3] = {b.roof_colour.r, b.roof_colour.g, b.roof_colour.b};
    const Vec3 V{vx, vy, 0}, U{ux, uy, 0};
    const int n = b.detail.dormers;
    const float w = 1.4f;
    for (int k = 0; k < n; ++k) {
        const float uc = -hl + 2 * hl * (k + 0.5f) / n, a = uc - w / 2, c = uc + w / 2;
        for (float su : {-1.0f, 1.0f}) {  // the cheeks
            const float ue = uc + su * w / 2;
            polygon(m, {P(ue, vf, z0), P(ue, vf, ztop), P(ue, vb, ztop), P(ue, vb, roof_z(vb))}, U * su, wall);
        }
        face(m, P(a, vf, z0), P(c, vf, z0), P(c, vf, ztop), P(a, vf, ztop), V, wall);
        polygon(m, {P(a, vf, ztop), P(c, vf, ztop), P(uc, vf, ztop + 0.6f)}, V, wall);
        face(m, P(a + 0.2f, vf + 0.04f, z0 + 0.25f), P(c - 0.2f, vf + 0.04f, z0 + 0.25f), P(c - 0.2f, vf + 0.04f, ztop - 0.15f),
             P(a + 0.2f, vf + 0.04f, ztop - 0.15f), V, kTrim);
        face(m, P(a + 0.32f, vf + 0.06f, z0 + 0.37f), P(c - 0.32f, vf + 0.06f, z0 + 0.37f), P(c - 0.32f, vf + 0.06f, ztop - 0.27f),
             P(a + 0.32f, vf + 0.06f, ztop - 0.27f), V, kGlass);
        for (float su : {-1.0f, 1.0f}) {  // its own little gable roof
            const float ue = uc + su * (w / 2 + 0.12f);
            face(m, P(ue, vf + 0.15f, ztop - 0.05f), P(ue, vb, ztop - 0.05f), P(uc, vb, ztop + 0.6f), P(uc, vf + 0.15f, ztop + 0.6f),
                 U * su + Vec3{0, 0, 1}, roof);
        }
    }
}

// Centre and long axis of the outline (render3d: b[0], b[1] and extras["axis"]).
void centre_axis(const Building& b, float& cx, float& cy, float& axis) {
    const auto& p = b.points;
    cx = cy = 0;
    float best = -1;
    for (std::size_t k = 0; k < p.size(); ++k) {
        cx += p[k].x;
        cy += p[k].y;
        const Point2 q = p[(k + 1) % p.size()];
        const float L = std::hypot(q.x - p[k].x, q.y - p[k].y);
        if (L > best) {
            best = L;
            axis = std::atan2(q.y - p[k].y, q.x - p[k].x);
        }
    }
    cx /= p.size();
    cy /= p.size();
}

// A frame at the far end of the long axis, `inset` back from it (where a church's tower stands).
Frame end_frame(const Building& b, float cx, float cy, float axis, float inset, float hi) {
    const float ax = std::cos(axis), ay = std::sin(axis);
    float far = -1e9f;
    for (const Point2& q : b.points) far = std::max(far, (q.x - cx) * ax + (q.y - cy) * ay);
    const float o = std::max(0.0f, far - inset);
    return heading_frame(cx + ax * o, cy + ay * o, axis, hi);
}

void steeple(MeshBuilder& m, const Building& b, float hi) {
    float cx, cy, axis = 0;
    centre_axis(b, cx, cy, axis);
    const std::uint8_t wall[3] = {b.wall.r, b.wall.g, b.wall.b};
    const float h = b.height;
    if (b.detail.flags & kClockTower) {  // _draw_clock_tower (its moving hands: App::draw_clock_hands)
        const Frame f = heading_frame(cx, cy, axis, hi);
        const float half = 2.4f, top = h + 8.5f;
        box(m, f, -half, half, -half, half, h - 0.5f, top, wall, wall);
        box(m, f, -half - 0.2f, half + 0.2f, -half - 0.2f, half + 0.2f, top - 0.5f, top, kCorniceRgb, kCorniceRgb);
        const float bot[4] = {-half - 0.2f, half + 0.2f, -half - 0.2f, half + 0.2f}, tip[4] = {-0.1f, 0.1f, -0.1f, 0.1f};
        frustum(m, f, bot, tip, top, top + 4.5f, kSlate);
        const float zc = top - 2.6f;
        for (Vec3 n : {f.F(), f.R(), f.F() * -1.0f, f.R() * -1.0f}) {
            const float fx = cx + n.x * (half + 0.04f), fy = cy + n.y * (half + 0.04f);
            const float ux = n.y, uy = -n.x;  // the viewer's right on this face
            std::vector<Vec3> disc;
            for (int k = 0; k < 10; ++k) {
                const float a = k * 3.14159265f / 5;
                disc.push_back({fx + ux * 1.4f * std::cos(a), fy + uy * 1.4f * std::cos(a), hi + zc + 1.4f * std::sin(a)});
            }
            polygon(m, disc, n, kClockFace);
        }
        return;
    }
    if (b.detail.landmark != kChurch) return;
    const float bot19[4] = {-1.9f, 1.9f, -1.9f, 1.9f}, pt[4] = {-0.05f, 0.05f, -0.05f, 0.05f};
    if (b.detail.steeple == kSpire) {
        const Frame f = end_frame(b, cx, cy, axis, 3.0f, hi);
        box(m, f, -1.8f, 1.8f, -1.8f, 1.8f, h - 0.5f, h + 4.0f, wall, wall);
        frustum(m, f, bot19, pt, h + 4.0f, h + 15.0f, kSlate);
    } else if (b.detail.steeple == kTurret) {  // a square castellated bell tower at one corner of the front
        float area = 0;
        const auto& p = b.points;
        for (std::size_t k = 0; k < p.size(); ++k) {
            const Point2 q = p[(k + 1) % p.size()];
            area += p[k].x * q.y - q.x * p[k].y;
        }
        const float s = std::max(2.3f, std::min(4.2f, std::sqrt(std::fabs(area) / 2) / 11.0f));
        Frame f = end_frame(b, cx, cy, axis, s + 0.2f, hi);
        f.x += f.rx * s * 1.3f;
        f.y += f.ry * s * 1.3f;
        const float top = h + 2.6f * s, mm = s * 0.42f, q = s * 0.2f;
        box(m, f, -s, s, -s, s, h - 0.5f, top, wall, wall);
        const float merlons[8][4] = {{-s, -s + mm, -s, -s + mm}, {s - mm, s, -s, -s + mm}, {-s, -s + mm, s - mm, s},
                                     {s - mm, s, s - mm, s},     {-q, q, -s, -s + mm},     {-q, q, s - mm, s},
                                     {-s, -s + mm, -q, q},       {s - mm, s, -q, q}};
        for (const auto& c : merlons) box(m, f, c[0], c[1], c[2], c[3], top, top + 0.5f * s, wall, wall);
    } else if (b.detail.steeple == kTwin) {  // two towers with pointed caps either side of the front
        const Frame f = end_frame(b, cx, cy, axis, 1.8f, hi);
        const float bot18[4] = {-1.8f, 1.8f, -1.8f, 1.8f};
        for (float sd : {-1.0f, 1.0f}) {
            Frame t = f;
            t.x += f.rx * sd * 4.2f;
            t.y += f.ry * sd * 4.2f;
            const float top = h + (sd > 0 ? 7.0f : 5.0f);
            box(m, t, -1.6f, 1.6f, -1.6f, 1.6f, h - 0.5f, top, wall, wall);
            frustum(m, t, bot18, pt, top, top + 5.5f, kSlate);
        }
    }
}

}  // namespace

std::vector<ClockFace> clock_faces(const Building& b, float hi) {
    std::vector<ClockFace> out;
    if (!(b.detail.flags & kClockTower)) return out;
    float cx, cy, axis = 0;
    centre_axis(b, cx, cy, axis);
    const Frame f = heading_frame(cx, cy, axis, hi);
    const float half = 2.4f, zc = hi + b.height + 8.5f - 2.6f;
    for (Vec3 n : {f.F(), f.R(), f.F() * -1.0f, f.R() * -1.0f})
        out.push_back({cx + n.x * (half + 0.08f), cy + n.y * (half + 0.08f), zc, n.x, n.y});
    return out;
}

void add_attic_window(MeshBuilder& m, const Vec3 tri[3], Vec3 n) {
    const Vec3 a = tri[0], b = tri[1], top = tri[2];
    const float L = std::hypot(b.x - a.x, b.y - a.y), rh = top.z - a.z;
    if (L < 3.0f || rh < 1.6f) return;
    const float ux = (b.x - a.x) / L, uy = (b.y - a.y) / L, mx = (a.x + b.x) / 2, my = (a.y + b.y) / 2;
    const float zc = a.z + rh * 0.33f;
    const Vec3 dir = normalize(Vec3{n.x, n.y, 0});
    auto pane = [&](float hw, float hh, float out, const std::uint8_t rgb[3]) {
        const float x = mx + dir.x * out, y = my + dir.y * out;
        face(m, {x - ux * hw, y - uy * hw, zc - hh}, {x + ux * hw, y + uy * hw, zc - hh}, {x + ux * hw, y + uy * hw, zc + hh},
             {x - ux * hw, y - uy * hw, zc + hh}, dir, rgb);
    };
    pane(0.55f, 0.7f, 0.05f, kTrim);
    pane(0.42f, 0.57f, 0.07f, kGlass);
}

void add_building_details(MeshBuilder& m, const Building& b, int index, float hi, float base) {
    const BuildingDetail& d = b.detail;
    const float h = b.height, low = base - hi;
    const bool is_arched = d.landmark == kCourtHouse || d.landmark == kCivic || d.landmark == kChurch;
    const std::vector<Wall> walls = walls_of(b, hi);
    for (int k = 0; k < static_cast<int>(walls.size()); ++k) {
        const Wall& w = walls[k];
        if (w.L < 0.5f) continue;
        if (d.plinth_h > 0) w.rect(m, 0, w.L, low, std::min(h, d.plinth_h), &d.plinth.r, 0.03f);
        const bool shop = std::find(d.shops.begin(), d.shops.end(), static_cast<std::uint16_t>(k)) != d.shops.end();
        if (d.house != kNotHouse) house_wall(m, b, index, k, w, low);
        if (shop) shop_front(m, b, index, w);
        else if (d.house == kNotHouse && !is_arched && h > 3.5f) ribbons(m, w, h);
        if (is_arched) arched(m, w, h);
        if (d.flags & kCornice) w.rect(m, 0, w.L, h - 0.75f, h - 0.25f, kCorniceRgb, 0.1f);
        if (d.landmark == kArmoury) {  // battlements along the top of every wall
            const std::uint8_t wall[3] = {b.wall.r, b.wall.g, b.wall.b};
            for (float u = 0.3f; u + 1.2f <= w.L; u += 2.4f) box(m, w.frame(), u, u + 1.2f, -0.4f, 0.0f, h, h + 1.0f, wall, wall);
        }
    }
    for (const Paint& p : b.paint) {  // graffiti and murals, a touch further out than the windows they cross
        const Vec3 n{p.nx, p.ny, 0};
        const std::uint8_t rgb[3] = {p.rgb.r, p.rgb.g, p.rgb.b};
        for (std::size_t k = 0; k + 8 < p.xyz.size(); k += 9) {
            Vec3 v[3];
            for (int c = 0; c < 3; ++c)
                v[c] = Vec3{p.xyz[k + 3 * c] + p.nx * 0.03f, p.xyz[k + 3 * c + 1] + p.ny * 0.03f, hi + p.xyz[k + 3 * c + 2]};
            if (dot(cross(v[1] - v[0], v[2] - v[0]), n) < 0) std::swap(v[1], v[2]);
            m.tri(m.add(v[0], n, rgb), m.add(v[1], n, rgb), m.add(v[2], n, rgb));
        }
    }
    if (d.chimney > -2) chimney(m, b, hi);
    if (d.dormers) dormers(m, b, hi);
    steeple(m, b, hi);
}

}  // namespace amr
