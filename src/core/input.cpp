#include "core/input.h"

#include <algorithm>
#include <cmath>

namespace amr {

float stick(Sint16 raw, float dead) {
    float v = std::max(-1.0f, raw / 32767.0f);
    float a = std::fabs(v);
    if (a <= dead) return 0.0f;
    return std::copysign((a - dead) / (1.0f - dead), v);
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
    Controls c;
    const Uint8* k = SDL_GetKeyboardState(nullptr);
    c.steer = static_cast<float>((k[SDL_SCANCODE_RIGHT] || k[SDL_SCANCODE_D]) - (k[SDL_SCANCODE_LEFT] || k[SDL_SCANCODE_A]));
    c.throttle = (k[SDL_SCANCODE_UP] || k[SDL_SCANCODE_W]) ? 1.0f : 0.0f;
    c.brake = (k[SDL_SCANCODE_DOWN] || k[SDL_SCANCODE_S]) ? 1.0f : 0.0f;
    for (auto* p : pads_) {  // any pad drives player 1 for now; the strongest input wins
        float s = stick(SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_LEFTX));
        if (std::fabs(s) > std::fabs(c.steer)) c.steer = s;
        c.throttle = std::max(c.throttle, SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) / 32767.0f);
        c.brake = std::max(c.brake, SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_TRIGGERLEFT) / 32767.0f);
    }
    c.a = a_;
    c.quit = quit_;
    a_ = false;
    return c;
}

}  // namespace amr
