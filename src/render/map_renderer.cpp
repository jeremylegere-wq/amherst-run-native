#include "render/map_renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "render/building_detail.h"
#include "render/car_mesh.h"
#include "render/landmarks.h"
#include "render/palette.h"
#include "render/shader.h"
#include "render/weather.h"

namespace amr {

namespace {

const char* kVertex = R"(#version 300 es
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec4 a_color;
uniform mat4 u_mvp;     // view-projection
uniform mat4 u_model;   // identity for the map; places the car
out vec3 v_normal;
out vec3 v_world;
out vec3 v_color;
void main() {
    vec4 w = u_model * vec4(a_pos, 1.0);
    v_world = w.xyz;
    v_normal = mat3(u_model) * a_normal;
    v_color = a_color.rgb;
    gl_Position = u_mvp * w;
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
uniform vec3 u_tint;    // white for the map; the car's colour on the white box
uniform float u_inked;  // 1 for buildings, cars and props (they get ink outlines), 0 for the ground, roads, areas
uniform vec4 u_grade;   // the weather's colour grade: multipliers, then desaturation
out vec4 o_color;
void main() {
    vec3 n = normalize(v_normal);
    float d = dot(n, normalize(vec3(-0.45, 0.40, 0.80)));   // the sun (game/render3d.py LIGHT, map y south)
    float lit = n.z > 0.9 ? 1.0 : (d > 0.25 ? 0.95 : 0.66);   // flat ground stays its own colour
    float dist = distance(v_world, u_eye);
    float fog = floor(clamp((dist - u_fog_range.x) / (u_fog_range.y - u_fog_range.x), 0.0, 1.0) * 4.0) / 4.0;
    vec3 c = min(v_color * u_tint * lit * u_grade.rgb, vec3(1.0));
    c = mix(c, vec3(dot(c, vec3(0.3, 0.59, 0.11))), u_grade.a);
    o_color = vec4(mix(c, u_fog, fog), u_inked);
}
)";

const char* kShadowVertex = R"(#version 300 es
layout(location = 0) in vec3 a_pos;
uniform mat4 u_mvp;
void main() { gl_Position = u_mvp * vec4(a_pos, 1.0); }
)";
const char* kShadowFragment = R"(#version 300 es
precision mediump float;
uniform vec4 u_rgba;
out vec4 o_color;
void main() { o_color = u_rgba; }
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

// An upright box centred on (x, y) from z0 up h, hx along `heading` and hy across it (no bottom face).
void prop_box(MeshBuilder& m, float x, float y, float z0, float hx, float hy, float h, float heading,
              const std::uint8_t rgb[3]) {
    const float c = std::cos(heading), s = std::sin(heading);
    auto P = [&](float a, float b, float z) { return Vec3{x + c * a - s * b, y + s * a + c * b, z}; };
    const Vec3 q[8] = {P(-hx, -hy, z0), P(hx, -hy, z0), P(hx, hy, z0), P(-hx, hy, z0),
                       P(-hx, -hy, z0 + h), P(hx, -hy, z0 + h), P(hx, hy, z0 + h), P(-hx, hy, z0 + h)};
    m.quad(q[4], q[5], q[6], q[7], rgb);
    m.quad(q[0], q[1], q[5], q[4], rgb);
    m.quad(q[1], q[2], q[6], q[5], rgb);
    m.quad(q[2], q[3], q[7], q[6], rgb);
    m.quad(q[3], q[0], q[4], q[7], rgb);
}

// A tree: a trunk and a rounded two-tone canopy (lit above, shaded underneath): eight sides, two rings, each tree
// turned a little differently so a row of them does not line up. 32 triangles: round enough at 320x180.
void tree(MeshBuilder& m, float x, float y, float g, float h, float r, int c, bool snowy) {
    prop_box(m, x, y, g, 0.12f, 0.12f, h * 0.62f, 0.0f, palette::kTrunk);
    const float zc = g + h - r * 0.8f, vz = r * 0.9f;
    const std::uint8_t* lit = snowy ? snow::kTreeLit[c % 3] : palette::kTreeLit[c % 3];
    const std::uint8_t* shade = snowy ? snow::kTreeShade[c % 3] : palette::kTreeShade[c % 3];
    const float turn = std::fmod(std::fabs(x * 0.37f + y * 0.61f), 0.785f);
    Vec3 lo[8], hi[8];
    for (int k = 0; k < 8; ++k) {
        const float a = turn + k * 0.7853982f, a2 = a + 0.3927f;  // the upper ring sits half a side round
        lo[k] = {x + r * std::cos(a), y + r * std::sin(a), zc - vz * 0.2f};
        hi[k] = {x + r * 0.78f * std::cos(a2), y + r * 0.78f * std::sin(a2), zc + vz * 0.45f};
    }
    const Vec3 top{x, y, zc + vz}, bottom{x, y, zc - vz * 0.7f};
    auto tri = [&](Vec3 a, Vec3 b, Vec3 c2, const std::uint8_t* col) {
        const Vec3 n = normalize(cross(b - a, c2 - a));
        m.tri(m.add(a, n, col), m.add(b, n, col), m.add(c2, n, col));
    };
    for (int k = 0; k < 8; ++k) {
        const int j = (k + 1) % 8;
        tri(hi[k], hi[j], top, lit);          // the crown
        tri(lo[k], lo[j], hi[k], lit);        // the band, zig-zag between the two rings
        tri(lo[j], hi[j], hi[k], lit);
        tri(lo[k], bottom, lo[j], shade);     // the underside
    }
}

}  // namespace

