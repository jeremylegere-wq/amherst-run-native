// Junction rules for traffic (game/junctions.py): traffic lights, stop signs, giving way and all-way stops.
// Which rule each road into a junction has is decided by the Python game and baked into the map (JNCT); this is
// the per-decision part: how fast a car may go given the junction ahead, the light, and the cars around it.
// Only traffic uses it; police and players ignore it, and traffic gives way to them like to anyone else.
#pragma once
#include <unordered_map>
#include <vector>

#include "sim/ai.h"
#include "sim/roadgraph.h"

namespace amr {

enum Control : int { kPriority = 0, kSignal = 1, kStop = 2, kYield = 3, kAllWay = 4 };

// 0 red, 1 yellow, 2 green for a signal approach of phase ph at game time t (streetdetail.signal_light).
int signal_light(double t, int phase);

class Junctions {
public:
    Junctions(const MapData& map, const RoadGraph& graph);
    bool ok() const { return !is_.empty(); }
    // Highest safe speed for `car` at distance s along edge eid (1e9 = no junction rule applies).
    double limit(AICar& car, int eid, double s, const std::vector<const Car*>& others, double t, double dt);
    // (junction node, metres from the end of eid to it, the edge into it), following plain nodes; node -1 if none.
    struct Ahead {
        int node = -1;
        double extra = 0;
        int into = -1;
    };
    Ahead ahead(int eid);
    int control(int eid) const { return control_[eid]; }
    int light(int eid, double t) const { return signal_light(t, phase_[eid]); }

private:
    void traffic_at(const AICar& car, int n, double ax, double ay, const std::vector<const Car*>& others, bool& busy,
                    double& first) const;
    bool oncoming(const AICar& car, int n, double ax, double ay, const std::vector<const Car*>& others) const;
    bool exit_blocked(const AICar& car, int n, int ie, const std::vector<const Car*>& others) const;
    bool turns_left(const AICar& car, int ie) const;

    const RoadGraph& g_;
    std::vector<std::uint8_t> is_, control_, phase_;
    std::vector<double> box_, stop_at_;
    std::unordered_map<int, Ahead> ahead_;  // cache per edge
};

}  // namespace amr
