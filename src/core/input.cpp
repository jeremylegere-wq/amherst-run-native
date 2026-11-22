#include "core/input.h"

#include <algorithm>
#include <cmath>

namespace amr {

float stick(Sint16 raw, float dead) {
    const float v = std::max(-1.0f, raw / 32767.0f);
    return std::fabs(v) > dead ? v : 0.0f;  // like game/main.py _joy_axis: the raw value past the dead zone
}

Input::Input() {
    for (int i = 0; i < SDL_NumJoysticks(); ++i) open(i);
}

Input::~Input() {
    for (auto* p : pads_) SDL_GameControllerClose(p);
}

void Input::open(int device_index) {
    if (!SDL_IsGameController(device_index)) return;
    if (auto* p = SDL_GameControllerOpen(device_index)) {
        if (std::find(pads_.begin(), pads_.end(), p) == pads_.end()) pads_.push_back(p);
    }
}

void Input::handle(const SDL_Event& e) {
    switch (e.type) {
        case SDL_QUIT: quit_ = true; break;
        case SDL_CONTROLLERDEVICEADDED: open(e.cdevice.which); break;
        case SDL_CONTROLLERDEVICEREMOVED: {
            SDL_GameController* gone = SDL_GameControllerFromInstanceID(e.cdevice.which);
            pads_.erase(std::remove(pads_.begin(), pads_.end(), gone), pads_.end());
            if (gone) SDL_GameControllerClose(gone);
            break;
        }
        case SDL_CONTROLLERBUTTONDOWN:
            if (e.cbutton.button == SDL_CONTROLLER_BUTTON_A) a_ = true;
            if (e.cbutton.button == SDL_CONTROLLER_BUTTON_BACK) quit_ = true;
            break;
        case SDL_KEYDOWN:
            if (e.key.repeat) break;
            if (e.key.keysym.sym == SDLK_SPACE || e.key.keysym.sym == SDLK_RETURN) a_ = true;
            if (e.key.keysym.sym == SDLK_ESCAPE || e.key.keysym.sym == SDLK_AC_BACK) quit_ = true;
            break;
        default: break;
    }
}

Controls Input::poll() {
    constexpr float kDead = 0.18f;  // game/main.py _DEAD
    Controls c;
    const Uint8* k = SDL_GetKeyboardState(nullptr);
    const float kb_steer = static_cast<float>((k[SDL_SCANCODE_RIGHT] || k[SDL_SCANCODE_D]) -
                                              (k[SDL_SCANCODE_LEFT] || k[SDL_SCANCODE_A]));
    c.throttle = k[SDL_SCANCODE_UP] || k[SDL_SCANCODE_W];
    c.brake = k[SDL_SCANCODE_DOWN] || k[SDL_SCANCODE_S];
    c.handbrake = k[SDL_SCANCODE_SPACE];
    c.bumper = k[SDL_SCANCODE_LSHIFT] || k[SDL_SCANCODE_RSHIFT];
    float pad_steer = 0;
    for (auto* p : pads_) {  // any pad drives player 1 for now
        auto btn = [&](SDL_GameControllerButton b) { return SDL_GameControllerGetButton(p, b) != 0; };
        float ax = stick(SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_LEFTX), kDead);
        float ay = stick(SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_LEFTY), kDead);  // up is negative
        if (btn(SDL_CONTROLLER_BUTTON_DPAD_LEFT)) ax = -1;
        if (btn(SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) ax = 1;
        if (btn(SDL_CONTROLLER_BUTTON_DPAD_UP)) ay = -1;
        if (btn(SDL_CONTROLLER_BUTTON_DPAD_DOWN)) ay = 1;
        if (std::fabs(ax) > std::fabs(pad_steer)) pad_steer = ax;
        c.throttle = c.throttle || ay < 0 || SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 9830;
        c.brake = c.brake || ay > 0 || SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 9830;
        c.handbrake = c.handbrake || btn(SDL_CONTROLLER_BUTTON_A);
        c.bumper = c.bumper || btn(SDL_CONTROLLER_BUTTON_LEFTSHOULDER) || btn(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    }
    c.steer = kb_steer != 0 ? kb_steer : pad_steer;
    c.a = a_;
    c.quit = quit_;
    a_ = false;
    return c;
}

}  // namespace amr
