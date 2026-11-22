// The synthesized sound effects (game/sfx.py): each is made, as long as the Python game's, loud enough to hear and
// never clipped. Runs without a sound device (SDL's dummy driver): the effects are made either way.
#include <SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "core/sound.h"

int main() {
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 0);
    SDL_Init(0);
    int bad = 0;
    {
        amr::Sound sound;
        struct Want {
            const char* name;
            double secs;
        };
        const Want want[] = {{"tick", 0.03}, {"item", 0.19}, {"launch", 0.22}, {"ricochet", 0.09},
                             {"pop", 0.12},  {"spin", 0.6},  {"boost", 0.5},   {"win", 0.56}};
        for (const Want& w : want) {
            const auto* pcm = sound.samples(w.name);
            int peak = 0;
            if (pcm)
                for (Sint16 v : *pcm) peak = std::max(peak, std::abs(static_cast<int>(v)));
            const bool ok = pcm && std::abs(static_cast<double>(pcm->size()) / 22050 - w.secs) < 0.002 && peak > 2000 &&
                            peak <= 32000;
            std::printf("  %s %-9s %5zu samples, peak %d\n", ok ? "ok  " : "FAIL", w.name, pcm ? pcm->size() : 0, peak);
            if (!ok) ++bad;
        }
        sound.play("win");  // with or without a device: no crash
        sound.play("nothing");
        SDL_Delay(50);
        std::printf("  audio device: %s\n", sound.ok() ? "open" : "none (effects are silent)");
    }
    SDL_Quit();
    return bad ? 1 : 0;
}
