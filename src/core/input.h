// Player input from SDL's GameController API (buttons by name: A, B, Back, Start...) and the keyboard.
// Never raw button numbers: they differ between drivers (Batocera's Xbox pads count from 1). This is the C++
// version of the Python game's game/padmap.py rule.
#pragma once
#include <SDL.h>

#include <vector>

namespace amr {

// The Python game's controls (game/main.py read_input): left stick or D-pad steers, stick/D-pad up is gas and down
// is brake/reverse (the triggers work too), A is the handbrake, LB/RB hop (hold through a turn to power-slide).
// Keyboard: arrows/WASD, Space handbrake, Shift hop, Esc quits.
struct Controls {
    float steer = 0;          // -1 left .. +1 right
    bool throttle = false;
    bool brake = false;       // also reverse
    bool handbrake = false;
    bool bumper = false;      // hop / power slide
    bool a = false;           // A pressed this frame (menus)
    bool quit = false;        // Back / Esc / window closed
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

// A stick value, 0 inside the dead zone (game/main.py _joy_axis).
float stick(Sint16 raw, float dead = 0.18f);

}  // namespace amr
