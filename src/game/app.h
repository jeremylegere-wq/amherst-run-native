// The game around the simulation: the start screen, the one-player game (jobs, police, traffic), split screen for
// 2-4 players (free roam, no police, no damage: game/twoplayer.py) and the pause screen. Fixed 60 Hz physics, one
// 320x180 picture a frame (split into views for split screen) with the HUD drawn over it.
#pragma once
#include <SDL.h>

#include <array>
#include <memory>
#include <unordered_map>
#include <string>
#include <vector>

#include "core/input.h"
#include "core/sound.h"
#include "game/battle.h"
#include "game/missions.h"
#include "game/save.h"
#include "game/tagging.h"
#include "render/car_mesh.h"
#include "render/chase_camera.h"
#include "render/building_detail.h"
#include "render/hud.h"
#include "render/map_renderer.h"
#include "render/minimap.h"
#include "game/street.h"
#include "render/target.h"
#include "sim/junctions.h"
#include "sim/roadgraph.h"
#include "sim/town.h"
#include "sim/train.h"
#include "world/world.h"

namespace amr {

struct Options {
    int frames = -1;            // test runs: drive itself for this many frames, then stop
    std::string screenshot;     // ... and save the last picture here
    int players = 0;            // 0: show the start screen (1-4 skip it)
    int battle = 0;             // skip the start screen into a battle: 1 balloon battle, 2 VS race
    bool menu = false;          // show the start screen even in a test run
    bool at = false, has_heading = false;
    double at_x = 0, at_y = 0, heading = 0;
    int traffic = 16;
    bool cops = true;
    double heat = 0;
    bool trains = true;
    double train_in = -1;       // tests: the first train comes after this many seconds
    int weather = 0;            // render/weather.h kWeathers (F4 cycles them)
    bool touch = false;         // show the on-screen touch buttons from the start (the mouse works them on a desktop)
    bool low_quality = false;   // phones: a shorter draw distance, no see-through shadows (the pause menu switches)
    bool tag_test = false;      // test runs: park and hold TAG (the screenshots of a tag being sprayed)
    bool weather_set = false, quality_set = false;  // given on the command line: they win over the saved choice
    bool no_music = false;      // start with the music off (the pause menu switches it)
    bool no_save = false;       // never read or write the save (test runs never do)
    std::string save_dir;       // keep the save in this folder instead (test runs too)
};

class App {
public:
    static constexpr int kW = 320, kH = 180;  // the Python game's game/config.py INTERNAL_W/H
    App(SDL_Window* window, World& world, RoadGraph& graph, Junctions& junctions, const Options& opt);
    bool ok() const { return renderer_.ok() && target_.ok() && hud_.ok(); }
    int run();  // until quit (or the test run's frames); returns the exit code

private:
    enum class Mode { Menu, Single, Split, Battle };
    void start(int players, int battle = 0);  // 1: the full game; 2-4: split screen; battle 1 balloon, 2 VS race
    void spawn_players();
    void respawn(int i);
    void step(double dt);
    void draw(int frame);
    void draw_view(int i, int x, int y, int w, int h);
    void draw_cars();
    void draw_train();
    void draw_shadows(const Camera& cam);
    void draw_sky(const Camera& cam, int w, int h);
    void draw_car(const Car& c, const std::string& model, const CarLook& look, int key);
    void hud_single();
    void hud_split(int i, int x, int y, int w, int h);
    void hud_battle(int i, int x, int y, int w, int h);
    void draw_battle_things(int viewer);
    void view_rect(int i, int& x, int& y, int& w, int& h) const;
    void draw_menu();
    void draw_pause();
    void draw_touch();
    void draw_clock_hands();
    void draw_tags();
    void refresh_tag(int i);  // remake wall i's art from who holds it
    std::vector<ClockFace> clock_faces_;
    struct Cloud {  // a cartoon cloud: ellipses (dx, dy, rx, ry in pixels of the 224-line Python picture) at a bearing
        double az = 0, elev = 0;
        std::vector<std::array<float, 4>> blobs;
    };
    std::vector<Cloud> clouds_;  // the clock tower's faces (render/building_detail.h)
    void menu_input(const Actions& a);
    void pause_input(const Actions& a);

    SDL_Window* window_;
    World& world_;
    RoadGraph& graph_;
    Junctions& junctions_;
    Options opt_;
    MapRenderer renderer_;
    RenderTarget target_;
    Hud hud_;
    Hud sky_;                                   // the sky, drawn behind each view
    std::unordered_map<int, Mesh> car_meshes_;  // built once per model and look
    Minimap minimap_;
    std::vector<StreetTracker> streets_;        // the street each player is on
    Input input_;
    Sound sound_;
    std::unique_ptr<Town> town_;
    std::unique_ptr<MissionManager> jobs_;
    std::unique_ptr<Battle> battle_;
    std::unique_ptr<Train> train_;
    std::unique_ptr<Tagging> tagging_;                 // the full game's tag spots (game/tagging.h)
    std::vector<std::unique_ptr<Mesh>> tag_meshes_;    // the player's tags, per spot, once sprayed
    double spray_sound_t_ = 0;
    std::string save_path_;     // "" : no saving (test runs, --no-save, no place for app data)
    SaveData save_;             // what was loaded, kept up to date
    bool loaded_ = false;
    void write_save_now();      // the current cash, tags and choices
    int battle_kind_ = 0;
    unsigned battle_seed_ = 1;
    std::vector<std::unique_ptr<Car>> cars_;  // the human players
    std::vector<ChaseCamera> cams_;
    Mode mode_ = Mode::Menu;
    int players_ = 1;
    bool paused_ = false, quit_ = false;
    int menu_index_ = 0, pause_index_ = 0;
    double menu_t_ = 0;
    double start_x_ = 0, start_y_ = 0, start_heading_ = 0;
    // timing for test runs
    double ms_total_ = 0, ms_worst_ = 0;
    long long tris_total_ = 0;
};

}  // namespace amr
