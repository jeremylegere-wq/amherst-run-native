// The game's music: an original funky breakbeat made in code as it plays (no music files), like the sound effects.
// 100 beats a minute in E minor, a 16-bar cycle: |Em9|Em9|A13|A13|Cmaj9|D6|Em9|B7| twice, the second time with a
// chiptune hook on top. Drums (kick, snare with ghost notes, hats), a syncopated bass on the chords' roots, and
// electric-piano stabs on the offbeats. When the player is wanted a chase layer comes in: sixteenth hats, a
// shaker, a busier kick. Sound mixes it under the effects; set_on / set_chase are safe to call from the game.
#pragma once
#include <atomic>
#include <cstdint>
#include <vector>

namespace amr {

class Music {
public:
    explicit Music(int rate = 22050);
    float next();                                   // the next sample, about -1..1 (the audio thread calls it)
    void set_on(bool on) { on_.store(on); }
    bool on() const { return on_.load(); }
    void set_chase(bool chase) { chase_.store(chase); }
    static constexpr double kBpm = 100.0;
    static constexpr int kBars = 16;                // the cycle's length in bars

private:
    enum Kind : std::uint8_t { Kick, Snare, Hat, OpenHat, Shaker, Bass, Keys, Lead };
    struct Voice {
        Kind kind;
        double freq, amp;
        long start, len;                            // samples
        double phase = 0, lp = 0;                   // oscillator phase, low-pass filter state
    };
    void trigger(Kind k, double freq, double amp, double steps);
    void step(long n);                              // start what plays on sixteenth-note step n
    float voice(Voice& v, long t);
    int rate_;
    long t_ = 0, next_step_ = 0, step_n_ = 0;
    double step_len_;                               // samples per sixteenth note
    std::vector<Voice> voices_;
    std::uint32_t noise_ = 12345;
    double chase_mix_ = 0;                          // how much of the chase layer (eases in and out)
    std::atomic<bool> on_{true}, chase_{false};
};

}  // namespace amr
