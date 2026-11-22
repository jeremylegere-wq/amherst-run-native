// The baked map (amherst.amr), made by the Python repository's tools/bake_native.py. That file's docstring is the
// format's specification; this loader reads version 1 and refuses anything else.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace amr {

struct Point2 {
    float x = 0, y = 0;
};

struct Rgb {
    std::uint8_t r = 0, g = 0, b = 0;
};

enum RoadFlag : std::uint8_t { kOneWay = 1, kBridge = 2, kDeck = 4, kTunnel = 8, kTurnaround = 16 };
enum RoadType : std::uint8_t { kLane = 0, kSide = 1, kMain = 2 };
enum Service : std::uint8_t { kNoService = 0, kDriveway = 1, kTrack = 2, kOtherService = 3 };

enum Marking : std::uint8_t { kNoPaint = 0, kDoubleYellow, kDashYellow, kDashWhite, kHighway, kEdges };

struct Road {
    std::uint8_t type = kSide, service = kNoService, flags = 0, lanes = 0;
    std::uint8_t marking = kNoPaint;  // road paint (game/streetdetail.py marking)
    bool town_sidewalk = false;       // a concrete sidewalk (brick downtown); else a narrow shoulder
    bool gravel = false;
    float width = 7, speed_kmh = 50;
    std::uint32_t name = 0;
    std::vector<Point2> points;
};

struct SpanPoint {
    float x = 0, y = 0, h = 0;  // h: deck height above the (graded) ground
};

struct Span {  // an overpass: ramp up, deck, ramp down (game/world.py build_spans + game/grading.py)
    std::uint32_t road = 0;
    float width = 8, lift = 5.2f;
    std::vector<SpanPoint> points;
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // bounding box including half the width (filled by the loader)
};

struct Tri {
    std::uint16_t a = 0, b = 0, c = 0;
};

enum Roof : std::uint8_t { kFlat = 0, kGable = 1, kHip = 2, kCone = 3, kMansard = 4 };

enum HouseKind : std::uint8_t { kNotHouse = 0, kGarage, kMiniHome, kBungalow, kCape, kTwoStorey };
enum DetailFlag : std::uint8_t {
    kPorch = 1, kSidelights = 2, kNoFrontWindows = 4, kShutters = 8, kPlaque = 16, kClockTower = 32, kCornice = 64,
    kOwnDoor = 128
};
enum Steeple : std::uint8_t { kNoSteeple = 0, kSpire, kTurret, kTwin };
enum LandmarkKind : std::uint8_t { kNoLandmark = 0, kCourtHouse, kCivic, kChurch, kArmoury, kOtherLandmark };

// What the street style draws on a building (BDET; game/archetypes.py and render3d.py _house / _shop_walls).
// Walls are numbered by the outline: wall k runs from point k to point k + 1.
struct BuildingDetail {
    std::uint8_t house = kNotHouse, storeys = 1, flags = 0, steeple = kNoSteeple, landmark = kNoLandmark, dormers = 0;
    std::int16_t front = -1, wrap = -1;  // the wall facing the street; the side wall a veranda turns onto
    float door = 0.5f;                   // door place along the front wall, 0..1
    Rgb door_rgb, shutters, plaque, plinth;
    float window_h = 1.3f, plinth_h = 0, chimney = -9;  // chimney: place along the ridge -1..1 (-9: none)
    std::int8_t dormer_side = 1;         // which roof slope: + or - the ridge's left normal
    std::uint8_t shop_storeys = 0;       // 0: from the height
    std::vector<std::uint16_t> shops;    // walls with a shop front
};

struct Building {
    float height = 6, roof_height = 0;
    std::uint8_t roof = kFlat, flags = 0;
    Rgb wall, roof_colour;
    std::vector<Point2> points;  // footprint outline
    std::vector<Tri> tris;       // footprint triangles (counter-clockwise)
    BuildingDetail detail;       // (all defaults when the map has no BDET chunk)
};

enum PolyKind : std::uint8_t { kWater = 0, kPark = 1, kLot = 2, kLandUse = 3 };

struct Poly {
    std::uint8_t kind = kLandUse;
    Rgb colour;
    std::vector<Point2> points;
    std::vector<Tri> tris;
};

