#include "sim/roadgraph.h"

#include <algorithm>
#include <functional>
#include <queue>
#include <tuple>

namespace amr {

namespace {
constexpr double kCell = 40.0;
constexpr double kBehindPenalty = 50.0;
long long key(int gx, int gy) { return (static_cast<long long>(gx) << 32) ^ static_cast<unsigned>(gy); }
int cell(double v) { return static_cast<int>(std::floor(v / kCell)); }
}  // namespace

RoadGraph::RoadGraph(const MapData& map) : nodes(map.graph_nodes) {
    out.resize(nodes.size());
    edges.reserve(map.graph_edges.size());
    for (const GraphEdge& g : map.graph_edges) {
        Edge e;
        e.a = static_cast<int>(g.a);
        e.b = static_cast<int>(g.b);
        e.ax = nodes[e.a].x; e.ay = nodes[e.a].y; e.bx = nodes[e.b].x; e.by = nodes[e.b].y;
        const double dx = e.bx - e.ax, dy = e.by - e.ay;
        e.length = std::hypot(dx, dy);
        e.dx = dx / e.length;
        e.dy = dy / e.length;
        e.type = g.type;
        e.width = g.width;
        e.kmh = g.kmh;
        e.cruise = e.kmh / 3.6;
        e.lane = std::min(e.width * 0.25, 2.6);
        e.name = g.name;
        out[e.a].push_back(static_cast<int>(edges.size()));
        edges.push_back(e);
    }
    for (int i = 0; i < static_cast<int>(edges.size()); ++i) {
        const Edge& e = edges[i];
        edge_grid_[key(cell((e.ax + e.bx) / 2), cell((e.ay + e.by) / 2))].push_back(i);
    }
    for (int n = 0; n < static_cast<int>(nodes.size()); ++n) node_grid_[key(cell(nodes[n].x), cell(nodes[n].y))].push_back(n);
}

LanePoint RoadGraph::lane_point(int eid, double s) const {
    const Edge& e = edges[eid];
    return {e.ax - e.dy * e.lane + e.dx * s, e.ay + e.dx * e.lane + e.dy * s, std::atan2(e.dy, e.dx)};
}

std::vector<int> RoadGraph::edges_near(double x, double y, double r) const {
    const double reach = r + 30.0;
    std::vector<int> found;
    for (int cx = cell(x - reach); cx <= cell(x + reach); ++cx)
        for (int cy = cell(y - reach); cy <= cell(y + reach); ++cy) {
            auto it = edge_grid_.find(key(cx, cy));
            if (it == edge_grid_.end()) continue;
            for (int eid : it->second) {
                const Edge& e = edges[eid];
                const double mx = (e.ax + e.bx) / 2 - x, my = (e.ay + e.by) / 2 - y;
                if (mx * mx + my * my <= reach * reach) found.push_back(eid);
            }
        }
    return found;
}

int RoadGraph::nearest_node(double x, double y, double max_r, const double* heading) const {
    int best = -1;
    double best_score = 1e18, fx = 0, fy = 0;
    if (heading) { fx = std::cos(*heading); fy = std::sin(*heading); }
    const int k = static_cast<int>(std::floor(max_r / kCell)) + 1, cx = cell(x), cy = cell(y);
    for (int gx = cx - k; gx <= cx + k; ++gx)
        for (int gy = cy - k; gy <= cy + k; ++gy) {
            auto it = node_grid_.find(key(gx, gy));
            if (it == node_grid_.end()) continue;
            for (int n : it->second) {
                const double dx = nodes[n].x - x, dy = nodes[n].y - y, d = std::hypot(dx, dy);
                if (d > max_r) continue;
                const double score = d + (heading && dx * fx + dy * fy < 0 ? kBehindPenalty : 0.0);
                if (score < best_score) { best = n; best_score = score; }
            }
        }
    return best;
}

std::vector<int> RoadGraph::forward_choices(int eid) const {
    const Edge& e = edges[eid];
    std::vector<int> fwd;
    for (int c : out[e.b])
        if (edges[c].b != e.a) fwd.push_back(c);
    if (fwd.empty()) fwd = out[e.b];  // a dead end: turn round
    return fwd;
}

double RoadGraph::turn_weight(const Edge& e, const Edge& n) const {
    double w = 0.5 + 1.5 * std::max(0.0, e.dx * n.dx + e.dy * n.dy);
    if (n.type == kLane) w *= 0.4;
    return w;
}

int RoadGraph::choose_next(int eid, std::mt19937& rng) const {
    const std::vector<int> fwd = forward_choices(eid);
    if (fwd.empty()) return -1;
    std::vector<double> w;
    for (int c : fwd) w.push_back(turn_weight(edges[eid], edges[c]));
    std::discrete_distribution<int> pick(w.begin(), w.end());
    return fwd[pick(rng)];
}

int RoadGraph::choose_best(int eid) const {
    int best = -1;
    double best_w = -1.0;
    for (int c : forward_choices(eid)) {
        const double w = turn_weight(edges[eid], edges[c]);
        if (w > best_w) { best = c; best_w = w; }
    }
    return best;
}

std::vector<int> RoadGraph::shortest_path(int src, int dst, int max_expand) const {
    if (src == dst) return {src};
    const double tx = nodes[dst].x, ty = nodes[dst].y;
    auto h = [&](int n) { return std::hypot(nodes[n].x - tx, nodes[n].y - ty); };
    std::unordered_map<int, double> best{{src, 0.0}};
    std::unordered_map<int, int> came;
    using Item = std::tuple<double, double, int>;  // (f, g, node): Python heapq order, ties broken the same way
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> heap;
    heap.emplace(h(src), 0.0, src);
    int expanded = 0;
    while (!heap.empty() && expanded < max_expand) {
        const auto [f, g, n] = heap.top();
        heap.pop();
        (void)f;
        auto bi = best.find(n);
        if (g > (bi == best.end() ? 1e18 : bi->second)) continue;
        if (n == dst) {
            std::vector<int> path{n};
            for (int m = n; came.count(m);) {
                m = came[m];
                path.push_back(m);
            }
            std::reverse(path.begin(), path.end());
            return path;
        }
        ++expanded;
        for (int eid : out[n]) {
            const Edge& e = edges[eid];
            const double ng = g + e.length * (e.type == kLane ? 1.3 : 1.0);
            auto bj = best.find(e.b);
            if (ng < (bj == best.end() ? 1e18 : bj->second)) {
                best[e.b] = ng;
                came[e.b] = n;
                heap.emplace(ng + h(e.b), ng, e.b);
            }
        }
    }
    return {};
}

}  // namespace amr
