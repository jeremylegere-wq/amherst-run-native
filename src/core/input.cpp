#include "core/input.h"

#include "core/log.h"

#include <algorithm>
#include <cmath>

namespace amr {

namespace {
constexpr float kDead = 0.18f;  // game/main.py _DEAD
}

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
        if (std::find(pads_.begin(), pads_.end(), p) == pads_.end()) {
            pads_.push_back(p);
            const char* name = SDL_GameControllerName(p);
            log_info("pad %zu: %s", pads_.size(), name ? name : "(no name)");
        }
    }
}

int Input::pad_player(SDL_JoystickID id, int players) const {
    const int k = std::min(static_cast<int>(pads_.size()), players), first = players - k;
    for (int i = 0; i < k; ++i)
        if (SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pads_[i])) == id) return first + i;
    return -1;
}

void Input::handle(const SDL_Event& e) {
    touch_.handle(e, win_w_, win_h_);
    if (e.type == SDL_CONTROLLERBUTTONDOWN || e.type == SDL_KEYDOWN) touch_.hide();  // a pad or keys: no buttons
    switch (e.type) {
        case SDL_QUIT: acts_.quit = true; break;
        case SDL_APP_WILLENTERBACKGROUND: acts_.background = true; break;
        case SDL_APP_DIDENTERFOREGROUND: acts_.foreground = true; break;
        case SDL_CONTROLLERDEVICEADDED: open(e.cdevice.which); break;
        case SDL_CONTROLLERDEVICEREMOVED: {
            SDL_GameController* gone = SDL_GameControllerFromInstanceID(e.cdevice.which);
            pads_.erase(std::remove(pads_.begin(), pads_.end(), gone), pads_.end());
            if (gone) SDL_GameControllerClose(gone);
            break;
        }
        case SDL_CONTROLLERBUTTONDOWN:
            switch (e.cbutton.button) {
                case SDL_CONTROLLER_BUTTON_A: acts_.confirm = true; break;
                case SDL_CONTROLLER_BUTTON_START: acts_.confirm = acts_.pause = true; break;
                case SDL_CONTROLLER_BUTTON_B: acts_.back = true; break;
                case SDL_CONTROLLER_BUTTON_BACK: acts_.back = acts_.pause = true; break;
                case SDL_CONTROLLER_BUTTON_DPAD_UP: acts_.up = true; break;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN: acts_.down = true; break;
                case SDL_CONTROLLER_BUTTON_Y: acts_.respawn = pad_player(e.cbutton.which, players_); break;
                default: break;
            }
            break;
        case SDL_CONTROLLERAXISMOTION:
            if (e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {  // the stick moves the menu once per push
                const float v = e.caxis.value / 32767.0f;
                if (std::fabs(v) < 0.4f) stick_held_ = false;
                else if (!stick_held_ && std::fabs(v) > 0.6f) {
                    stick_held_ = true;
                    (v < 0 ? acts_.up : acts_.down) = true;
                }
            }
            break;
        case SDL_KEYDOWN:
            if (e.key.repeat) break;
            switch (e.key.keysym.sym) {
                case SDLK_UP: case SDLK_w: acts_.up = true; break;
                case SDLK_DOWN: case SDLK_s: acts_.down = true; break;
                case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE: acts_.confirm = true; break;
                case SDLK_ESCAPE: case SDLK_AC_BACK: acts_.back = acts_.pause = true; break;
                case SDLK_p: acts_.pause = true; break;
                case SDLK_F4: acts_.weather = true; break;
                case SDLK_r: acts_.respawn = 0; break;
                case SDLK_BACKSPACE: acts_.respawn = 1; break;
                default: break;
            }
            break;
        default: break;
    }
}

Actions Input::actions() {
    for (const std::string& tap : touch_.update())
        if (tap == "pause") acts_.pause = true;
    const Actions a = acts_;
    acts_ = Actions{};
    return a;
}

Controls Input::controls(int i, int players, bool battle) const {
    Controls c;
    const Uint8* k = SDL_GetKeyboardState(nullptr);
    if (battle && i == 0) {
        c.steer = static_cast<float>(k[SDL_SCANCODE_D] - k[SDL_SCANCODE_A]);
        c.throttle = k[SDL_SCANCODE_W];
        c.brake = k[SDL_SCANCODE_S];
        c.bumper = k[SDL_SCANCODE_SPACE];
        c.fire = k[SDL_SCANCODE_E] || k[SDL_SCANCODE_LSHIFT];
    } else if (battle && i == 1) {
        c.steer = static_cast<float>(k[SDL_SCANCODE_RIGHT] - k[SDL_SCANCODE_LEFT]);
        c.throttle = k[SDL_SCANCODE_UP];
        c.brake = k[SDL_SCANCODE_DOWN];
        c.bumper = k[SDL_SCANCODE_RSHIFT] || k[SDL_SCANCODE_RETURN] || k[SDL_SCANCODE_KP_ENTER];
        c.fire = k[SDL_SCANCODE_SLASH] || k[SDL_SCANCODE_RCTRL];
    } else if (battle) {
    } else if (players <= 1) {  // one player: either key set
        c.steer = static_cast<float>((k[SDL_SCANCODE_RIGHT] || k[SDL_SCANCODE_D]) - (k[SDL_SCANCODE_LEFT] || k[SDL_SCANCODE_A]));
        c.throttle = k[SDL_SCANCODE_UP] || k[SDL_SCANCODE_W];
        c.brake = k[SDL_SCANCODE_DOWN] || k[SDL_SCANCODE_S];
        c.handbrake = k[SDL_SCANCODE_SPACE];
        c.bumper = k[SDL_SCANCODE_LSHIFT] || k[SDL_SCANCODE_RSHIFT];
        c.tag = k[SDL_SCANCODE_E];
    } else if (i == 0) {
        c.steer = static_cast<float>(k[SDL_SCANCODE_D] - k[SDL_SCANCODE_A]);
        c.throttle = k[SDL_SCANCODE_W];
        c.brake = k[SDL_SCANCODE_S];
        c.handbrake = k[SDL_SCANCODE_SPACE];
        c.bumper = k[SDL_SCANCODE_LSHIFT];
    } else if (i == 1) {
        c.steer = static_cast<float>(k[SDL_SCANCODE_RIGHT] - k[SDL_SCANCODE_LEFT]);
        c.throttle = k[SDL_SCANCODE_UP];
        c.brake = k[SDL_SCANCODE_DOWN];
        c.handbrake = k[SDL_SCANCODE_RCTRL] || k[SDL_SCANCODE_KP_0];
        c.bumper = k[SDL_SCANCODE_RSHIFT];
    }
    if (players <= 1 && !battle && touch_.shown()) {  // the on-screen buttons drive the one player
        if (c.steer == 0.0f) c.steer = static_cast<float>(touch_.pressed("right") - touch_.pressed("left"));
        c.throttle = c.throttle || touch_.pressed("gas");
        c.brake = c.brake || touch_.pressed("brake");
        c.handbrake = c.handbrake || touch_.pressed("hand");
        c.bumper = c.bumper || touch_.pressed("hop");
        c.tag = c.tag || touch_.pressed("tag");
    }
    const int npads = static_cast<int>(pads_.size());
    const int kpads = std::min(npads, std::max(players, 1)), first = std::max(players, 1) - kpads;
    for (int p = 0; p < npads; ++p) {
        if (players > 1 && p != i - first) continue;  // split screen: this player's own pad only
        SDL_GameController* pad = pads_[p];
        auto btn = [&](SDL_GameControllerButton b) { return SDL_GameControllerGetButton(pad, b) != 0; };
        float ax = stick(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX), kDead);
        float ay = stick(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY), kDead);  // up is negative
        if (btn(SDL_CONTROLLER_BUTTON_DPAD_LEFT)) ax = -1;
        if (btn(SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) ax = 1;
        if (btn(SDL_CONTROLLER_BUTTON_DPAD_UP)) ay = -1;
        if (btn(SDL_CONTROLLER_BUTTON_DPAD_DOWN)) ay = 1;
        if (c.steer == 0.0f) c.steer = ax;
        c.throttle = c.throttle || ay < 0 || SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 9830;
        c.brake = c.brake || ay > 0 || SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 9830;
        if (battle) {
            c.bumper = c.bumper || btn(SDL_CONTROLLER_BUTTON_LEFTSHOULDER) || btn(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) ||
                       btn(SDL_CONTROLLER_BUTTON_A);
            c.fire = c.fire || btn(SDL_CONTROLLER_BUTTON_X) || btn(SDL_CONTROLLER_BUTTON_B);
            continue;
        }
        c.handbrake = c.handbrake || btn(SDL_CONTROLLER_BUTTON_A);
        if (players <= 1) c.tag = c.tag || btn(SDL_CONTROLLER_BUTTON_X);
        c.bumper = c.bumper || btn(SDL_CONTROLLER_BUTTON_LEFTSHOULDER) || btn(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    }
    return c;
}

}  // namespace amr
