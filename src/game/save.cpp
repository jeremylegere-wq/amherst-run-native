#include "game/save.h"

#include <SDL.h>

#include <cstdio>
#include <sstream>

namespace amr {

std::string save_path() {
    char* dir = SDL_GetPrefPath("AmherstRun", "AmherstRun");
    if (!dir) return "";
    std::string p = std::string(dir) + "save.txt";
    SDL_free(dir);
    return p;
}

bool load_save(const std::string& path, SaveData& out) {
    if (path.empty()) return false;
    SDL_RWops* f = SDL_RWFromFile(path.c_str(), "rb");
    if (!f) f = SDL_RWFromFile((path + ".tmp").c_str(), "rb");  // stopped between writing and renaming
    if (!f) return false;
    std::string text;
    char buf[4096];
    for (size_t n; (n = SDL_RWread(f, buf, 1, sizeof buf)) > 0;) text.append(buf, n);
    SDL_RWclose(f);
    std::istringstream in(text);
    std::string line;
    if (!std::getline(in, line) || line.rfind("amherst-run-save 1", 0) != 0) return false;  // not ours, or newer
    SaveData d;
    while (std::getline(in, line)) {
        std::istringstream l(line);
        std::string key;
        l >> key;
        if (key == "cash") l >> d.cash;
        else if (key == "weather") l >> d.weather;
        else if (key == "quality") { std::string q; l >> q; d.low_quality = q == "low"; }
        else if (key == "tag") {
            double x, y;
            if (l >> x >> y) d.tags.push_back({x, y});
        }
    }
    out = d;
    return true;
}

bool write_save(const std::string& path, const SaveData& d) {
    if (path.empty()) return false;
    std::ostringstream o;
    o << "amherst-run-save 1\n";
    o << "cash " << d.cash << "\n";
    o << "weather " << d.weather << "\n";
    o << "quality " << (d.low_quality ? "low" : "high") << "\n";
    char line[64];
    for (const auto& t : d.tags) {
        std::snprintf(line, sizeof line, "tag %.2f %.2f\n", t.first, t.second);
        o << line;
    }
    const std::string text = o.str(), tmp = path + ".tmp";
    SDL_RWops* f = SDL_RWFromFile(tmp.c_str(), "wb");
    if (!f) return false;
    const bool ok = SDL_RWwrite(f, text.data(), 1, text.size()) == text.size();
    SDL_RWclose(f);
    if (!ok) return false;
    std::remove(path.c_str());  // (rename does not replace a file on Windows)
    return std::rename(tmp.c_str(), path.c_str()) == 0;
}

}  // namespace amr
