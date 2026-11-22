#include "render/map_renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "render/palette.h"
#include "render/shader.h"

namespace amr {

namespace {

const char* kVertex = R"(#version 300 es
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec4 a_color;
uniform mat4 u_mvp;
out vec3 v_normal;
out vec3 v_world;
out vec3 v_color;
void main() {
    v_world = a_pos;
    v_normal = a_normal;
    v_color = a_color.rgb;
    gl_Position = u_mvp * vec4(a_pos, 1.0);
}
)";

// Two-tone cel lighting (the street style's shade levels) and banded distance fog, per pixel.
const char* kFragment = R"(#version 300 es
precision highp float;
in vec3 v_normal;
in vec3 v_world;
in vec3 v_color;
uniform vec3 u_eye;
uniform vec3 u_fog;
uniform vec2 u_fog_range;
out vec4 o_color;
void main() {
    vec3 n = normalize(v_normal);
    float d = dot(n, normalize(vec3(0.45, -0.3, 0.85)));
    float lit = n.z > 0.9 ? 1.0 : (d > 0.25 ? 0.95 : 0.66);   // flat ground stays its own colour
    float dist = distance(v_world, u_eye);
    float fog = floor(clamp((dist - u_fog_range.x) / (u_fog_range.y - u_fog_range.x), 0.0, 1.0) * 4.0) / 4.0;
    o_color = vec4(mix(v_color * lit, u_fog, fog), 1.0);
}
)";

constexpr float kPiece = 4.0f;      // roads, decks and big areas are cut into pieces this long to follow the ground
constexpr float kTriMax = 12.0f;    // big area triangles are split until no edge is longer (the ground's grid)

Vec3 v3(float x, float y, float z) { return {x, y, z}; }

void shade(const std::uint8_t in[3], float k, std::uint8_t out[3]) {
    for (int i = 0; i < 3; ++i) out[i] = static_cast<std::uint8_t>(std::min(255.0f, in[i] * k));
}

const std::uint8_t* road_colour(const Road& r) {
    if (r.service == kTrack) return palette::kGravel;
    if (r.type == kMain) return palette::kRoadMain;
    if (r.type == kSide) return palette::kRoadSide;
    return palette::kRoadLane;
}

float road_lift(const Road& r) {  // busier roads sit a touch higher, so where they overlap at junctions they win
    return r.type == kMain ? 0.11f : (r.type == kSide ? 0.09f : 0.07f);
}

// Mitred half-width offset (unit normal * scale) at vertex i of a polyline, so pieces join without gaps.
Vec3 mitre(const std::vector<Point2>& p, std::size_t i) {
    auto seg_n = [&](std::size_t a, std::size_t b) {
        float dx = p[b].x - p[a].x, dy = p[b].y - p[a].y, l = std::hypot(dx, dy);
        return l > 1e-6f ? Vec3{-dy / l, dx / l, 0} : Vec3{0, 0, 0};
    };
    Vec3 n0 = i > 0 ? seg_n(i - 1, i) : seg_n(i, i + 1);
    Vec3 n1 = i + 1 < p.size() ? seg_n(i, i + 1) : n0;
    Vec3 m = normalize(n0 + n1);
    if (length(m) < 0.5f) return n1;
    float c = dot(m, n1);
    return m * (1.0f / std::max(c, 0.5f));  // keep the width; cap very sharp corners
}

}  // namespace

MapRenderer::MapRenderer(const World& world) : world_(world) {
    std::string err;
    program_ = make_program(kVertex, kFragment, err);
    if (!program_) {
        std::fprintf(stderr, "map shader: %s\n", err.c_str());
        return;
    }
    u_mvp_ = glGetUniformLocation(program_, "u_mvp");
    u_eye_ = glGetUniformLocation(program_, "u_eye");
    u_fog_ = glGetUniformLocation(program_, "u_fog");
    u_fog_range_ = glGetUniformLocation(program_, "u_fog_range");
    const auto& m = world_.map();
    water_level_.resize(m.polys.size(), 0.0f);
    for (std::size_t i = 0; i < m.polys.size(); ++i) {
        float lo = 1e9f;
        for (const auto& p : m.polys[i].points) lo = std::min(lo, world_.ground(p.x, p.y));
        water_level_[i] = lo;
    }
    bucket();
}

