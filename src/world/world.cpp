#include "world/world.h"

#include <cmath>
#include <limits>

namespace amr {

namespace {
constexpr float kCell = 40.0f;  // game/world.py CELL
long long cell_key(int gx, int gy) { return (static_cast<long long>(gx) << 32) ^ static_cast<unsigned>(gy); }
}  // namespace

World::World(MapData data) : map_(std::move(data)) {
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
