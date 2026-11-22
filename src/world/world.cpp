#include "world/world.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace amr {

namespace {
constexpr float kCell = 40.0f;  // game/world.py CELL
long long cell_key(int gx, int gy) { return (static_cast<long long>(gx) << 32) ^ static_cast<unsigned>(gy); }
}  // namespace

namespace {
template <typename F>
void for_cells(double x0, double y0, double x1, double y1, F f) {  // world.py _cells: every cell a box touches
    for (int gx = static_cast<int>(std::floor(x0 / kCell)); gx <= static_cast<int>(std::floor(x1 / kCell)); ++gx)
        for (int gy = static_cast<int>(std::floor(y0 / kCell)); gy <= static_cast<int>(std::floor(y1 / kCell)); ++gy)
            f(cell_key(gx, gy));
}
double seg_dist(double x, double y, double ax, double ay, double bx, double by) {
    const double dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
    double t = l2 == 0 ? 0.0 : ((x - ax) * dx + (y - ay) * dy) / l2;
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    return std::hypot(x - (ax + dx * t), y - (ay + dy * t));
}
template <typename P>
void box_of(const std::vector<P>& pts, double& x0, double& y0, double& x1, double& y1) {
    x0 = y0 = 1e30;
    x1 = y1 = -1e30;
    for (const auto& p : pts) {
        x0 = std::min<double>(x0, p.x); y0 = std::min<double>(y0, p.y);
        x1 = std::max<double>(x1, p.x); y1 = std::max<double>(y1, p.y);
    }
}
}  // namespace

bool point_in_polygon(double x, double y, const std::vector<Point2>& pts) {
    bool inside = false;
    for (std::size_t i = 0, j = pts.size() - 1; i < pts.size(); j = i++) {
        const double xi = pts[i].x, yi = pts[i].y, xj = pts[j].x, yj = pts[j].y;
        if ((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi) inside = !inside;
    }
    return inside;
}

World::World(MapData data) : map_(std::move(data)) {
    for (const Road& r : map_.roads)
        for (std::size_t i = 0; i + 1 < r.points.size(); ++i) {
            const Point2 a = r.points[i], b = r.points[i + 1];
            const Seg s{a.x, a.y, b.x, b.y, r.width};
            for_cells(std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y),
                      [&](long long k) { road_cells_[k].push_back(s); });
        }
    for (int i = 0; i < static_cast<int>(map_.buildings.size()); ++i) {
        double x0, y0, x1, y1;
        box_of(map_.buildings[i].points, x0, y0, x1, y1);
        for_cells(x0, y0, x1, y1, [&](long long k) { building_cells_[k].push_back(i); });
    }
    for (int i = 0; i < static_cast<int>(map_.props.size()); ++i) {  // each prop in the cell it stands in
        const Prop& p = map_.props[i];
        prop_cells_[cell_key(static_cast<int>(std::floor(p.x / kCell)), static_cast<int>(std::floor(p.y / kCell)))].push_back(i);
    }
    for (int i = 0; i < static_cast<int>(map_.polys.size()); ++i) {
        if (map_.polys[i].kind != kLot) continue;
        double x0, y0, x1, y1;
        box_of(map_.polys[i].points, x0, y0, x1, y1);
        for_cells(x0, y0, x1, y1, [&](long long k) { lot_cells_[k].push_back(i); });
    }
    for (int i = 0; i < static_cast<int>(map_.spans.size()); ++i) {
        const Span& sp = map_.spans[i];
        for (int gx = static_cast<int>(std::floor(sp.x0 / kCell)); gx <= static_cast<int>(std::floor(sp.x1 / kCell)); ++gx)
            for (int gy = static_cast<int>(std::floor(sp.y0 / kCell)); gy <= static_cast<int>(std::floor(sp.y1 / kCell)); ++gy)
                span_cells_[cell_key(gx, gy)].push_back(i);
    }
}

