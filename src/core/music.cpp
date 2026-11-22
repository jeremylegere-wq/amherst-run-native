#include "core/music.h"

#include <algorithm>
#include <cmath>

namespace amr {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kE2 = 82.41;  // the bass's home note

double note(double base, int semitones) { return base * std::pow(2.0, semitones / 12.0); }

// The chords, bar by bar: root (semitones above E) and the voicing (semitones above the root)
struct Chord {
    int root;
    int tones[5];
    int n;
};
const Chord kChords[8] = {
    {0, {0, 3, 7, 10, 14}, 5},   // Em9
    {0, {0, 3, 7, 10, 14}, 5},   // Em9
    {5, {0, 4, 10, 14, 21}, 5},  // A13
    {5, {0, 4, 10, 14, 21}, 5},  // A13
    {8, {0, 4, 7, 11, 14}, 5},   // Cmaj9
    {10, {0, 4, 7, 9, 0}, 4},    // D6
    {0, {0, 3, 7, 10, 14}, 5},   // Em9
    {7, {0, 4, 10, 15, 0}, 4},   // B7#9
};

// The bass line over two bars (step, semitones above the chord's root, length in steps)
struct Note {
    int step, semi;
    double len;
};
const Note kBass[] = {{0, 0, 2}, {3, 0, 1}, {6, 12, 1}, {7, 10, 2}, {10, 7, 2}, {13, 5, 1}, {14, 7, 2},
                      {16, 0, 2}, {19, 3, 1}, {21, 5, 1}, {23, 7, 2}, {26, 10, 1}, {27, 12, 2}, {30, 10, 1}, {31, 7, 1}};
// The hook over two bars (semitones above E4)
const Note kHook[] = {{0, 7, 2}, {2, 10, 2}, {4, 12, 1}, {5, 10, 1}, {6, 7, 2}, {10, 5, 1}, {11, 7, 3},
                      {16, 12, 2}, {18, 14, 2}, {20, 15, 1}, {21, 14, 1}, {22, 12, 2}, {26, 10, 1}, {27, 7, 4}};

}  // namespace

Music::Music(int rate) : rate_(rate), step_len_(rate * 60.0 / kBpm / 4.0) { voices_.reserve(64); }

void Music::trigger(Kind k, double freq, double amp, double steps) {
    if (voices_.size() >= 48) voices_.erase(voices_.begin());  // the oldest makes room
    voices_.push_back({k, freq, amp, t_, static_cast<long>(steps * step_len_), 0, 0});
}

void Music::step(long n) {
    const int s = static_cast<int>(n % 16), bar = static_cast<int>((n / 16) % kBars), bar8 = bar % 8;
    const bool chase = chase_mix_ > 0.5, fill = bar8 == 7 && s >= 12;
    // drums: a funk break (kick on 1, the "a" of 1, the "and" of 2 and 3), snare on 2 and 4 with ghosts
    const bool kick = s == 0 || s == 3 || s == 7 || s == 10 || (chase && (s == 13 || s == 15)) || (bar8 == 3 && s == 14);
    if (kick && !fill) trigger(Kick, 0, 1.0, 2);
    if (s == 4 || s == 12) trigger(Snare, 0, 0.8, 2);
    else if (!fill && (s == 6 || s == 9 || s == 15)) trigger(Snare, 0, 0.18, 1);  // ghost notes
    if (fill) trigger(Snare, 0, 0.3 + 0.15 * (s - 12), 1);                            // a roll into the next bar
    if (s == 14 && !fill) trigger(OpenHat, 0, 0.35, 2);
    else if (s % 2 == 0) trigger(Hat, 0, 0.32, 1);
    else if (chase) trigger(Hat, 0, 0.18, 1);
    if (chase && s % 4 == 2) trigger(Shaker, 0, 0.3, 2);
    // the bass: the two-bar line on this bar's chord root (one octave lower on the B7, it sits better)
    const Chord& c = kChords[bar8];
    const int in_two = s + 16 * (bar % 2);
    for (const Note& b : kBass)
        if (b.step == in_two && !(chase && b.semi == 12)) trigger(Bass, note(kE2, c.root - (c.root > 7 ? 12 : 0) + b.semi), 0.55, b.len);
    if (chase && s % 2 == 1 && in_two % 4 == 1) trigger(Bass, note(kE2, c.root - (c.root > 7 ? 12 : 0)), 0.35, 0.5);
    // electric piano stabs on the offbeats
    if (s == 2 || s == 10 || (s == 7 && bar8 % 2 == 1))
        for (int k = 0; k < c.n; ++k) trigger(Keys, note(kE2 * 2, c.root + c.tones[k]), 0.13, s == 7 ? 1 : 1.5);
    // the hook: the second half of the cycle
    if (bar >= 8 && bar < 16 && bar8 != 7)
        for (const Note& h : kHook)
            if (h.step == in_two) trigger(Lead, note(kE2 * 4, h.semi), 0.2, h.len);
}

