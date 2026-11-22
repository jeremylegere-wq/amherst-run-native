#include "render/minimap.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "render/gl.h"
#include "render/shader.h"

namespace amr {

namespace {
const std::uint8_t kLand[3] = {58, 64, 82}, kBlock[3] = {74, 80, 100}, kWaterC[3] = {86, 150, 214};
const std::uint8_t kParkC[3] = {84, 138, 98}, kInkC[3] = {22, 18, 40}, kWhite[3] = {255, 255, 255};
const std::uint8_t kCasing[3] = {38, 42, 58}, kFill[3][3] = {{150, 156, 176}, {200, 206, 222}, {250, 250, 255}};
const std::uint8_t kHwyCasing[3] = {60, 44, 18}, kHwyFill[3] = {255, 208, 84};
const std::uint8_t kDeckFill[3] = {255, 236, 150}, kTrafficDot[3] = {120, 124, 150};

const char* kVertex = R"(#version 300 es
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";
// Paste the map as a disc: inside, the map; a white line just inside the edge; an ink rim outside it.
const char* kFragment = R"(#version 300 es
precision mediump float;
uniform sampler2D u_map;
uniform vec3 u_box;      // left, bottom (OpenGL rows), size in pixels
uniform vec3 u_ink, u_white;
out vec4 o_color;
void main() {
    vec2 q = gl_FragCoord.xy - u_box.xy;
    float r = u_box.z / 2.0, d = distance(q, vec2(r));
    if (d > r + 1.5) discard;
    if (d > r - 1.0) o_color = vec4(u_ink, 1.0);
    else if (d > r - 2.0) o_color = vec4(u_white, 1.0);
    else o_color = vec4(texture(u_map, q / u_box.z).rgb, 1.0);
}
)";
}  // namespace

