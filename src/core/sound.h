// Small synthesized sound effects (game/sfx.py), made in code at start-up (no sound files): the item roulette tick,
// an item landing, a shot launching, a ricochet, a balloon pop, a spin-out, a boost and a win jingle; and this
// game's own spray-can hiss and tag jingle.
// If audio cannot start (no sound device, a dummy driver in tests), every call quietly does nothing: the game never
// depends on sound working.
#pragma once
#include <SDL.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace amr {

class Sound {
public:
    Sound();
    ~Sound();
    Sound(const Sound&) = delete;
    Sound& operator=(const Sound&) = delete;
    bool ok() const { return dev_ != 0; }
    void play(const std::string& name);
    void pause(bool on) { if (dev_) SDL_PauseAudioDevice(dev_, on ? 1 : 0); }  // the app is in the background
    // The samples of an effect (tests: they are made even without a sound device).
    const std::vector<Sint16>* samples(const std::string& name) const;

private:
    static void callback(void* self, Uint8* stream, int len);
    struct Voice {
        const std::vector<Sint16>* pcm;
        std::size_t pos;
    };
    std::unordered_map<std::string, std::vector<Sint16>> sounds_;
    std::vector<Voice> voices_;
    SDL_AudioDeviceID dev_ = 0;
};

}  // namespace amr