MapRenderer::~MapRenderer() {
    if (program_) glDeleteProgram(program_);
}

long long MapRenderer::key(int cx, int cy) const {
    return (static_cast<long long>(cx) << 32) ^ static_cast<unsigned>(cy);
}

// Sort every road segment, deck segment, building and area triangle into the chunks it touches, once.
void MapRenderer::bucket() {
    const auto& m = world_.map();
    auto add_box = [&](float x0, float y0, float x1, float y1, Item it) {
        for (int cx = static_cast<int>(std::floor(x0 / kChunk)); cx <= static_cast<int>(std::floor(x1 / kChunk)); ++cx)
            for (int cy = static_cast<int>(std::floor(y0 / kChunk)); cy <= static_cast<int>(std::floor(y1 / kChunk)); ++cy)
                items_[key(cx, cy)].push_back(it);
    };
    for (int r = 0; r < static_cast<int>(m.roads.size()); ++r) {
        const Road& rd = m.roads[r];
        if (rd.flags & (kDeck | kTunnel)) continue;  // decks are drawn from their spans
        for (int s = 0; s + 1 < static_cast<int>(rd.points.size()); ++s) {
            const Point2 a = rd.points[s], b = rd.points[s + 1];
            add_box(std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y), {0, r, s});
        }
    }
    for (int i = 0; i < static_cast<int>(m.spans.size()); ++i) {
        const auto& p = m.spans[i].points;
        for (int s = 0; s + 1 < static_cast<int>(p.size()); ++s)
            add_box(std::min(p[s].x, p[s + 1].x), std::min(p[s].y, p[s + 1].y), std::max(p[s].x, p[s + 1].x),
                    std::max(p[s].y, p[s + 1].y), {1, i, s});
    }
    for (int i = 0; i < static_cast<int>(m.buildings.size()); ++i) {
        const auto& p = m.buildings[i].points;
        float sx = 0, sy = 0;
        for (const auto& q : p) { sx += q.x; sy += q.y; }
        sx /= p.size(); sy /= p.size();
        add_box(sx, sy, sx, sy, {2, i, 0});  // whole building in the chunk holding its middle
    }
    for (int i = 0; i < static_cast<int>(m.polys.size()); ++i) {
        const auto& P = m.polys[i];
        for (int t = 0; t < static_cast<int>(P.tris.size()); ++t) {
            const Point2 a = P.points[P.tris[t].a], b = P.points[P.tris[t].b], c = P.points[P.tris[t].c];
            add_box(std::min({a.x, b.x, c.x}), std::min({a.y, b.y, c.y}), std::max({a.x, b.x, c.x}),
                    std::max({a.y, b.y, c.y}), {3, i, t});
        }
    }
}

MapRenderer::Chunk& MapRenderer::chunk(int cx, int cy, FrameStats& stats) {
    auto& slot = chunks_[key(cx, cy)];
    if (!slot) {
        slot = std::make_unique<Chunk>();
        build(cx, cy, *slot);
        ++stats.chunks_built;
    }
    return *slot;
}