std::vector<RoofFace> roof_faces(const std::vector<Point2>& pts, float h, int kind, float rh, bool across) {
    std::vector<RoofFace> out;
    const std::size_t n = pts.size();
    if (rh <= 0 || n < 3) return out;
    if (kind == kCone) {  // any outline: one face from each wall up to a point over the middle
        float cx = 0, cy = 0;
        for (const auto& p : pts) { cx += p.x; cy += p.y; }
        cx /= n; cy /= n;
        for (std::size_t k = 0; k < n; ++k) {
            RoofFace f;
            f.n = 3;
            f.v[0] = {pts[k].x, pts[k].y, h};
            f.v[1] = {pts[(k + 1) % n].x, pts[(k + 1) % n].y, h};
            f.v[2] = {cx, cy, h + rh};
            out.push_back(f);
        }
        return out;
    }
    if ((kind != kGable && kind != kHip && kind != kMansard) || n != 4) return out;
    const float cx = (pts[0].x + pts[1].x + pts[2].x + pts[3].x) / 4, cy = (pts[0].y + pts[1].y + pts[2].y + pts[3].y) / 4;
    const float e1x = pts[1].x - pts[0].x, e1y = pts[1].y - pts[0].y, e2x = pts[2].x - pts[1].x, e2y = pts[2].y - pts[1].y;
    const float l1 = std::hypot(e1x, e1y), l2 = std::hypot(e2x, e2y);
    if (l1 < 0.1f || l2 < 0.1f) return out;
    float ux, uy, hl, hw;  // the ridge runs along u: the long side, or the short way for a cross gable
    if ((l1 >= l2) != across) { ux = e1x / l1; uy = e1y / l1; hl = l1 / 2; hw = l2 / 2; }
    else { ux = e2x / l2; uy = e2y / l2; hl = l2 / 2; hw = l1 / 2; }
    const float vx = -uy, vy = ux, top = h + rh;
    auto P = [&](float a, float b, float z) { return Vec3{cx + ux * a + vx * b, cy + uy * a + vy * b, z}; };
    auto face = [&](std::initializer_list<Vec3> vs) {
        RoofFace f;
        f.n = 0;
        for (const Vec3& v : vs) f.v[f.n++] = v;
        out.push_back(f);
    };
    if (kind == kGable) {
        face({P(-hl, -hw, h), P(hl, -hw, h), P(hl, 0, top), P(-hl, 0, top)});
        face({P(hl, hw, h), P(-hl, hw, h), P(-hl, 0, top), P(hl, 0, top)});
        face({P(hl, -hw, h), P(hl, hw, h), P(hl, 0, top)});
        face({P(-hl, hw, h), P(-hl, -hw, h), P(-hl, 0, top)});
    } else if (kind == kHip) {
        const float r = std::max(0.0f, hl - hw);
        face({P(-hl, -hw, h), P(hl, -hw, h), P(r, 0, top), P(-r, 0, top)});
        face({P(hl, hw, h), P(-hl, hw, h), P(-r, 0, top), P(r, 0, top)});
        face({P(hl, -hw, h), P(hl, hw, h), P(r, 0, top)});
        face({P(-hl, hw, h), P(-hl, -hw, h), P(-r, 0, top)});
    } else {  // mansard: steep skirt, flat top
        const float i = std::min({rh * 0.55f, hw * 0.5f, hl * 0.5f});
        const float eave[4][2] = {{-hl, -hw}, {hl, -hw}, {hl, hw}, {-hl, hw}};
        const float crown[4][2] = {{-(hl - i), -(hw - i)}, {hl - i, -(hw - i)}, {hl - i, hw - i}, {-(hl - i), hw - i}};
        for (int k = 0; k < 4; ++k) {
            const int k1 = (k + 1) % 4;
            face({P(eave[k][0], eave[k][1], h), P(eave[k1][0], eave[k1][1], h), P(crown[k1][0], crown[k1][1], top),
                  P(crown[k][0], crown[k][1], top)});
        }
        face({P(crown[0][0], crown[0][1], top), P(crown[1][0], crown[1][1], top), P(crown[2][0], crown[2][1], top),
              P(crown[3][0], crown[3][1], top)});
    }
    return out;
}

