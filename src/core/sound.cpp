#include "core/sound.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace amr {

namespace {
constexpr int kRate = 22050;
constexpr double kPi = 3.14159265358979323846;

enum Wave { Square, Saw, Sine };

// game/sfx.py _tone: a wave with a linear pitch sweep, optional noise, a quick attack and an exponential fall.
std::vector<double> tone(double freq, double secs, Wave kind = Square, double vol = 0.3, double sweep = 0.0,
                         double noise = 0.0, double decay = 6.0) {
    const int n = static_cast<int>(kRate * secs);
    std::vector<double> out(n);
    double phase = 0.0;
    std::uint32_t seed = 7;  // the same noise every time
    for (int i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / kRate;
        phase += 2 * kPi * (freq + sweep * t / std::max(secs, 1e-6)) / kRate;
        double w = kind == Square ? (std::sin(phase) >= 0 ? 1.0 : -1.0)
                 : kind == Saw    ? 2.0 * std::fmod(phase / (2 * kPi), 1.0) - 1.0
                                  : std::sin(phase);
        if (noise > 0) {
            seed = seed * 1664525u + 1013904223u;
            w = w * (1.0 - noise) + (seed / 4294967296.0 * 2.0 - 1.0) * noise;
        }
        out[i] = w * std::exp(-decay * t) * std::min(1.0, t * 400.0) * vol;
    }
    return out;
}

std::vector<Sint16> pcm(const std::vector<double>& wave) {
    std::vector<Sint16> out(wave.size());
    for (std::size_t i = 0; i < wave.size(); ++i)
        out[i] = static_cast<Sint16>(std::max(-1.0, std::min(1.0, wave[i])) * 32000);
    return out;
}

std::vector<double> join(std::initializer_list<std::vector<double>> parts) {
    std::vector<double> out;
    for (const auto& p : parts) out.insert(out.end(), p.begin(), p.end());
    return out;
}
}  // namespace

Sound::Sound() {
    sounds_["tick"] = pcm(tone(1400, 0.03, Square, 0.18, 0, 0, 40));
    sounds_["item"] = pcm(join({tone(880, 0.07, Square, 0.2), tone(1320, 0.12, Square, 0.2)}));
    sounds_["launch"] = pcm(tone(300, 0.22, Saw, 0.25, 500, 0.3, 10));
    sounds_["ricochet"] = pcm(tone(2200, 0.09, Sine, 0.22, -900, 0, 25));
    sounds_["pop"] = pcm(tone(180, 0.12, Square, 0.35, 0, 0.7, 30));
    sounds_["spin"] = pcm(tone(700, 0.6, Square, 0.18, -500, 0, 3));
    sounds_["spray"] = pcm(tone(5200, 0.3, Sine, 0.16, -800, 0.97, 2));   // a can's hiss
    sounds_["tagged"] = pcm(join({tone(784, 0.08, Square, 0.2, 0, 0, 6), tone(1175, 0.16, Square, 0.2, 0, 0, 5)}));
    sounds_["boost"] = pcm(tone(200, 0.5, Saw, 0.22, 600, 0, 3));
    sounds_["win"] = pcm(join({tone(523, 0.14, Square, 0.22, 0, 0, 4), tone(659, 0.14, Square, 0.22, 0, 0, 4),
                               tone(784, 0.14, Square, 0.22, 0, 0, 4), tone(1047, 0.14, Square, 0.22, 0, 0, 4)}));
    if (!SDL_WasInit(SDL_INIT_AUDIO) && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return;
    SDL_AudioSpec want{}, have{};
    want.freq = kRate;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 512;
    want.callback = &Sound::callback;
    want.userdata = this;
    dev_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (dev_) SDL_PauseAudioDevice(dev_, 0);
}

Sound::~Sound() {
    if (dev_) SDL_CloseAudioDevice(dev_);
}

const std::vector<Sint16>* Sound::samples(const std::string& name) const {
    auto it = sounds_.find(name);
    return it == sounds_.end() ? nullptr : &it->second;
}

void Sound::play(const std::string& name) {
    if (!dev_) return;
    auto it = sounds_.find(name);
    if (it == sounds_.end()) return;
    SDL_LockAudioDevice(dev_);
    if (voices_.size() < 16) voices_.push_back({&it->second, 0});
    SDL_UnlockAudioDevice(dev_);
}

// Mix every playing effect into the device's buffer (runs on SDL's audio thread, under its lock).
void Sound::callback(void* self, Uint8* stream, int len) {
    auto* s = static_cast<Sound*>(self);
    auto* out = reinterpret_cast<Sint16*>(stream);
    const int n = len / 2;
    for (int i = 0; i < n; ++i) {
        int mix = static_cast<int>(s->music_.next() * 12500.0f);  // the music, under the effects
        for (Voice& v : s->voices_)
            if (v.pos < v.pcm->size()) mix += (*v.pcm)[v.pos++];
        out[i] = static_cast<Sint16>(std::max(-32768, std::min(32767, mix)));
    }
    s->voices_.erase(std::remove_if(s->voices_.begin(), s->voices_.end(),
                                    [](const Voice& v) { return v.pos >= v.pcm->size(); }),
                     s->voices_.end());
}

}  // namespace amr
