// The save file (game/save.h): what is written comes back; a missing or foreign file is refused and leaves the data
// alone; a save stopped between writing and renaming still loads.
//   test_save DIR      (a folder it may write in)
#include <cstdio>
#include <string>

#include "game/save.h"

namespace {
int bad = 0;
void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++bad;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    const std::string path = std::string(argv[1]) + "/test_save.txt";
    std::remove(path.c_str());
    amr::SaveData d;
    check(!amr::load_save(path, d), "no save yet: nothing loads");
    d.cash = 375;
    d.weather = 3;
    d.low_quality = true;
    d.tags = {{412.25, -903.5}, {-17.0, 1250.75}};
    check(amr::write_save(path, d), "a save is written");
    amr::SaveData e;
    check(amr::load_save(path, e), "... and read back");
    check(e.cash == 375 && e.weather == 3 && e.low_quality && e.tags.size() == 2 && e.tags[0].first == 412.25 &&
              e.tags[1].second == 1250.75, "with the same cash, weather, quality and tags");
    d.cash = 400;
    check(amr::write_save(path, d) && amr::load_save(path, e) && e.cash == 400, "saving again replaces it");
    std::rename(path.c_str(), (path + ".tmp").c_str());
    amr::SaveData f;
    check(amr::load_save(path, f) && f.cash == 400, "only the temporary file left (stopped mid-save): it loads");
    std::remove((path + ".tmp").c_str());
    if (FILE* o = std::fopen(path.c_str(), "w")) {
        std::fputs("something else entirely\ncash 99999\n", o);
        std::fclose(o);
    }
    amr::SaveData g;
    g.cash = 7;
    check(!amr::load_save(path, g) && g.cash == 7, "a file that is not a save is refused, the data left alone");
    std::remove(path.c_str());
    return bad ? 1 : 0;
}