MapRenderer::MapRenderer(const World& world) : world_(world) {
    std::string err;
    program_ = make_program(kVertex, kFragment, err);
    if (!program_) {
        std::fprintf(stderr, "map shader: %s\n", err.c_str());
        return;
    }
    u_mvp_ = glGetUniformLocation(program_, "u_mvp");
    u_model_ = glGetUniformLocation(program_, "u_model");
    u_eye_ = glGetUniformLocation(program_, "u_eye");
    u_fog_ = glGetUniformLocation(program_, "u_fog");
    u_fog_range_ = glGetUniformLocation(program_, "u_fog_range");
    u_grade_ = glGetUniformLocation(program_, "u_grade");
    u_tint_ = glGetUniformLocation(program_, "u_tint");
    u_inked_ = glGetUniformLocation(program_, "u_inked");
    shadow_prog_ = make_program(kShadowVertex, kShadowFragment, err);
    if (!shadow_prog_) std::fprintf(stderr, "shadow shader: %s\n", err.c_str());
    glGenVertexArrays(1, &shadow_vao_);
    glGenBuffers(1, &shadow_vbo_);
    glBindVertexArray(shadow_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, shadow_vbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glBindVertexArray(0);
    const auto& m = world_.map();
    water_level_.resize(m.polys.size(), 0.0f);
    for (std::size_t i = 0; i < m.polys.size(); ++i) {
        float lo = 1e9f;
        for (const auto& p : m.polys[i].points) lo = std::min(lo, world_.ground(p.x, p.y));
        water_level_[i] = lo;
    }
    bucket();
    MeshBuilder b;  // the unit box for draw_box: -0.5..0.5 in x and y, 0..1 in z, white (tinted by the caller's colour)
    const Vec3 c[8] = {{-.5f, -.5f, 0}, {.5f, -.5f, 0}, {.5f, .5f, 0}, {-.5f, .5f, 0},
                       {-.5f, -.5f, 1}, {.5f, -.5f, 1}, {.5f, .5f, 1}, {-.5f, .5f, 1}};
    const std::uint8_t white[3] = {255, 255, 255};
    b.quad(c[4], c[5], c[6], c[7], white);  // top
    b.quad(c[0], c[1], c[5], c[4], white);  // sides
    b.quad(c[1], c[2], c[6], c[5], white);
    b.quad(c[2], c[3], c[7], c[6], white);
    b.quad(c[3], c[0], c[4], c[7], white);
    box_.upload(b);
}

MapRenderer::~MapRenderer() {
    if (shadow_vbo_) glDeleteBuffers(1, &shadow_vbo_);
    if (shadow_vao_) glDeleteVertexArrays(1, &shadow_vao_);
    if (shadow_prog_) glDeleteProgram(shadow_prog_);
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
    for (int i = 0; i < static_cast<int>(m.props.size()); ++i) {
        const Prop& p = m.props[i];
        add_box(p.x, p.y, p.x, p.y, {4, i, 0});
    }
    for (int i = 0; i < static_cast<int>(m.parked.size()); ++i) add_box(m.parked[i].x, m.parked[i].y, m.parked[i].x, m.parked[i].y, {6, i, 0});
    for (int w = 0; w < static_cast<int>(m.rails.size()); ++w)
        for (int s = 0; s + 1 < static_cast<int>(m.rails[w].size()); ++s) {
            const Point2 a = m.rails[w][s], b = m.rails[w][s + 1];
            add_box(std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y), {5, w, s});
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

MapRenderer::Chunk* MapRenderer::chunk(int cx, int cy, FrameStats& stats, bool may_build) {
    auto it = chunks_.find(key(cx, cy));
    if (it != chunks_.end()) return it->second.get();
    if (!may_build) return nullptr;
    auto c = std::make_unique<Chunk>();
    build(cx, cy, *c);
    ++stats.chunks_built;
    return (chunks_[key(cx, cy)] = std::move(c)).get();
}

void MapRenderer::warm(float x, float y) {
    FrameStats stats;
    const float reach = kDrawDist + kChunk * 0.71f;
    for (int cx = static_cast<int>(std::floor((x - reach) / kChunk)); cx <= static_cast<int>(std::floor((x + reach) / kChunk)); ++cx)
        for (int cy = static_cast<int>(std::floor((y - reach) / kChunk)); cy <= static_cast<int>(std::floor((y + reach) / kChunk)); ++cy)
            if (std::hypot((cx + 0.5f) * kChunk - x, (cy + 0.5f) * kChunk - y) <= reach) chunk(cx, cy, stats, true);
}

void MapRenderer::build(int cx, int cy, Chunk& out) {
    const auto& m = world_.map();
    const auto& T = m.terrain;
    const Weather& W = *weather_;
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
                    g.add({T.x0 + i * T.step, T.y0 + j * T.step, T.node(i, j)}, normalize({-gx, -gy, 1}), W.snow ? snow::kGround : palette::kGrass);
                }
            for (int j = 0; j < j1 - j0; ++j)
                for (int i = 0; i < i1 - i0; ++i) {
                    const std::uint32_t a = j * w + i, b = a + 1, c = a + w + 1, d = a + w;
                    g.tri(a, b, c);
                    g.tri(a, c, d);
                }
        }
    }

    MeshBuilder o, s, ar, sh;
    const float sdx = W.shadow_dx, sdy = W.shadow_dy;  // metres of shadow per metre of height (the weather's sun)
    auto shadow_fan = [&](const std::vector<std::pair<float, float>>& poly) {  // a convex polygon on the ground
        if (poly.size() < 3) return;
        std::vector<std::uint32_t> idx;
        for (const auto& q : poly) idx.push_back(sh.add({q.first, q.second, T.mesh_height(q.first, q.second) + 0.2f}, Vec3{0, 0, 1}, palette::kInk));
        for (std::size_t k = 1; k + 1 < idx.size(); ++k) sh.tri(idx[0], idx[k], idx[k + 1]);
    };
    const Vec3 up{0, 0, 1};
    auto items = items_.find(key(cx, cy));
    if (items != items_.end()) {
        for (const Item& it : items->second) {
            if (it.kind == 0) {  // a road segment, in pieces that follow the ground: shoulder or sidewalk, surface, paint
                const Road& rd = m.roads[it.index];
                const Point2 a = rd.points[it.sub], b = rd.points[it.sub + 1];
                const float hw = rd.width / 2, lift = road_lift(rd);
                const float seg = std::hypot(b.x - a.x, b.y - a.y);
                if (seg < 0.01f) continue;
                const int n = std::max(1, static_cast<int>(std::ceil(seg / kPiece)));
                const std::uint8_t* gravel = W.snow ? snow::kGravel : palette::kGravel;
                const std::uint8_t* col = rd.gravel ? gravel : (W.snow && rd.service == kTrack ? gravel : road_colour(rd));
                float along0 = 0;  // where this segment starts along the road (dashes are spaced along the road)
                for (int k = 0; k < it.sub; ++k)
                    along0 += std::hypot(rd.points[k + 1].x - rd.points[k].x, rd.points[k + 1].y - rd.points[k].y);
                const float dx = seg > 0 ? (b.x - a.x) / seg : 1, dy = seg > 0 ? (b.y - a.y) / seg : 0;
                // a strip of the road from t0 to t1 (fractions of the segment), `off` metres to the right of the
                // centre, `half` metres either side of that, lengthened by `ext` at both ends so neighbouring pieces
                // overlap (game/render3d.py _quad: no mitred joints, which balloon at sharp bends), lifted over the ground
                const Vec3 nrm{-dy, dx, 0};
                auto strip = [&](float t0, float t1, float off, float half, float lift_by, const std::uint8_t* c,
                                 float ext = 0.0f, MeshBuilder* into = nullptr) {
                    MeshBuilder& mb_ = into ? *into : o;
                    const float x0 = a.x + (b.x - a.x) * t0 - dx * ext, y0 = a.y + (b.y - a.y) * t0 - dy * ext;
                    const float x1 = a.x + (b.x - a.x) * t1 + dx * ext, y1 = a.y + (b.y - a.y) * t1 + dy * ext;
                    // across the strip in columns about 3 m wide, so it follows the ground's bends on steep hills
                    const int cols = std::max(1, static_cast<int>(std::ceil(2 * half / 3.0f)));
                    auto at = [&](float x, float y) { return v3(x, y, T.mesh_height(x, y) + lift_by); };
                    for (int c0 = 0; c0 < cols; ++c0) {
                        const float u0 = off - half + 2 * half * c0 / cols, u1 = off - half + 2 * half * (c0 + 1) / cols;
                        const Vec3 r0 = at(x0 + nrm.x * u0, y0 + nrm.y * u0), l0 = at(x0 + nrm.x * u1, y0 + nrm.y * u1);
                        const Vec3 r1 = at(x1 + nrm.x * u0, y1 + nrm.y * u0), l1 = at(x1 + nrm.x * u1, y1 + nrm.y * u1);
                        const auto i0 = mb_.add(r0, up, c), i1 = mb_.add(r1, up, c), i2 = mb_.add(l1, up, c), i3 = mb_.add(l0, up, c);
                        mb_.tri(i0, i1, i2);
                        mb_.tri(i0, i2, i3);
                    }
                };
                for (int k = 0; k < n; ++k) {
                    const float t0 = static_cast<float>(k) / n, t1 = static_cast<float>(k + 1) / n;
                    const float mx = a.x + (b.x - a.x) * (t0 + t1) / 2, my = a.y + (b.y - a.y) * (t0 + t1) / 2;
                    if (!inside(mx, my)) continue;
                    // the shoulder or sidewalk beside it (game/render3d.py _draw_road_pieces), under the surface
                    const int sw = !rd.town_sidewalk ? 0 : (m.downtown(mx, my) ? 2 : 1);
                    const std::uint8_t* edge = (rd.gravel && sw == 0) ? gravel : palette::kSidewalk[sw];
                    strip(t0, t1, 0.0f, hw + palette::kSidewalkW[sw], 0.04f, edge, 0.3f, &ar);  // under every road
                    strip(t0, t1, 0.0f, hw, lift, col, 0.3f);
                    // solid paint; offsets are to the right of travel (the mitre is (-dy, dx), y grows south), as Python
                    const std::uint8_t *white = palette::kLineWhite, *yellow = palette::kLineYellow;
                    const float pl = 0.15f;
                    switch (rd.marking) {
                        case kDoubleYellow:
                            strip(t0, t1, 0.2f, 0.04f, pl, yellow);
                            strip(t0, t1, -0.2f, 0.04f, pl, yellow);
                            break;
                        case kHighway:
                            strip(t0, t1, hw - 0.5f, 0.05f, pl, white);
                            strip(t0, t1, -hw + 0.5f, 0.05f, pl, white);
                            break;
                        case kEdges:  // white on the right edge, yellow on the left (a one-way road or a ramp)
                            strip(t0, t1, hw - 0.5f, 0.05f, pl, white);
                            strip(t0, t1, -(hw - 0.5f), 0.05f, pl, yellow);
                            break;
                        default: break;
                    }
                }
                // dashes: 3 m long every 10 m along the road, centred 5 m in (game/render3d.py _draw_marking)
                const bool dashed = rd.marking == kDashYellow || rd.marking == kDashWhite || (rd.marking == kHighway && hw >= 3.7f);
                if (dashed && seg > 0) {
                    for (float c = 5.0f + 10.0f * std::ceil((along0 - 6.5f) / 10.0f); c - 1.5f < along0 + seg; c += 10.0f) {
                        const float s0 = std::max(0.0f, c - 1.5f - along0), s1 = std::min(seg, c + 1.5f - along0);
                        if (s1 <= s0) continue;
                        const float mx = a.x + dx * (s0 + s1) / 2, my = a.y + dy * (s0 + s1) / 2;
                        if (!inside(mx, my)) continue;
                        const float t0 = s0 / seg, t1 = s1 / seg;
                        if (rd.marking == kDashWhite) {
                            strip(t0, t1, hw / 3, 0.05f, 0.15f, palette::kLineWhite);
                            strip(t0, t1, -hw / 3, 0.05f, 0.15f, palette::kLineWhite);
                        } else {
                            strip(t0, t1, 0.0f, 0.05f, 0.15f, rd.marking == kDashYellow ? palette::kLineYellow : palette::kLineWhite);
                        }
                    }
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
            } else if (it.kind == 2) {  // a building: sits on its highest corner, walls reach down to the land
                const Building& bd = m.buildings[it.index];
                const auto& p = bd.points;
                float lo = 1e9f, hi = -1e9f, area = 0, cxm = 0, cym = 0;
                for (std::size_t k = 0; k < p.size(); ++k) {
                    const float gz = world_.ground(p[k].x, p[k].y);
                    lo = std::min(lo, gz);
                    hi = std::max(hi, gz);
                    const Point2 q = p[(k + 1) % p.size()];
                    area += p[k].x * q.y - q.x * p[k].y;
                    cxm += p[k].x;
                    cym += p[k].y;
                }
                cxm /= p.size();
                cym /= p.size();
                hi = std::max(hi, world_.ground(cxm, cym));
                const float base = lo - 0.3f, top = hi + bd.height;
                {  // its shadow: the footprint and the footprint moved away from the sun, their convex hull
                    std::vector<std::pair<float, float>> pts;
                    for (const Point2& q : p) {
                        pts.push_back({q.x, q.y});
                        pts.push_back({q.x + sdx * bd.height, q.y + sdy * bd.height});
                    }
                    std::sort(pts.begin(), pts.end());
                    pts.erase(std::unique(pts.begin(), pts.end()), pts.end());
                    auto turn = [](const std::pair<float, float>& o2, const std::pair<float, float>& a2, const std::pair<float, float>& c2) {
                        return (a2.first - o2.first) * (c2.second - o2.second) - (a2.second - o2.second) * (c2.first - o2.first);
                    };
                    std::vector<std::pair<float, float>> hull;
                    for (int pass = 0; pass < 2; ++pass) {
                        const std::size_t start = hull.size();
                        for (std::size_t i2 = 0; i2 < pts.size(); ++i2) {
                            const auto& q = pass == 0 ? pts[i2] : pts[pts.size() - 1 - i2];
                            while (hull.size() >= start + 2 && turn(hull[hull.size() - 2], hull.back(), q) <= 0) hull.pop_back();
                            hull.push_back(q);
                        }
                        hull.pop_back();
                    }
                    shadow_fan(hull);
                }
                const std::uint8_t wall[3] = {bd.wall.r, bd.wall.g, bd.wall.b};
                for (std::size_t k = 0; k < p.size(); ++k) {
                    Point2 a = p[k], b = p[(k + 1) % p.size()];
                    if (area < 0) std::swap(a, b);  // walls face out whichever way the outline runs
                    s.quad({a.x, a.y, base}, {b.x, b.y, base}, {b.x, b.y, top}, {a.x, a.y, top}, wall);
                }
                const std::vector<RoofFace> faces = roof_faces(p, top, bd.roof, bd.roof_height, (bd.flags & 1) != 0);
                if (faces.empty()) {  // flat roof (or a pitched roof on an outline that is not four-cornered)
                    std::uint8_t roof[3];
                    shade(wall, 0.82f, roof);
                    const std::uint32_t first = static_cast<std::uint32_t>(s.verts.size());
                    for (const auto& q : p) s.add({q.x, q.y, top}, up, roof);
                    for (const Tri& t : bd.tris) s.tri(first + t.a, first + t.b, first + t.c);
                } else {
                    // snow lies on the roofs that have the style's own slate (those with a colour of their own keep it)
                    const bool slate = bd.roof_colour.r == 64 && bd.roof_colour.g == 70 && bd.roof_colour.b == 104;
                    const std::uint8_t roof[3] = {W.snow && slate ? snow::kRoof[0] : bd.roof_colour.r,
                                                  W.snow && slate ? snow::kRoof[1] : bd.roof_colour.g,
                                                  W.snow && slate ? snow::kRoof[2] : bd.roof_colour.b};
                    const Vec3 pivot{cxm, cym, top};
                    for (const RoofFace& f : faces) {
                        Vec3 n = normalize(cross(f.v[1] - f.v[0], f.v[2] - f.v[0]));
                        Vec3 mid{0, 0, 0};
                        for (int k = 0; k < f.n; ++k) mid = mid + f.v[k];
                        mid = mid * (1.0f / f.n);
                        const bool outward = dot(n, mid - pivot) >= 0;
                        if (!outward) n = n * -1.0f;
                        const bool upright = std::fabs(n.z) < 0.05f;  // a gable end: wall colour
                        const std::uint8_t* col = upright ? wall : roof;
                        std::uint32_t idx[4];
                        for (int k = 0; k < f.n; ++k) idx[k] = s.add(f.v[outward ? k : f.n - 1 - k], n, col);
                        for (int k = 1; k + 1 < f.n; ++k) s.tri(idx[0], idx[k], idx[k + 1]);
                        if (upright && f.n == 3 && (bd.detail.house == kCape || bd.detail.house == kTwoStorey)) {
                            int apex = 0;  // the gable's top corner; the other two are the eaves
                            for (int k = 1; k < 3; ++k) if (f.v[k].z > f.v[apex].z) apex = k;
                            const Vec3 tri[3] = {f.v[(apex + 1) % 3], f.v[(apex + 2) % 3], f.v[apex]};
                            add_attic_window(s, tri, n);
                        }
                    }
                }
                add_building_details(s, bd, it.index, hi, base);
            } else if (it.kind == 5) {  // a rail segment: the bed, and two rails on it
                const auto& line = m.rails[it.index];
                const Point2 a = line[it.sub], b = line[it.sub + 1];
                const Vec3 ma = mitre(line, it.sub), mb = mitre(line, it.sub + 1);
                const int n = std::max(1, static_cast<int>(std::ceil(std::hypot(b.x - a.x, b.y - a.y) / kPiece)));
                for (int k = 0; k < n; ++k) {
                    const float t0 = static_cast<float>(k) / n, t1 = static_cast<float>(k + 1) / n;
                    const float x0 = a.x + (b.x - a.x) * t0, y0 = a.y + (b.y - a.y) * t0;
                    const float x1 = a.x + (b.x - a.x) * t1, y1 = a.y + (b.y - a.y) * t1;
                    if (!inside((x0 + x1) / 2, (y0 + y1) / 2)) continue;
                    const Vec3 n0 = ma * (1 - t0) + mb * t0, n1 = ma * (1 - t1) + mb * t1;
                    auto strip = [&](float off, float hw, float lift, const std::uint8_t* col) {
                        auto at = [&](float x, float y) { return v3(x, y, T.mesh_height(x, y) + lift); };
                        const Vec3 l0 = at(x0 + n0.x * (off + hw), y0 + n0.y * (off + hw)), r0 = at(x0 + n0.x * (off - hw), y0 + n0.y * (off - hw));
                        const Vec3 l1 = at(x1 + n1.x * (off + hw), y1 + n1.y * (off + hw)), r1 = at(x1 + n1.x * (off - hw), y1 + n1.y * (off - hw));
                        const auto i0 = o.add(r0, up, col), i1 = o.add(r1, up, col), i2 = o.add(l1, up, col), i3 = o.add(l0, up, col);
                        o.tri(i0, i1, i2);
                        o.tri(i0, i2, i3);
                    };
                    strip(0.0f, 1.6f, 0.06f, palette::kRailBed);
                    strip(0.72f, 0.06f, 0.22f, palette::kRail);
                    strip(-0.72f, 0.06f, 0.22f, palette::kRail);
                }
            } else if (it.kind == 4) {  // a prop
                const Prop& p = m.props[it.index];
                if (!inside(p.x, p.y)) continue;
                const std::string& kind = m.prop_kinds[p.kind];
                const float g = world_.ground(p.x, p.y);
                const bool own = p.colour.r || p.colour.g || p.colour.b;
                const std::uint8_t mine[3] = {p.colour.r, p.colour.g, p.colour.b};
                if (kind == "tree") {
                    tree(s, p.x, p.y, g, p.h, p.r, p.c, W.snow);
                    const float ox = p.x + sdx * p.h * 0.6f, oy = p.y + sdy * p.h * 0.6f, rr = p.r * 0.9f;
                    std::vector<std::pair<float, float>> oct8;
                    for (int k2 = 0; k2 < 8; ++k2) oct8.push_back({ox + rr * std::cos(k2 * 0.7853982f), oy + rr * std::sin(k2 * 0.7853982f)});
                    shadow_fan(oct8);
                } else if (kind == "lamp") {
                    prop_box(s, p.x, p.y, g, 0.08f, 0.08f, p.h, 0, palette::kLamp);
                } else if (kind == "pole") {
                    prop_box(s, p.x, p.y, g, 0.14f, 0.14f, p.h, 0, palette::kWoodPole);
                    prop_box(s, p.x, p.y, g + p.h - 0.6f, 0.06f, 0.9f, 0.12f, p.heading, palette::kWoodPole);  // cross-arm
                } else if (kind == "tower") {
                    prop_box(s, p.x, p.y, g, 1.2f, 1.2f, p.h, p.heading, palette::kSteel);
                } else if (kind == "gravestone") {
                    prop_box(s, p.x, p.y, g, 0.08f, 0.35f, p.h, p.heading, own ? mine : palette::kStone);
                } else if (kind == "fence" && p.segment) {
                    const float dx = p.x2 - p.x, dy = p.y2 - p.y, len = std::hypot(dx, dy);
                    if (len > 0.05f)
                        prop_box(s, (p.x + p.x2) / 2, (p.y + p.y2) / 2, g, len / 2, 0.05f, std::max(0.8f, p.h),
                                 std::atan2(dy, dx), own ? mine : palette::kFence);
                } else if (kind == "hydrant") {
                    prop_box(s, p.x, p.y, g, 0.15f, 0.15f, 0.7f, 0, palette::kHydrant);
                } else if (kind == "bin") {
                    prop_box(s, p.x, p.y, g, 0.25f, 0.25f, 0.9f, 0, palette::kDarkGreen);
                } else if (kind == "bench") {
                    prop_box(s, p.x, p.y, g, 0.8f, 0.25f, 0.5f, p.heading, palette::kBench);
                } else if (kind == "stopsign" || kind == "crossbuck" || kind == "signal") {
                    prop_box(s, p.x, p.y, g, 0.05f, 0.05f, std::max(2.2f, p.h), 0, palette::kLamp);
                    const std::uint8_t* head = kind == "stopsign" ? palette::kSignRed
                                             : (kind == "signal" ? palette::kSignalBox : palette::kWhite);
                    prop_box(s, p.x, p.y, g + std::max(2.2f, p.h) - 0.3f, 0.3f, 0.06f, 0.6f, p.heading, head);
                }
                else add_landmark(s, kind, p, g);  // the lighthouse, cenotaph, dam and horse (car_block: invisible)
            } else if (it.kind == 6) {  // a parked car: its cartoon mesh, turned and set down in the chunk
                const ParkedCar& pc = m.parked[it.index];
                if (!inside(pc.x, pc.y)) continue;
                CarLook look{};  // render3d parked_cars: dark lower body, grey-blue glass, red tail lights
                const std::uint8_t body[3] = {pc.body.r, pc.body.g, pc.body.b}, roof[3] = {pc.roof.r, pc.roof.g, pc.roof.b};
                std::copy(body, body + 3, look.body);
                std::copy(roof, roof + 3, look.roof);
                look.glass[0] = 52; look.glass[1] = 62; look.glass[2] = 84;
                look.tail[0] = 200; look.tail[1] = 40; look.tail[2] = 40;
                const MeshBuilder car = build_car(car_shape(pc.model), look);
                const float c = std::cos(pc.heading), sn = std::sin(pc.heading), g = world_.ground(pc.x, pc.y);
                const std::uint32_t first = static_cast<std::uint32_t>(s.verts.size());
                for (Vertex v : car.verts) {
                    const float x = v.x, y = v.y, nx = v.nx, ny = v.ny;
                    v.x = pc.x + x * c - y * sn;
                    v.y = pc.y + x * sn + y * c;
                    v.z += g;
                    v.nx = nx * c - ny * sn;
                    v.ny = nx * sn + ny * c;
                    s.verts.push_back(v);
                }
                for (std::uint32_t k : car.idx) s.idx.push_back(first + k);
            } else {  // an area triangle (land use, park, lot, water), split to follow the ground
                const Poly& P = m.polys[it.index];
                const Tri t = P.tris[it.sub];
                std::uint8_t col[3] = {P.colour.r, P.colour.g, P.colour.b};
                if (W.snow && (P.kind == kPark || P.kind == kLandUse))  // parks and fields under snow
                    for (int i = 0; i < 3; ++i) col[i] = static_cast<std::uint8_t>(snow::kPark[i] * 0.85f + col[i] * 0.15f);
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
                    auto z = [&](Point2 u) { return water ? level + lift : T.mesh_height(u.x, u.y) + lift; };
                    const auto i0 = ar.add({q.a.x, q.a.y, z(q.a)}, up, col);
                    const auto i1 = ar.add({q.b.x, q.b.y, z(q.b)}, up, col);
                    const auto i2 = ar.add({q.c.x, q.c.y, z(q.c)}, up, col);
                    ar.tri(i0, i1, i2);
                }
            }
        }
    }
    out.ground.upload(g);
    out.areas.upload(ar);
    out.shadows.upload(sh);
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
    const Weather& W = *weather_;
    glUniform3f(u_fog_, W.fog[0] / 255.0f, W.fog[1] / 255.0f, W.fog[2] / 255.0f);
    {  // the clear day's fog runs from 70 m to the draw distance; thicker weather brings both in
        const float k = W.fog_scale / kWeathers[0].fog_scale;
        const float near = 70.0f / k, far = std::min(kDrawDist, near + (kDrawDist - 70.0f) / k);
        glUniform2f(u_fog_range_, std::min(near, far - 10.0f), far);
    }
    glUniform4f(u_grade_, W.grade[0], W.grade[1], W.grade[2], W.desat);
    const Mat4 identity;
    glUniformMatrix4fv(u_model_, 1, GL_FALSE, identity.m);
    glUniform3f(u_tint_, 1, 1, 1);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    const Vec3 fwd = normalize(Vec3{cam.target.x - cam.eye.x, cam.target.y - cam.eye.y, 0});
    const float reach = kDrawDist + kChunk * 0.71f;
    std::vector<std::pair<float, std::pair<int, int>>> wanted;  // (distance, chunk) in view
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
            wanted.push_back({d, {cx, cy}});
        }
    std::sort(wanted.begin(), wanted.end());
    std::vector<Chunk*>& visible = visible_;
    visible.clear();
    for (const auto& w : wanted)  // the nearest missing ones are built first; far ones may wait a frame or two
        if (Chunk* c = chunk(w.second.first, w.second.second, stats, stats.chunks_built < kBuildsPerFrame))
            visible.push_back(c);
    glUniform1f(u_inked_, 0.0f);
    for (Chunk* c : visible) {
        c->ground.draw();
        stats.triangles += c->ground.count() / 3;
    }
    glUniform1f(u_inked_, 1.0f);
    for (Chunk* c : visible) {
        c->solid.draw();
        stats.triangles += c->solid.count() / 3;
    }
    glUniform1f(u_inked_, 0.0f);
    glEnable(GL_POLYGON_OFFSET_FILL);  // areas and roads lie on the ground: pull them forward a little
    glPolygonOffset(-1.0f, -4.0f);
    glDepthMask(GL_FALSE);             // areas first, without depth, so the roads after them always cover them
    for (Chunk* c : visible) {         // (big flat area triangles can float a little above a creased slope)
        c->areas.draw();
        stats.triangles += c->areas.count() / 3;
    }
    glDepthMask(GL_TRUE);
    for (Chunk* c : visible) {
        c->overlay.draw();
        stats.triangles += c->overlay.count() / 3;
    }
    glDisable(GL_POLYGON_OFFSET_FILL);
    glUniform1f(u_inked_, 1.0f);  // what is drawn after (cars, the train, beacons) gets ink
    stats.chunks_drawn = static_cast<int>(visible.size());
    return stats;
}

