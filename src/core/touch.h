// On-screen touch controls for phones and tablets (the Python game's game/touch.py): buttons laid out in the 320x180
// picture, recomputed every frame from where the fingers are, so a thumb can slide from one button to the next.
//
//   left thumb: steer left / right        right thumb: GAS (tall), BRAKE, HAND (handbrake), HOP (hop + power slide)
//   top left:   PAUSE                      in the menus a tap picks the item under it (App reads taps())
//
// They show once a finger has touched the screen (or with --touch) and hide again when a pad or key is used.
// Pure logic, no drawing: App draws them from buttons() and pressed().
#pragma once
#include <SDL.h>

#include <map>
#include <string>
#include <vector>

namespace amr {

struct TouchButton {
    const char* name;   // "left", "right", "gas", "brake", "hand", "hop", "pause"
    const char* label;
    int x, y, w, h;     // in the 320x180 picture
    bool held;          // held to drive (false: a one-shot tap)
};

class Touch {
public:
    Touch();
    // Feed every SDL event. window_w/h: the window's size in pixels (finger positions arrive as 0..1 of it).
    // Mouse presses act as one finger only when `mouse` is on (--touch on a desktop, for trying it out).
    void handle(const SDL_Event& e, int window_w, int window_h);
    void set_mouse(bool on) { mouse_ = on; if (on) shown_ = true; }
    void hide() { shown_ = false; }      // a pad or key was used
    bool shown() const { return shown_; }

    // Work out what is pressed now; returns the one-shot buttons just tapped ("pause").
    std::vector<std::string> update();
    bool pressed(const std::string& name) const;
    const std::vector<TouchButton>& buttons() const { return buttons_; }
    // Where fingers came down since the last call, in picture pixels (menus: tap an item).
    std::vector<std::pair<int, int>> taps();

    // Window pixel -> picture pixel through the same 16:9 letterbox as RenderTarget::present (clamped to the
    // picture, so a thumb on a black bar still reaches the buttons at the edge).
    static void to_picture(float wx, float wy, int window_w, int window_h, float& px, float& py);
    static constexpr int kW = 320, kH = 180;

private:
    std::map<long long, std::pair<float, float>> fingers_;  // finger (or the mouse, -1) -> picture position
    std::vector<TouchButton> buttons_;
    std::vector<std::string> pressed_, prev_;
    std::vector<std::pair<int, int>> taps_;
    bool shown_ = false, mouse_ = false, finger_seen_ = false;
};

}  // namespace amr