float span_height(const Span& sp, float x, float y) {
    if (x < sp.x0 || x > sp.x1 || y < sp.y0 || y > sp.y1) return -1.0f;
    const float half = sp.width / 2 + 0.3f;
    float best_d = std::numeric_limits<float>::max(), best_h = -1.0f;
    for (std::size_t i = 0; i + 1 < sp.points.size(); ++i) {
        const SpanPoint& a = sp.points[i];
        const SpanPoint& b = sp.points[i + 1];
        const float dx = b.x - a.x, dy = b.y - a.y, ll = dx * dx + dy * dy;
        if (ll < 1e-9f) continue;
        float t = ((x - a.x) * dx + (y - a.y) * dy) / ll;
        t = t < 0 ? 0 : (t > 1 ? 1 : t);
        const float d = std::hypot(x - (a.x + dx * t), y - (a.y + dy * t));
        if (d <= half && d < best_d) {
            best_d = d;
            best_h = a.h + (b.h - a.h) * t;
        }
    }
    return best_h;
}

float World::height_at(float x, float y, float z_hint) const {
    const float g = ground(x, y);
    auto it = span_cells_.find(cell_key(static_cast<int>(std::floor(x / kCell)), static_cast<int>(std::floor(y / kCell))));
    if (it == span_cells_.end()) return g;
    float lowest = std::numeric_limits<float>::max(), best = 0.0f;
    const float rel = z_hint - g;
    bool any = false;
    for (int i : it->second) {
        const float h = span_height(map_.spans[i], x, y);
        if (h < 0) continue;
        any = true;
        lowest = std::fmin(lowest, h);
        if (std::fabs(h - rel) < std::fabs(best - rel)) best = h;  // the level nearest where the car is
    }
    if (!any) return g;
    if (lowest < kClearance) return g + lowest;  // a low ramp: nothing can drive beneath it
    return g + best;
}

}  // namespace amr

namespace amr {

bool World::on_road(double x, double y) const {
    const int cx = static_cast<int>(std::floor(x / kCell)), cy = static_cast<int>(std::floor(y / kCell));
    for (int gx = cx - 1; gx <= cx + 1; ++gx)
        for (int gy = cy - 1; gy <= cy + 1; ++gy) {
            auto it = road_cells_.find(cell_key(gx, gy));
            if (it == road_cells_.end()) continue;
            for (const Seg& s : it->second)
                if (seg_dist(x, y, s.ax, s.ay, s.bx, s.by) <= s.w / 2.0) return true;
        }
    return in_lot(x, y);
}

bool World::in_lot(double x, double y) const {
    auto it = lot_cells_.find(cell_key(static_cast<int>(std::floor(x / kCell)), static_cast<int>(std::floor(y / kCell))));
    if (it == lot_cells_.end()) return false;
    for (int i : it->second) {
        const auto& p = map_.polys[i].points;
        double x0, y0, x1, y1;
        box_of(p, x0, y0, x1, y1);
        if (x0 <= x && x <= x1 && y0 <= y && y <= y1 && point_in_polygon(x, y, p)) return true;
    }
    return false;
}

bool World::in_building(double x, double y) const {
    auto it = building_cells_.find(cell_key(static_cast<int>(std::floor(x / kCell)), static_cast<int>(std::floor(y / kCell))));
    if (it == building_cells_.end()) return false;
    for (int i : it->second) {
        const auto& p = map_.buildings[i].points;
        double x0, y0, x1, y1;
        box_of(p, x0, y0, x1, y1);
        if (x0 <= x && x <= x1 && y0 <= y && y <= y1 && point_in_polygon(x, y, p)) return true;
    }
    return false;
}

void World::ground_normal(double x, double y, double& nx, double& ny, double& nz) const {
    const double d = map_.terrain.step * 0.5;
    const float fx = static_cast<float>(x), fy = static_cast<float>(y), fd = static_cast<float>(d);
    const double gx = (ground(fx + fd, fy) - ground(fx - fd, fy)) / (2 * d);
    const double gy = (ground(fx, fy + fd) - ground(fx, fy - fd)) / (2 * d);
    const double n = std::sqrt(gx * gx + gy * gy + 1.0);
    nx = -gx / n;
    ny = -gy / n;
    nz = 1.0 / n;
}

}  // namespace amr
