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

struct Road {
    std::uint8_t type = kSide, service = kNoService, flags = 0, lanes = 0;
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

struct Building {
    float height = 6, roof_height = 0;
    std::uint8_t roof = kFlat, flags = 0;
    Rgb wall, roof_colour;
    std::vector<Point2> points;  // footprint outline
    std::vector<Tri> tris;       // footprint triangles (counter-clockwise)
};

enum PolyKind : std::uint8_t { kWater = 0, kPark = 1, kLot = 2, kLandUse = 3 };

struct Poly {
    std::uint8_t kind = kLandUse;
    Rgb colour;
    std::vector<Point2> points;
    std::vector<Tri> tris;
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
};

constexpr std::uint32_t kMapVersion = 1;

// Load from a file (SDL_RWops, so it also works from Android assets). False with a message on any problem.
bool load_map(const std::string& path, MapData& out, std::string& error);
// Parse bytes already in memory (tests).
bool parse_map(const std::uint8_t* data, std::size_t size, MapData& out, std::string& error);

}  // namespace amr
