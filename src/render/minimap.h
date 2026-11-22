// The round heading-up minimap (game/minimap.py): roads as light ribbons with dark casings (highways gold, decks
// pale), buildings as blocks, water and parks tinted, traffic as grey dots, police flashing red/blue, job targets
// that stick to the rim when far away, the player as an arrow pointing up. Drawn into its own small buffer, then
// pasted into the picture as a disc with an ink and white rim.
#pragma once
#include <vector>

#include "render/hud.h"
#include "sim/ai.h"
#include "world/world.h"

namespace amr {

struct MapBlip {
    double x, y;
    std::uint8_t rgb[3];
    int kind;  // 0 traffic, 1 police, 2 job target (sticks to the rim), 3 hub
};

class Minimap {
public:
    static constexpr int kSize = 66;           // pixels across (minimap.py SIZE)
    static constexpr double kRange = 120.0;    // metres from the car to the edge
    explicit Minimap(const World& world);
    ~Minimap();
    bool ok() const { return fbo_ != 0 && prog_ != 0; }
    // Draw the map around the car into the minimap's buffer, then paste it into the currently bound picture
    // (pic_w x pic_h) with its top-left corner at (x, y). Leaves the picture bound again.
    void draw(const Car& car, const std::vector<MapBlip>& blips, double time, int x, int y, int pic_w, int pic_h,
              unsigned picture_fbo);

private:
    const World& world_;
    Hud layer_;
    unsigned fbo_ = 0, tex_ = 0, prog_ = 0, vao_ = 0;
};

}  // namespace amr
