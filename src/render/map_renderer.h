// Draws the baked map: terrain, land use, parking lots, water, roads, overpass decks and buildings.
// The map is cut into 200 m chunks; each chunk's meshes are built the first time it comes into view and kept.
// A depth buffer does the hiding the Python renderer needed a painter's sort and hill clipping for.
#pragma once
#include <memory>
#include <unordered_map>
#include <vector>

#include "core/math.h"
#include "render/mesh.h"
#include "world/world.h"

namespace amr {

struct Camera {
    Vec3 eye, target;
    float fovy = 0.88f;  // radians: the street style's 80 degree horizontal view at 16:9
};

// A roof polygon (3 or 4 corners) in world space.
struct RoofFace {
    Vec3 v[4];
    int n = 0;
};

// Roof faces (game/builder.py roof_faces): gable, hip and mansard on four-cornered outlines, the ridge along the long
// side (or across it); cone on any outline. h: the wall top; rh: the roof's height. Empty for flat roofs.
std::vector<RoofFace> roof_faces(const std::vector<Point2>& pts, float h, int kind, float rh, bool across);

struct FrameStats {
    int chunks_drawn = 0, chunks_built = 0;
    long long triangles = 0;
};

class MapRenderer {
public:
    explicit MapRenderer(const World& world);
    ~MapRenderer();
    bool ok() const { return program_ != 0; }
    // Draw everything within view of the camera into the current framebuffer (viewport already set).
    FrameStats draw(const Camera& cam, float aspect);
    // Build every chunk within view distance of (x, y) now (at the start, so the first frames do not stutter).
    void warm(float x, float y);
    // Draw the unit box (the placeholder car) with the same lighting and fog; `model` places and sizes it.
    void draw_box(const Mat4& model, const std::uint8_t rgb[3]);
    static constexpr float kChunk = 200.0f;
    static constexpr float kDrawDist = 260.0f;
    static constexpr int kBuildsPerFrame = 2;  // new chunks built a frame, nearest first (no hitches)

private:
    struct Chunk {
        Mesh ground;    // terrain
        Mesh areas;     // land use, lots, water: painted on the ground, under the roads
        Mesh overlay;   // roads (drawn with a depth offset over the ground and the areas)
        Mesh solid;     // buildings and overpass decks
    };
    struct Item {       // something that belongs to a chunk: (kind, index, sub-index)
        int kind, index, sub;
    };
    long long key(int cx, int cy) const;
    Chunk* chunk(int cx, int cy, FrameStats& stats, bool may_build);
    void build(int cx, int cy, Chunk& out);
    void bucket();

    const World& world_;
    unsigned program_ = 0;
    int u_mvp_ = -1, u_model_ = -1, u_eye_ = -1, u_fog_ = -1, u_fog_range_ = -1, u_tint_ = -1;
    Mat4 vp_;
    std::unordered_map<long long, std::unique_ptr<Chunk>> chunks_;
    std::unordered_map<long long, std::vector<Item>> items_;  // what each chunk holds, sorted out at start
    std::vector<float> water_level_;                          // per polygon: the level water lies at
    Mesh box_;                                                // unit box (the placeholder car)
};

}  // namespace amr
