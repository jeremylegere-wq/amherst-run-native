// The road graph the AI drives on: a port of the Python game's game/roadgraph.py queries, on the graph baked into
// the map (the Python game builds it from the roads at load; the baker saves that exact graph, so no geometry code
// is ported). Edges are directed; cars keep to the RIGHT of their direction of travel. Map y grows south, so the
// right of a direction (dx, dy) is (-dy, dx).
#pragma once
#include <cmath>
#include <cstdint>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "world/map_file.h"

namespace amr {

struct Edge {
    int a = 0, b = 0;
    double ax = 0, ay = 0, bx = 0, by = 0;
    double length = 0, dx = 0, dy = 0;
    int type = kSide;
    double width = 8, kmh = 50, cruise = 0, lane = 0;  // cruise: the posted limit in m/s; lane: offset from the centre
    std::uint32_t name = 0;
};

struct LanePoint {
    double x, y, heading;
};

class RoadGraph {
public:
    explicit RoadGraph(const MapData& map);

    std::vector<GraphNode> nodes;
    std::vector<Edge> edges;
    std::vector<std::vector<int>> out;  // node -> outgoing edge ids, in edge order

    LanePoint lane_point(int eid, double s) const;
    // Edge ids whose midpoint is within r (+30 m slack) of a point.
    std::vector<int> edges_near(double x, double y, double r) const;
    // Nearest node within max_r (-1 if none). With a heading, nodes behind count 50 m further (no turning back).
    int nearest_node(double x, double y, double max_r = 80.0, const double* heading = nullptr) const;
    // The edge after eid: favours going straight, avoids U-turns unless at a dead end. -1 at a dead end.
    int choose_next(int eid, std::mt19937& rng) const;
    // The same without the dice: the most likely turn (the largest weight, the first on a tie). Parity tests.
    int choose_best(int eid) const;
    // A* from src to dst (node ids), or empty. Lanes cost 1.3x their length.
    std::vector<int> shortest_path(int src, int dst, int max_expand = 6000) const;

private:
    double turn_weight(const Edge& e, const Edge& n) const;
    std::vector<int> forward_choices(int eid) const;
    std::unordered_map<long long, std::vector<int>> edge_grid_;  // 40 m cell of an edge's midpoint -> edges
    std::unordered_map<long long, std::vector<int>> node_grid_;  // 40 m cell -> nodes
};

}  // namespace amr
