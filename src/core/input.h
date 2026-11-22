// Player input from SDL's GameController API (buttons by name: A, B, Back, Start...) and the keyboard.
// Never raw button numbers: they differ between drivers (Batocera's Xbox pads count from 1). This is the C++
// version of the Python game's game/padmap.py rule.
#pragma once
#include <SDL.h>

#include <vector>

namespace amr {

// One player's driving controls (game/main.py read_input, game/twoplayer.py read_pad / read_keys): left stick or
// D-pad steers, up is gas and down brake/reverse (the triggers work too), A the handbrake, LB/RB hop (hold through a
// turn to power-slide).
struct Controls {
    float steer = 0;          // -1 left .. +1 right
    bool throttle = false;
    bool brake = false;       // also reverse
    bool handbrake = false;
    bool bumper = false;      // hop / power slide
    bool fire = false;        // battle: use the item
};

// Presses this frame, for menus and pausing (from the keyboard or any pad).
struct Actions {
    bool up = false, down = false;
    bool confirm = false;     // A / Start / Enter / Space
    bool back = false;        // B / Back / Esc
    bool pause = false;       // Start / Back / Esc / P
    bool quit = false;        // the window was closed
    int respawn = -1;         // split screen: this player asked for a respawn (Y, or R / Backspace)
};

class Input {
public:
    Input();
    ~Input();
    Input(const Input&) = delete;
    Input& operator=(const Input&) = delete;

    void handle(const SDL_Event& e);  // pads plugged in or out, presses
    Actions actions();                // this frame's presses (and clears them)
    // Player i's controls when `players` are playing. One player: the keyboard (arrows or WASD) and every pad.
    // More: player 1 drives with WASD + Space + Left Shift, player 2 with the arrows + Right Ctrl / Keypad 0 + Right
    // Shift, and pads go to the LAST players first (one pad and two players: the pad drives player 2).
    // Battle (game/battle.py): player 1 hops with Space and fires with E / Left Shift, player 2 hops with Right Shift
    // / Enter and fires with / or Right Ctrl; on a pad A or LB/RB hop and X or B fire. No handbrake.
    Controls controls(int i, int players, bool battle = false) const;
    int pads() const { return static_cast<int>(pads_.size()); }
    void set_players(int n) { players_ = n; }  // how many are playing (whose pad's Y respawns whom)

private:
    void open(int device_index);
    int pad_player(SDL_JoystickID id, int players) const;
    std::vector<SDL_GameController*> pads_;
    Actions acts_;
    bool stick_held_ = false;   // menu: the stick has to come back before it moves again
    int players_ = 1;
};

// A stick value, 0 inside the dead zone (game/main.py _joy_axis).
float stick(Sint16 raw, float dead = 0.18f);

}  // namespace amr
