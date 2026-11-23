// Controller rumble: how hard and how long each thing shakes the pad (low: the big slow motor, high: the small fast
// one, both 0..1). Pure numbers, so they can be tested; Input::rumble sends them to the player's pad.
#pragma once
#include <algorithm>

namespace amr {

struct Rumble {
    float low = 0, high = 0;
    int ms = 0;
    bool any() const { return ms > 0 && (low > 0 || high > 0); }
};

// Hitting something: `impact` is the speed lost in the hit (m/s). Scrapes under 1.5 m/s do nothing; a 15 m/s crash
// is a full shake.
inline Rumble crash_rumble(double impact) {
    if (impact < 1.5) return {};
    const float k = static_cast<float>(std::min(1.0, (impact - 1.5) / 13.5));
    return {0.25f + 0.75f * k, 0.15f + 0.5f * k, static_cast<int>(120 + 260 * k)};
}

namespace rumble {
inline constexpr Rumble kBoost{0.55f, 0.2f, 320};      // a drift boost firing
inline Rumble charge(int level) {                       // the drift charge going up a colour
    return {0.0f, 0.25f + 0.15f * static_cast<float>(level), 90};
}
inline constexpr Rumble kSpray{0.0f, 0.18f, 110};       // the can while spraying (sent again and again)
inline constexpr Rumble kTagged{0.6f, 0.6f, 260};       // a wall finished
inline constexpr Rumble kBusted{0.8f, 0.3f, 700};       // BUSTED or WRECKED
inline constexpr Rumble kTrain{1.0f, 0.7f, 600};        // hit by the train
}  // namespace rumble

}  // namespace amr
