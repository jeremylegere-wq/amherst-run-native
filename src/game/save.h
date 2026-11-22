// The player's progress between games: cash, the walls they have tagged, and their weather and quality choices.
// A small text file ("key value" lines) in the system's place for app data (SDL_GetPrefPath: ~/.local/share on
// Linux, the app's own storage on Android and Xbox). Tags are kept by where they are (metres), so a re-baked map
// that moves the spots around keeps the ones still there. Test runs (--frames) never read or write it.
#pragma once
#include <string>
#include <utility>
#include <vector>

namespace amr {

struct SaveData {
    int cash = 0;
    std::vector<std::pair<double, double>> tags;  // tagged spots' positions
    int weather = 0;
    bool low_quality = false;
    bool music = true;
};

// The save file's path ("" when the system has no place for app data).
std::string save_path();
// Read a save; false (and `out` left as it was) when there is none or it cannot be read.
bool load_save(const std::string& path, SaveData& out);
// Write it (through a temporary file, so a crash never leaves half a save); false on failure.
bool write_save(const std::string& path, const SaveData& data);

}  // namespace amr
