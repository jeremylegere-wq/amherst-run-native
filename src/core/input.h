// Player input from SDL's GameController API (buttons by name: A, B, Back, Start...) and the keyboard.
// Never raw button numbers: they differ between drivers (Batocera's Xbox pads count from 1). This is the C++
// version of the Python game's game/padmap.py rule.
#pragma once
#include <SDL.h>

#include <vector>

namespace amr {

struct Controls {
    float steer = 0;     // -1 left .. +1 right
    float throttle = 0;  // 0 .. 1
    float brake = 0;     // 0 .. 1 (also reverse)
    bool a = false;      // pressed this frame
    bool quit = false;   // Back / Esc / window closed
};

class Input {
public:
    Input();
    ~Input();
    Input(const Input&) = delete;
    Input& operator=(const Input&) = delete;

    // Handle one SDL event (pads plugged in or out, quit requests, button presses).
    void handle(const SDL_Event& e);
    // The controls for this frame; resets the one-frame presses.
    Controls poll();
    int pads() const { return static_cast<int>(pads_.size()); }

private:
    void open(int device_index);
    std::vector<SDL_GameController*> pads_;
    bool a_ = false, quit_ = false;
};

// A stick value with a dead zone, rescaled so it still reaches 1 at full tilt.
float stick(Sint16 raw, float dead = 0.18f);

}  // namespace amr