void MapRenderer::build(int cx, int cy, Chunk& out) {
    const auto& m = world_.map();
    const auto& T = m.terrain;
    const float X0 = cx * kChunk, Y0 = cy * kChunk, X1 = X0 + kChunk, Y1 = Y0 + kChunk;
    auto inside = [&](float x, float y) { return x >= X0 && x < X1 && y >= Y0 && y < Y1; };

    // terrain: the grid nodes covering this chunk (one cell of overlap with the neighbours: same heights, no seam)
    MeshBuilder g;
    if (T.nx > 1 && T.ny > 1) {
        const int i0 = std::max(0, static_cast<int>(std::floor((X0 - T.x0) / T.step)));
        const int i1 = std::min(T.nx - 1, static_cast<int>(std::ceil((X1 - T.x0) / T.step)));
        const int j0 = std::max(0, static_cast<int>(std::floor((Y0 - T.y0) / T.step)));
        const int j1 = std::min(T.ny - 1, static_cast<int>(std::ceil((Y1 - T.y0) / T.step)));
        if (i1 > i0 && j1 > j0) {
            const int w = i1 - i0 + 1;
            for (int j = j0; j <= j1; ++j)
                for (int i = i0; i <= i1; ++i) {
                    const float gx = (T.node(i + 1, j) - T.node(i - 1, j)) / (2 * T.step);
                    const float gy = (T.node(i, j + 1) - T.node(i, j - 1)) / (2 * T.step);
                    g.add({T.x0 + i * T.step, T.y0 + j * T.step, T.node(i, j)}, normalize({-gx, -gy, 1}), palette::kGrass);
                }
            for (int j = 0; j < j1 - j0; ++j)
                for (int i = 0; i < i1 - i0; ++i) {
                    const std::uint32_t a = j * w + i, b = a + 1, c = a + w + 1, d = a + w;
                    g.tri(a, b, c);
                    g.tri(a, c, d);
                }
        }
    }

    MeshBuilder o, s;
    const Vec3 up{0, 0, 1};
    auto items = items_.find(key(cx, cy));
    if (items != items_.end()) {
        for (const Item& it : items->second) {
            if (it.kind == 0) {  // a road segment, in pieces that follow the ground
                const Road& rd = m.roads[it.index];
                const Point2 a = rd.points[it.sub], b = rd.points[it.sub + 1];
                const Vec3 ma = mitre(rd.points, it.sub), mb = mitre(rd.points, it.sub + 1);
                const float hw = rd.width / 2, lift = road_lift(rd);
                const int n = std::max(1, static_cast<int>(std::ceil(std::hypot(b.x - a.x, b.y - a.y) / kPiece)));
                const std::uint8_t* col = road_colour(rd);
                for (int k = 0; k < n; ++k) {
                    const float t0 = static_cast<float>(k) / n, t1 = static_cast<float>(k + 1) / n;
                    const float x0 = a.x + (b.x - a.x) * t0, y0 = a.y + (b.y - a.y) * t0;
                    const float x1 = a.x + (b.x - a.x) * t1, y1 = a.y + (b.y - a.y) * t1;
                    if (!inside((x0 + x1) / 2, (y0 + y1) / 2)) continue;
                    const Vec3 n0 = (ma * (1 - t0) + mb * t0) * hw, n1 = (ma * (1 - t1) + mb * t1) * hw;
                    auto at = [&](float x, float y) { return v3(x, y, world_.ground(x, y) + lift); };
                    const Vec3 l0 = at(x0 + n0.x, y0 + n0.y), r0 = at(x0 - n0.x, y0 - n0.y);
                    const Vec3 l1 = at(x1 + n1.x, y1 + n1.y), r1 = at(x1 - n1.x, y1 - n1.y);
                    const auto i0 = o.add(r0, up, col), i1 = o.add(r1, up, col), i2 = o.add(l1, up, col), i3 = o.add(l0, up, col);
                    o.tri(i0, i1, i2);
                    o.tri(i0, i2, i3);
                }
            } else if (it.kind == 1) {  // an overpass: deck slab with its thick concrete edges
                const Span& sp = m.spans[it.index];
                const SpanPoint a = sp.points[it.sub], b = sp.points[it.sub + 1];
                const float mx = (a.x + b.x) / 2, my = (a.y + b.y) / 2;
                if (!inside(mx, my)) continue;
                const float dx = b.x - a.x, dy = b.y - a.y, l = std::hypot(dx, dy);
                if (l < 1e-4f) continue;
                const float hw = sp.width / 2 + 0.5f, nx = -dy / l * hw, ny = dx / l * hw;
                const float za = world_.ground(a.x, a.y) + a.h + 0.12f, zb = world_.ground(b.x, b.y) + b.h + 0.12f;
                const Vec3 l0{a.x + nx, a.y + ny, za}, r0{a.x - nx, a.y - ny, za};
                const Vec3 l1{b.x + nx, b.y + ny, zb}, r1{b.x - nx, b.y - ny, zb};
                const Vec3 drop{0, 0, -1.1f};
                s.quad(r0, r1, l1, l0, palette::kRoadMain);
                std::uint8_t edge[3];
                shade(palette::kKerb, 1.0f, edge);
                s.quad(r0 + drop, r1 + drop, r1, r0, edge);           // right edge
                s.quad(l1 + drop, l0 + drop, l0, l1, edge);           // left edge
                s.quad(l0 + drop, l1 + drop, r1 + drop, r0 + drop, edge);  // underside
            } else if (it.kind == 2) {  // a building: walls from the lowest ground up, flat top for now
                const Building& bd = m.buildings[it.index];
                const auto& p = bd.points;
                float lo = 1e9f, hi = -1e9f, area = 0;
                for (std::size_t k = 0; k < p.size(); ++k) {
                    const float gz = world_.ground(p[k].x, p[k].y);
                    lo = std::min(lo, gz);
                    hi = std::max(hi, gz);
                    const Point2 q = p[(k + 1) % p.size()];
                    area += p[k].x * q.y - q.x * p[k].y;
                }
                const float base = lo - 0.3f, top = hi + bd.height;
                const std::uint8_t wall[3] = {bd.wall.r, bd.wall.g, bd.wall.b};
                for (std::size_t k = 0; k < p.size(); ++k) {
                    Point2 a = p[k], b = p[(k + 1) % p.size()];
                    if (area < 0) std::swap(a, b);  // walls face out whichever way the outline runs
                    s.quad({a.x, a.y, base}, {b.x, b.y, base}, {b.x, b.y, top}, {a.x, a.y, top}, wall);
                }
                std::uint8_t roof[3];
                if (bd.roof == kFlat) shade(wall, 0.82f, roof);
                else { roof[0] = bd.roof_colour.r; roof[1] = bd.roof_colour.g; roof[2] = bd.roof_colour.b; }
                const std::uint32_t first = static_cast<std::uint32_t>(s.verts.size());
                for (const auto& q : p) s.add({q.x, q.y, top}, up, roof);
                for (const Tri& t : bd.tris) s.tri(first + t.a, first + t.b, first + t.c);
            } else {  // an area triangle (land use, park, lot, water), split to follow the ground
                const Poly& P = m.polys[it.index];
                const Tri t = P.tris[it.sub];
                std::uint8_t col[3] = {P.colour.r, P.colour.g, P.colour.b};
                const bool water = P.kind == kWater;
                const float lift = P.kind == kLot ? 0.05f : (water ? 0.02f : 0.03f);
                const float level = water_level_[it.index];
                struct T2 { Point2 a, b, c; };
                std::vector<T2> todo{{P.points[t.a], P.points[t.b], P.points[t.c]}};
                while (!todo.empty()) {
                    T2 q = todo.back();
                    todo.pop_back();
                    const float ab = std::hypot(q.b.x - q.a.x, q.b.y - q.a.y);
                    const float bc = std::hypot(q.c.x - q.b.x, q.c.y - q.b.y);
                    const float ca = std::hypot(q.a.x - q.c.x, q.a.y - q.c.y);
                    const float longest = std::max({ab, bc, ca});
                    if (longest > kTriMax) {  // split the longest edge in two
                        auto mid = [](Point2 u, Point2 v) { return Point2{(u.x + v.x) / 2, (u.y + v.y) / 2}; };
                        if (longest == ab) { Point2 mm = mid(q.a, q.b); todo.push_back({q.a, mm, q.c}); todo.push_back({mm, q.b, q.c}); }
                        else if (longest == bc) { Point2 mm = mid(q.b, q.c); todo.push_back({q.a, q.b, mm}); todo.push_back({q.a, mm, q.c}); }
                        else { Point2 mm = mid(q.c, q.a); todo.push_back({q.a, q.b, mm}); todo.push_back({mm, q.b, q.c}); }
                        continue;
                    }
                    if (!inside((q.a.x + q.b.x + q.c.x) / 3, (q.a.y + q.b.y + q.c.y) / 3)) continue;
                    auto z = [&](Point2 u) { return water ? level + lift : world_.ground(u.x, u.y) + lift; };
                    const auto i0 = o.add({q.a.x, q.a.y, z(q.a)}, up, col);
                    const auto i1 = o.add({q.b.x, q.b.y, z(q.b)}, up, col);
                    const auto i2 = o.add({q.c.x, q.c.y, z(q.c)}, up, col);
                    o.tri(i0, i1, i2);
                }
            }
        }
    }
    // areas first, roads after them in the same mesh: depth decides (roads are lifted a little higher)
    out.ground.upload(g);
    out.overlay.upload(o);
    out.solid.upload(s);
}