// A prop: a tree, lamp post, power pole, hydrant, gravestone, fence piece, parked-car block... (game/props.py)
struct Prop {
    std::uint8_t kind = 0;   // index into MapData::prop_kinds
    std::uint8_t c = 0;      // trees: which of the style's canopy colours
    Rgb colour;              // its own colour, if it has one (0 0 0 if not)
    bool segment = false;    // fences: runs from (x, y) to (x2, y2)
    float x = 0, y = 0, h = 0, r = 0, cr = 0, heading = 0, x2 = 0, y2 = 0;
};

// A hand-placed parked car (PARK; data/map_edits.json "cars"): drawn as a cartoon car, solid through PROP car_blocks.
struct ParkedCar {
    float x = 0, y = 0, heading = 0;  // map radians
    std::string model = "sedan";
    Rgb body, roof;
};

// The road graph as baked (game/roadgraph.py RoadGraph): see sim/roadgraph.h for the queries.
struct GraphNode {
    double x = 0, y = 0;
};
struct GraphEdge {
    std::uint32_t a = 0, b = 0;
    std::uint8_t type = kSide;
    float width = 8, kmh = 50;
    std::uint32_t name = 0;
};

// Job hubs, Heritage Tour spots and real civic addresses (game/missions.py, data/addresses.json).
struct HubSpot {
    int role = 0;  // 0 deliveries, 1 getaway, 2 tour, 3 courthouse
    double x = 0, y = 0;
    std::string label;
};
struct TourSpot {
    double x = 0, y = 0;
    std::string label;
};
struct CivicAddress {
    float x = 0, y = 0;
    std::string number;
    std::uint32_t street = 0;  // index into names
};

struct TerrainGrid {
    float x0 = 0, y0 = 0, step = 12.5f;
    int nx = 0, ny = 0;
    std::vector<float> h;  // rows south to north, metres (already exaggerated like the Python game)
    float node(int i, int j) const;
    float height(float x, float y) const;  // bilinear, clamped at the edges (terrain.py Terrain.height)
    // The height of the drawn ground mesh (each cell is two flat triangles split corner (i,j) to (i+1,j+1)), so
    // things laid on the ground (roads, areas) sit exactly on it even on steep slopes.
    float mesh_height(float x, float y) const;
};

struct MapData {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // bounds
    float spawn_x = 0, spawn_y = 0, spawn_heading = 0;
    std::string attribution, source;
    std::vector<std::string> names;
    TerrainGrid terrain;
    std::vector<Road> roads;
    std::vector<Span> spans;
    std::vector<Building> buildings;
    std::vector<Poly> polys;
    std::vector<std::string> prop_kinds;
    std::vector<Prop> props;
    std::vector<GraphNode> graph_nodes;
    std::vector<GraphEdge> graph_edges;
    // junction rules (game/junctions.py, classified at bake time): per node, per edge into a junction
    std::vector<std::uint8_t> jn_is;          // node is a junction (3+ roads meet)
    std::vector<double> jn_box, jn_stop_at;   // node: junction box radius; distance from the node to the stop point
    std::vector<std::uint8_t> jn_control;     // edge: 0 priority 1 signal 2 stop 3 yield 4 all-way
    std::vector<std::uint8_t> jn_phase;       // edge: signal phase, 0 east-west 1 north-south
    std::vector<HubSpot> hubs;
    std::vector<TourSpot> tour_spots;
    std::vector<CivicAddress> addresses;
    std::vector<ParkedCar> parked;
    std::vector<long long> downtown_cells;     // 40 m cells (cx << 32 ^ cy) where sidewalks are brick, sorted
    bool downtown(float x, float y) const;
    std::vector<TourSpot> sprint_targets;  // VS race finishes (game/battle.py sprint_targets)
    std::vector<std::vector<Point2>> rails;    // rail ways (drawn as rail beds)
    std::vector<GraphNode> train_line;         // the train's line (game/train.py stitch)
    double train_lo = 0, train_hi = 0;         // the stretch of it a train runs (metres along the line)
    int prop_kind(const std::string& name) const;  // index of a kind, -1 if the map has none of it
};

constexpr std::uint32_t kMapVersion = 1;

// Load from a file (SDL_RWops, so it also works from Android assets). False with a message on any problem.
bool load_map(const std::string& path, MapData& out, std::string& error);
// Parse bytes already in memory (tests).
bool parse_map(const std::uint8_t* data, std::size_t size, MapData& out, std::string& error);

}  // namespace amr
