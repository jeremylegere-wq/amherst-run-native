// Controller support (core/input.h, core/rumble.h): crash rumble grows with the hit and stays in range; prompts
// follow the device used last (a pad press, a key, a finger); a small stick wobble does not count as using the pad.
#include <SDL.h>

#include <cstdio>

#include "core/input.h"
#include "core/rumble.h"

namespace {
int bad = 0;
void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++bad;
}
SDL_Event ev(Uint32 type) {
    SDL_Event e{};
    e.type = type;
    return e;
}
}  // namespace

int main() {
    using amr::crash_rumble;
    check(!crash_rumble(1.0).any(), "a scrape (1 m/s lost) does not shake the pad");
    const amr::Rumble small = crash_rumble(3.0), big = crash_rumble(12.0), huge = crash_rumble(40.0);
    check(small.any() && big.low > small.low && big.ms > small.ms, "a harder crash shakes harder and longer");
    check(huge.low <= 1.0f && huge.high <= 1.0f && huge.ms <= 400, "a huge crash stays within the motors' range");

    amr::Input in;
    check(in.last_device() == amr::Device::Keyboard, "before any input: the keyboard");
    SDL_Event e = ev(SDL_CONTROLLERBUTTONDOWN);
    e.cbutton.button = SDL_CONTROLLER_BUTTON_A;
    in.handle(e);
    check(in.last_device() == amr::Device::Pad, "a pad button: the pad");
    e = ev(SDL_FINGERDOWN);
    e.tfinger.x = 0.5f;
    e.tfinger.y = 0.5f;
    in.handle(e);
    check(in.last_device() == amr::Device::Touch, "a finger: touch");
    e = ev(SDL_CONTROLLERAXISMOTION);
    e.caxis.axis = SDL_CONTROLLER_AXIS_LEFTX;
    e.caxis.value = 3000;  // a resting stick's drift
    in.handle(e);
    check(in.last_device() == amr::Device::Touch, "a small stick wobble does not count as using the pad");
    e.caxis.value = 30000;
    in.handle(e);
    check(in.last_device() == amr::Device::Pad, "pushing the stick: the pad");
    e = ev(SDL_KEYDOWN);
    e.key.keysym.sym = SDLK_e;
    in.handle(e);
    check(in.last_device() == amr::Device::Keyboard, "a key: the keyboard");
    e.key.keysym.sym = SDLK_AC_BACK;
    in.handle(ev(SDL_FINGERDOWN));
    in.handle(e);
    check(in.last_device() == amr::Device::Touch, "a phone's Back button is not the keyboard");
    in.set_rumble(false);
    in.rumble(0, big);  // no pads and switched off: nothing happens (and nothing crashes)
    check(!in.rumble_on(), "rumble can be switched off");
    return bad ? 1 : 0;
}
