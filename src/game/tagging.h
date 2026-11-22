// Tagging: walls around town that the player sprays with their crew's tag. Pull up beside a marked wall (the wall
// on that side, under 60 km/h) and hold TAG for about a second. This game's own design (not in the Python game).
//
//   blank walls        pay $25
//   the rival crew's   about a third of the walls start with their tag on; spraying over it pays $50
//   big pieces         a few long walls take three sprays (the piece grows with each) and pay $100
//   the rivals strike  every 2.5 minutes (once you have 3 walls) they go over one of yours, away from you
// Spraying with police nearby raises the wanted level (App does that).
//
// The walls are worked out from the map when the game starts: long walls of commercial and downtown buildings (not
// homes or landmarks) that face a street close by, spread at least 70 m apart, always the same ones.
#pragma once
#include <vector>

#include "render/mesh.h"
#include "sim/car.h"
#include "world/world.h"

namespace amr {

enum class Crew { None, Player, Rival };

struct TagSpot {
    double x = 0, y = 0;    // the middle of the tag, on the wall's face
    double nx = 0, ny = 0;  // the wall's outward normal
    double width = 5;       // how wide the tag is
    double base = 0;        // the height the wall's heights are measured from (the building's highest corner)
    double wall_h = 6;      // the wall's height
    int building = -1;
    bool piece = false;     // a big piece: three sprays
    Crew owner = Crew::None;
    int passes = 0;         // sprays done on a piece not finished yet
};

struct TagEvent {
    enum Kind { None, Pass, Done, RivalStrike } kind = None;
    int spot = -1;
    Crew was = Crew::None;  // Done: who had the wall before
    int pay = 0;            // Done: the reward
};

class Tagging {
public:
    static constexpr double kReach = 11.0;            // metres out from the wall the car can spray from
    static constexpr double kMaxSpeed = 60.0 / 3.6;   // faster than this, no tagging
    static constexpr double kSprayTime = 0.9;         // seconds of holding TAG (one pass of a piece)
    static constexpr int kPieceSprays = 3;
    static constexpr double kRivalEvery = 150.0;      // seconds between the rivals' strikes
    static constexpr int kPay = 25, kCoverPay = 50, kPiecePay = 100;

    explicit Tagging(const World& world, int count = 40, int pieces = 5);
    // One step. `holding`: the TAG button. Reports a pass of a piece, a finished wall, or a rival strike.
    TagEvent update(double dt, const Car& car, bool holding);
    int in_reach() const { return reach_; }         // the wall the car could spray now (-1: none)
    double progress() const { return progress_ / kSprayTime; }  // 0..1 while spraying
    const std::vector<TagSpot>& spots() const { return spots_; }
    int mine() const;                                // walls with the player's tag
    int rivals() const;
    // A saved game: the wall within 2 m of (x, y) belongs to `crew` (walls not mentioned are blank). Its index, or
    // -1 (the map moved on). clear_owners() first.
    int set_owner(double x, double y, Crew crew);
    void clear_owners();

private:
    int find_reach(const Car& car) const;
    std::vector<TagSpot> spots_;
    int reach_ = -1;
    double progress_ = 0, rival_t_ = 0;
    unsigned strike_n_ = 0;
};

// The tag on spot s, as a mesh in world space: an ink-edged back plate and bubble letters with highlights, in the
// crew's colours. `seed` picks the shapes (the same seed, the same tag); a piece shows `fraction` of its letters.
MeshBuilder tag_mesh(const TagSpot& s, unsigned seed, Crew crew, double fraction = 1.0);

}  // namespace amr
