// The music (core/music.h): every bar of the 16-bar cycle has sound, nothing clips, the same notes every time, the
// chase layer is busier, and it is made far faster than it plays (phones). With a path, writes the cycle as a WAV.
//   test_music [out.wav]
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <vector>

#include "core/music.h"

namespace {
int bad = 0;
void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++bad;
}
std::vector<float> render(bool chase, double secs) {
    amr::Music m;
    m.set_chase(chase);
    std::vector<float> out(static_cast<std::size_t>(22050 * secs));
    for (float& s : out) s = m.next();
    return out;
}
double busy(const std::vector<float>& w) {  // how much high-frequency movement: mean absolute sample difference
    double s = 0;
    for (std::size_t i = 1; i < w.size(); ++i) s += std::fabs(w[i] - w[i - 1]);
    return s / w.size();
}
}  // namespace

int main(int argc, char** argv) {
    const double bar = 4 * 60.0 / amr::Music::kBpm, cycle = bar * amr::Music::kBars;
    const auto t0 = std::chrono::steady_clock::now();
    const std::vector<float> a = render(false, cycle);
    const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("%.1f s of music made in %.3f s\n", cycle, took);
    check(took < cycle / 20, "made at least 20 times faster than it plays");
    float peak = 0;
    bool every_bar = true;
    for (int b = 0; b < amr::Music::kBars; ++b) {
        double sq = 0;
        const std::size_t i0 = static_cast<std::size_t>(b * bar * 22050), i1 = static_cast<std::size_t>((b + 1) * bar * 22050);
        for (std::size_t i = i0; i < i1 && i < a.size(); ++i) sq += a[i] * a[i];
        every_bar = every_bar && std::sqrt(sq / (i1 - i0)) > 0.05;
    }
    for (float s : a) peak = std::max(peak, std::fabs(s));
    std::printf("peak %.3f\n", peak);
    check(every_bar, "every bar of the cycle has sound");
    check(peak < 1.0f && peak > 0.3f, "loud enough, never clipping");
    check(render(false, 5.0) == std::vector<float>(a.begin(), a.begin() + 22050 * 5), "the same music every time");
    const std::vector<float> c = render(true, cycle);
    std::printf("busyness: normal %.4f, chase %.4f\n", busy(a), busy(c));
    check(busy(c) > busy(a) * 1.1, "the chase layer is busier");
    amr::Music off;
    off.set_on(false);
    bool silent = true;
    for (int i = 0; i < 22050; ++i) silent = silent && off.next() == 0.0f;
    check(silent, "switched off: silence");
    if (argc > 1) {  // a 16-bit mono WAV of the cycle, then the chase version
        if (FILE* f = std::fopen(argv[1], "wb")) {
            std::vector<std::int16_t> pcm;
            for (float s : a) pcm.push_back(static_cast<std::int16_t>(s * 30000));
            for (float s : c) pcm.push_back(static_cast<std::int16_t>(s * 30000));
            const std::uint32_t data = static_cast<std::uint32_t>(pcm.size() * 2), rate = 22050, byte_rate = rate * 2;
            const std::uint32_t riff = 36 + data, fmt_len = 16;
            const std::uint16_t pcm_fmt = 1, ch = 1, align = 2, bits = 16;
            std::fwrite("RIFF", 1, 4, f); std::fwrite(&riff, 4, 1, f); std::fwrite("WAVEfmt ", 1, 8, f);
            std::fwrite(&fmt_len, 4, 1, f); std::fwrite(&pcm_fmt, 2, 1, f); std::fwrite(&ch, 2, 1, f);
            std::fwrite(&rate, 4, 1, f); std::fwrite(&byte_rate, 4, 1, f); std::fwrite(&align, 2, 1, f);
            std::fwrite(&bits, 2, 1, f); std::fwrite("data", 1, 4, f); std::fwrite(&data, 4, 1, f);
            std::fwrite(pcm.data(), 2, pcm.size(), f);
            std::fclose(f);
            std::printf("wrote %s\n", argv[1]);
        }
    }
    return bad ? 1 : 0;
}