float Music::voice(Voice& v, long t) {
    const double age = static_cast<double>(t - v.start) / rate_;
    auto noise = [&] {
        noise_ = noise_ * 1664525u + 1013904223u;
        return (noise_ >> 8) / 8388608.0 - 1.0;
    };
    double out = 0;
    switch (v.kind) {
        case Kick: {  // a sine dropping from 150 Hz to 48 Hz: the thump
            const double f = 48 + 102 * std::exp(-age * 30);
            v.phase += 2 * kPi * f / rate_;
            out = std::sin(v.phase) * std::exp(-age * 9) * 0.9;
            break;
        }
        case Snare: {  // noise and a 185 Hz body
            v.phase += 2 * kPi * 185 / rate_;
            out = (noise() * 0.7 * std::exp(-age * 18) + std::sin(v.phase) * 0.5 * std::exp(-age * 30)) * 0.7;
            break;
        }
        case Hat:
        case OpenHat:
        case Shaker: {  // high noise: the difference of two noise samples (a crude high-pass)
            const double n = noise();
            const double hp = n - v.lp;
            v.lp = n;
            const double decay = v.kind == Hat ? 70 : v.kind == OpenHat ? 9 : 25;
            out = hp * 0.35 * std::exp(-age * decay) * (v.kind == Shaker ? std::min(1.0, age * 60) : 1.0);
            break;
        }
        case Bass: {  // a square with its edges rounded off by a low-pass, plus a sine an octave down
            v.phase += 2 * kPi * v.freq / rate_;
            const double sq = std::sin(v.phase) >= 0 ? 1.0 : -1.0;
            v.lp += (sq - v.lp) * 0.18;
            const double gate = age * rate_ < v.len ? 1.0 : std::exp(-(age * rate_ - v.len) / rate_ * 60);
            out = (v.lp * 0.55 + std::sin(v.phase * 0.5) * 0.45) * std::min(1.0, age * 300) * std::exp(-age * 2.5) * gate;
            break;
        }
        case Keys: {  // a bell-ish electric piano: a sine and its octave, a quick fall
            v.phase += 2 * kPi * v.freq / rate_;
            out = (std::sin(v.phase) + 0.35 * std::sin(2 * v.phase) * std::exp(-age * 12)) * std::exp(-age * 7) *
                  std::min(1.0, age * 500);
            break;
        }
        case Lead: {  // a 25% pulse with a little vibrato once the note has settled
            const double vib = 1 + 0.006 * std::sin(age * 2 * kPi * 5.5) * std::min(1.0, age * 4);
            v.phase += 2 * kPi * v.freq * vib / rate_;
            const double frac = v.phase / (2 * kPi) - std::floor(v.phase / (2 * kPi));
            const double pulse = frac < 0.25 ? 1.0 : -0.33;
            v.lp += (pulse - v.lp) * 0.35;
            const double gate = age * rate_ < v.len ? 1.0 : std::exp(-(age * rate_ - v.len) / rate_ * 40);
            out = v.lp * std::min(1.0, age * 200) * std::exp(-age * 1.5) * gate;
            break;
        }
    }
    return static_cast<float>(out * v.amp);
}

float Music::next() {
    chase_mix_ += ((chase_.load() ? 1.0 : 0.0) - chase_mix_) * (1.0 / (rate_ * 2.0));  // ease in over ~2 s
    while (t_ >= next_step_) {
        step(step_n_++);
        next_step_ = static_cast<long>(step_n_ * step_len_);
    }
    double mix = 0;
    for (Voice& v : voices_) mix += voice(v, t_);
    ++t_;
    if ((t_ & 1023) == 0)  // now and then, drop the voices that have died away
        voices_.erase(std::remove_if(voices_.begin(), voices_.end(),
                                     [&](const Voice& v) { return t_ - v.start > v.len + rate_ * 0.8; }),
                      voices_.end());
    if (!on_.load()) return 0.0f;
    return static_cast<float>(std::tanh(mix * 0.55));  // a soft limit: loud moments squash, never clip
}

}  // namespace amr