Minimap::Minimap(const World& world) : world_(world), layer_(kSize, kSize) {
    glGenTextures(1, &tex_);
    glBindTexture(GL_TEXTURE_2D, tex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kSize, kSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex_, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glDeleteFramebuffers(1, &fbo_);
        fbo_ = 0;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    std::string err;
    prog_ = make_program(kVertex, kFragment, err);
    if (!prog_) std::fprintf(stderr, "minimap shader: %s\n", err.c_str());
    glGenVertexArrays(1, &vao_);
}

Minimap::~Minimap() {
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (prog_) glDeleteProgram(prog_);
    if (fbo_) glDeleteFramebuffers(1, &fbo_);
    if (tex_) glDeleteTextures(1, &tex_);
}

void Minimap::draw(const Car& car, const std::vector<MapBlip>& blips, double time, int x, int y, int pic_w, int pic_h,
                   unsigned picture_fbo) {
    if (!ok()) return;
    const MapData& m = world_.map();
    const double k = (kSize / 2.0 - 1.0) / kRange, c = kSize / 2.0;
    const double fx = car.fx(), fy = car.fy(), rx = car.rx(), ry = car.ry();
    auto px = [&](double wx, double wy, float& sx, float& sy) {  // heading-up: forward is up, right is right
        const double dx = wx - car.x, dy = wy - car.y;
        sx = static_cast<float>(c + (dx * rx + dy * ry) * k);
        sy = static_cast<float>(c - (dx * fx + dy * fy) * k);
    };
    const double reach = kRange * 1.45;
    auto near = [&](double x0, double y0, double x1, double y1) {
        return x1 >= car.x - reach && x0 <= car.x + reach && y1 >= car.y - reach && y0 <= car.y + reach;
    };
    layer_.begin();
    layer_.rect(0, 0, kSize, kSize, kLand);
    for (const Poly& p : m.polys) {  // water and parks
        if (p.kind != kWater && p.kind != kPark) continue;
        float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
        for (const Point2& q : p.points) { x0 = std::min(x0, q.x); y0 = std::min(y0, q.y); x1 = std::max(x1, q.x); y1 = std::max(y1, q.y); }
        if (!near(x0, y0, x1, y1)) continue;
        for (const Tri& t : p.tris) {
            float ax, ay, bx, by, cx2, cy2;
            px(p.points[t.a].x, p.points[t.a].y, ax, ay);
            px(p.points[t.b].x, p.points[t.b].y, bx, by);
            px(p.points[t.c].x, p.points[t.c].y, cx2, cy2);
            layer_.tri(ax, ay, bx, by, cx2, cy2, p.kind == kWater ? kWaterC : kParkC);
        }
    }
    world_.buildings_near(car.x, car.y, reach, [&](const Building& b) {
        for (const Tri& t : b.tris) {
            float ax, ay, bx, by, cx2, cy2;
            px(b.points[t.a].x, b.points[t.a].y, ax, ay);
            px(b.points[t.b].x, b.points[t.b].y, bx, by);
            px(b.points[t.c].x, b.points[t.c].y, cx2, cy2);
            layer_.tri(ax, ay, bx, by, cx2, cy2, kBlock);
        }
    });
    struct RoadLine {
        int order;
        const Road* r;
    };
    std::vector<RoadLine> roads;  // lanes, then streets, then main roads, highways, decks on top
    for (const Road& r : m.roads) {
        if (r.service == kDriveway || r.service == kTrack || r.service == kOtherService) continue;
        bool in = false;
        for (const Point2& q : r.points)
            if (std::fabs(q.x - car.x) < reach && std::fabs(q.y - car.y) < reach) { in = true; break; }
        if (!in) continue;
        const bool highway = r.type == kMain && r.speed_kmh >= 90;
        roads.push_back({(r.flags & kDeck ? 8 : 0) + (highway ? 4 : 0) + r.type, &r});
    }
    std::stable_sort(roads.begin(), roads.end(), [](const RoadLine& a, const RoadLine& b) { return a.order < b.order; });
    for (int fill = 0; fill < 2; ++fill)  // all casings first so junctions merge cleanly
        for (const RoadLine& rl : roads) {
            const Road& r = *rl.r;
            const bool highway = r.type == kMain && r.speed_kmh >= 90;
            const std::uint8_t* casing = (r.flags & kDeck) ? kInkC : highway ? kHwyCasing : kCasing;
            const std::uint8_t* inner = (r.flags & kDeck) ? kDeckFill : highway ? kHwyFill : kFill[r.type];
            float w = std::max(1.0f, std::round(static_cast<float>(r.width * k * (r.type == kMain ? 1.5 : 1.35))));
            if (fill) w = w > 3 ? std::max(1.0f, w - 2) : w;
            else w += w > 1 ? 2 : 1;
            for (std::size_t i = 0; i + 1 < r.points.size(); ++i) {
                float ax, ay, bx, by;
                px(r.points[i].x, r.points[i].y, ax, ay);
                px(r.points[i + 1].x, r.points[i + 1].y, bx, by);
                layer_.line(ax, ay, bx, by, w, fill ? inner : casing);
            }
        }
    const bool flash = static_cast<int>(time * 6) % 2 == 0;
    const double lim = kSize / 2.0 - 3;
    for (const MapBlip& b : blips) {
        float sx, sy;
        px(b.x, b.y, sx, sy);
        const double d = std::hypot(sx - c, sy - c);
        if (d > lim) {
            if (b.kind != 2) continue;  // job targets stick to the rim
            sx = static_cast<float>(c + (sx - c) / d * lim);
            sy = static_cast<float>(c + (sy - c) / d * lim);
        }
        const int ix = static_cast<int>(sx), iy = static_cast<int>(sy);
        if (b.kind == 0) {
            layer_.rect(ix - 1, iy - 1, 2, 2, kTrafficDot);
        } else if (b.kind == 1) {
            static const std::uint8_t red[3] = {240, 40, 40}, blue[3] = {70, 110, 255};
            layer_.rect(ix - 2, iy - 2, 5, 5, kInkC);
            layer_.rect(ix - 1, iy - 1, 3, 3, flash ? red : blue);
        } else if (b.kind == 4) {  // a tag spot: a small diamond in its colour
            layer_.tri(sx, sy - 3, sx + 3, sy, sx - 3, sy, kInkC);
            layer_.tri(sx - 3, sy, sx + 3, sy, sx, sy + 3, kInkC);
            layer_.tri(sx, sy - 2, sx + 2, sy, sx - 2, sy, b.rgb);
            layer_.tri(sx - 2, sy, sx + 2, sy, sx, sy + 2, b.rgb);
        } else if (b.kind == 3 || flash) {
            layer_.rect(ix - 3, iy - 3, 7, 7, kInkC);
            layer_.rect(ix - 2, iy - 2, 5, 5, b.rgb);
        }
    }
    const float cc = static_cast<float>(c);  // the player: an arrow pointing up
    layer_.tri(cc, cc - 6, cc + 5, cc + 5, cc, cc + 2, kInkC);
    layer_.tri(cc, cc - 6, cc, cc + 2, cc - 5, cc + 5, kInkC);
    layer_.tri(cc, cc - 4, cc + 3, cc + 3, cc, cc + 1, kWhite);
    layer_.tri(cc, cc - 4, cc, cc + 1, cc - 3, cc + 3, kWhite);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, kSize, kSize);
    layer_.end();
    // paste it into the picture
    glBindFramebuffer(GL_FRAMEBUFFER, picture_fbo);
    glViewport(0, 0, pic_w, pic_h);
    glDisable(GL_DEPTH_TEST);
    glUseProgram(prog_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex_);
    glUniform1i(glGetUniformLocation(prog_, "u_map"), 0);
    glUniform3f(glGetUniformLocation(prog_, "u_box"), static_cast<float>(x), static_cast<float>(pic_h - y - kSize),
                static_cast<float>(kSize));
    glUniform3f(glGetUniformLocation(prog_, "u_ink"), kInkC[0] / 255.0f, kInkC[1] / 255.0f, kInkC[2] / 255.0f);
    glUniform3f(glGetUniformLocation(prog_, "u_white"), 236 / 255.0f, 238 / 255.0f, 248 / 255.0f);
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

}  // namespace amr