void MapRenderer::set_weather(int k) {
    if (k < 0 || k >= kWeatherCount || &kWeathers[k] == weather_) return;
    weather_ = &kWeathers[k];
    chunks_.clear();  // snow and the sun's angle are built into the chunks: they are made again as they come into view
    visible_.clear();
}

void MapRenderer::draw_shadows(const std::vector<float>& tris, const std::uint8_t rgba[4]) {
    if (!shadow_prog_ || !weather_->shadows) return;
    glUseProgram(shadow_prog_);
    glUniformMatrix4fv(glGetUniformLocation(shadow_prog_, "u_mvp"), 1, GL_FALSE, vp_.m);
    glUniform4f(glGetUniformLocation(shadow_prog_, "u_rgba"), rgba[0] / 255.0f, rgba[1] / 255.0f, rgba[2] / 255.0f,
                rgba[3] / 255.0f);
    glBindVertexArray(shadow_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, shadow_vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(tris.size() * sizeof(float)), tris.data(), GL_STREAM_DRAW);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);  // keep the ink mask in alpha as it is
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glEnable(GL_STENCIL_TEST);  // each pixel once: overlapping shadows do not get darker
    glStencilFunc(GL_EQUAL, 0, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-1.0f, -8.0f);
    if (!tris.empty()) glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(tris.size() / 3));
    for (Chunk* c : visible_) c->shadows.draw();  // the buildings' and trees' (same program: position only)
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDisable(GL_STENCIL_TEST);
    glDepthMask(GL_TRUE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glBindVertexArray(0);
}

void MapRenderer::draw_mesh(const Mesh& mesh, const Mat4& model) {
    glUseProgram(program_);
    glUniformMatrix4fv(u_mvp_, 1, GL_FALSE, vp_.m);
    glUniformMatrix4fv(u_model_, 1, GL_FALSE, model.m);
    glUniform3f(u_tint_, 1, 1, 1);
    mesh.draw();
    const Mat4 identity;
    glUniformMatrix4fv(u_model_, 1, GL_FALSE, identity.m);
}

void MapRenderer::draw_box(const Mat4& model, const std::uint8_t rgb[3]) {
    glUseProgram(program_);
    glUniformMatrix4fv(u_mvp_, 1, GL_FALSE, vp_.m);
    glUniformMatrix4fv(u_model_, 1, GL_FALSE, model.m);
    glUniform3f(u_tint_, rgb[0] / 255.0f, rgb[1] / 255.0f, rgb[2] / 255.0f);
    box_.draw();
    const Mat4 identity;
    glUniformMatrix4fv(u_model_, 1, GL_FALSE, identity.m);
    glUniform3f(u_tint_, 1, 1, 1);
}

}  // namespace amr