FrameStats MapRenderer::draw(const Camera& cam, float aspect) {
    FrameStats stats;
    if (!program_) return stats;
    // The map's y grows south (y = -latitude), so x east, y, z up is left-handed: mirror y on the way to the
    // screen so the town is not drawn back to front, and count clockwise triangles as front faces.
    Mat4 flip;
    flip.at(1, 1) = -1;
    const Mat4 proj = perspective(cam.fovy, aspect, 0.5f, kDrawDist + 40.0f);
    vp_ = proj * look_at(transform_point(flip, cam.eye), transform_point(flip, cam.target)) * flip;
    glFrontFace(GL_CW);
    glUseProgram(program_);
    glUniformMatrix4fv(u_mvp_, 1, GL_FALSE, vp_.m);
    glUniform3f(u_eye_, cam.eye.x, cam.eye.y, cam.eye.z);
    glUniform3f(u_fog_, palette::kFog[0] / 255.0f, palette::kFog[1] / 255.0f, palette::kFog[2] / 255.0f);
    glUniform2f(u_fog_range_, 70.0f, kDrawDist);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    const Vec3 fwd = normalize(Vec3{cam.target.x - cam.eye.x, cam.target.y - cam.eye.y, 0});
    const float reach = kDrawDist + kChunk * 0.71f;
    std::vector<Chunk*> visible;
    const int c0x = static_cast<int>(std::floor((cam.eye.x - reach) / kChunk));
    const int c1x = static_cast<int>(std::floor((cam.eye.x + reach) / kChunk));
    const int c0y = static_cast<int>(std::floor((cam.eye.y - reach) / kChunk));
    const int c1y = static_cast<int>(std::floor((cam.eye.y + reach) / kChunk));
    for (int cx = c0x; cx <= c1x; ++cx)
        for (int cy = c0y; cy <= c1y; ++cy) {
            const float mx = (cx + 0.5f) * kChunk - cam.eye.x, my = (cy + 0.5f) * kChunk - cam.eye.y;
            const float d = std::hypot(mx, my);
            if (d > reach) continue;
            if (mx * fwd.x + my * fwd.y < -kChunk * 0.71f) continue;  // wholly behind the camera
            visible.push_back(&chunk(cx, cy, stats));
        }
    for (Chunk* c : visible) {
        c->ground.draw();
        c->solid.draw();
        stats.triangles += (c->ground.count() + c->solid.count()) / 3;
    }
    glEnable(GL_POLYGON_OFFSET_FILL);  // areas and roads lie on the ground: pull them forward a little
    glPolygonOffset(-1.0f, -4.0f);
    for (Chunk* c : visible) {
        c->overlay.draw();
        stats.triangles += c->overlay.count() / 3;
    }
    glDisable(GL_POLYGON_OFFSET_FILL);
    stats.chunks_drawn = static_cast<int>(visible.size());
    return stats;
}

void MapRenderer::draw_box(const Mat4& model, const std::uint8_t rgb[3]) {
    // a placeholder: five quads moved by `model` on the CPU and uploaded each frame (phase 1's real car replaces it)
    MeshBuilder b;
    const Vec3 c[8] = {{-.5f, -.5f, 0}, {.5f, -.5f, 0}, {.5f, .5f, 0}, {-.5f, .5f, 0},
                       {-.5f, -.5f, 1}, {.5f, -.5f, 1}, {.5f, .5f, 1}, {-.5f, .5f, 1}};
    auto P = [&](int i) { return transform_point(model, c[i]); };
    b.quad(P(4), P(5), P(6), P(7), rgb);
    b.quad(P(0), P(1), P(5), P(4), rgb);
    b.quad(P(1), P(2), P(6), P(5), rgb);
    b.quad(P(2), P(3), P(7), P(6), rgb);
    b.quad(P(3), P(0), P(4), P(7), rgb);
    Mesh tmp(b);
    glUseProgram(program_);
    glUniformMatrix4fv(u_mvp_, 1, GL_FALSE, vp_.m);
    tmp.draw();
}

}  // namespace amr
