// The on-screen touch buttons (core/touch.h, the Python game's game/touch.py): a finger on a button presses it,
// sliding moves the press, lifting releases it; a thumb on a phone's black side bar still reaches the edge buttons;
// mouse events made up from touches are not counted twice; a pad or key hides the buttons.
#include <SDL.h>

#include <cstdio>
#include <string>

#include "core/touch.h"

namespace {

int bad = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++bad;
}

SDL_Event finger(Uint32 type, SDL_FingerID id, float x, float y) {
    SDL_Event e{};
    e.type = type;
    e.tfinger.fingerId = id;
    e.tfinger.x = x;
    e.tfinger.y = y;
    return e;
}

// the middle of a button, as a fraction of a 16:9 window
void centre(const amr::Touch& t, const char* name, float& fx, float& fy) {
    for (const auto& b : t.buttons())
        if (std::string(b.name) == name) {
            fx = (b.x + b.w / 2.0f) / amr::Touch::kW;
            fy = (b.y + b.h / 2.0f) / amr::Touch::kH;
        }
}

}  // namespace

int main() {
    amr::Touch t;
    const int W = 1280, H = 720;
    float gx, gy, bx, by;
    centre(t, "gas", gx, gy);
    centre(t, "brake", bx, by);
    t.handle(finger(SDL_FINGERDOWN, 1, gx, gy), W, H);
    t.update();
    check(t.shown() && t.pressed("gas") && !t.pressed("brake"), "a finger on GAS presses it (and shows the buttons)");
    t.handle(finger(SDL_FINGERMOTION, 1, bx, by), W, H);
    t.update();
    check(t.pressed("brake") && !t.pressed("gas"), "sliding the thumb onto BRAKE moves the press");
    float lx, ly;
    centre(t, "left", lx, ly);
    t.handle(finger(SDL_FINGERDOWN, 2, lx, ly), W, H);
    t.update();
    check(t.pressed("brake") && t.pressed("left"), "two thumbs: BRAKE and LEFT together");
    t.handle(finger(SDL_FINGERUP, 1, bx, by), W, H);
    t.handle(finger(SDL_FINGERUP, 2, lx, ly), W, H);
    t.update();
    check(!t.pressed("brake") && !t.pressed("left"), "lifting releases them");

    // a 20:9 phone (2400 x 1080): the picture is 1920 wide with 240-pixel bars; a thumb on the left bar steers left
    float px, py;
    amr::Touch::to_picture(100.0f, 1000.0f, 2400, 1080, px, py);
    check(px == 0.0f && py > 160.0f, "a thumb on the black side bar maps to the picture's edge");
    t.handle(finger(SDL_FINGERDOWN, 3, 100.0f / 2400, 1000.0f / 1080), 2400, 1080);
    t.update();
    check(t.pressed("left"), "... and presses LEFT there");
    t.handle(finger(SDL_FINGERUP, 3, 0, 0), 2400, 1080);

    float qx, qy;
    centre(t, "pause", qx, qy);
    t.taps();
    t.handle(finger(SDL_FINGERDOWN, 4, qx, qy), W, H);
    auto tapped = t.update();
    check(tapped.size() == 1 && tapped[0] == "pause", "tapping PAUSE gives one tap");
    check(t.update().empty(), "... once, not every frame it is held");
    check(t.taps().size() == 1, "the tap's position is kept for the menus");
    t.handle(finger(SDL_FINGERUP, 4, qx, qy), W, H);

    SDL_Event m{};  // a mouse press SDL made up from a touch
    m.type = SDL_MOUSEBUTTONDOWN;
    m.button.which = SDL_TOUCH_MOUSEID;
    m.button.button = SDL_BUTTON_LEFT;
    m.button.x = static_cast<Sint32>(gx * W);
    m.button.y = static_cast<Sint32>(gy * H);
    t.set_mouse(true);
    t.handle(m, W, H);
    t.update();
    check(!t.pressed("gas"), "mouse events made up from touches are not counted");

    t.handle(finger(SDL_FINGERDOWN, 5, gx, gy), W, H);
    t.hide();
    t.update();
    check(!t.shown() && !t.pressed("gas"), "hidden (a pad is in use): fingers press nothing");
    t.handle(finger(SDL_FINGERMOTION, 5, gx, gy), W, H);
    t.update();
    check(t.shown() && t.pressed("gas"), "a finger brings them back");
    return bad ? 1 : 0;
}
