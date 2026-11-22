#include "game/app.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "render/palette.h"
#include "sim/collision.h"

namespace amr {

namespace {
constexpr double kPhysicsHz = 60.0;
constexpr float kFovy = 0.88f;  // the street style's 80 degree view at 16:9
const std::uint8_t kInk[3] = {16, 20, 28}, kYellow[3] = {252, 222, 48}, kOrange[3] = {255, 150, 30};
const std::uint8_t kPanel[3] = {30, 36, 60}, kPanelEdge[3] = {96, 108, 140}, kDim[3] = {200, 208, 230};
const std::uint8_t kTags[4][3] = {{90, 170, 255}, {255, 120, 90}, {120, 230, 120}, {240, 210, 80}};  // P1-P4
const std::uint8_t kPips[3][3] = {{70, 140, 255}, {255, 150, 30}, {190, 80, 255}};                 // boost charge
const std::uint8_t kPipOff[3] = {50, 56, 76};

struct MenuOption {
    const char* label;
    int players;  // 0 = quit
    const char* help;
};
const MenuOption kMenu[] = {
    {"1 PLAYER", 1, "The full game: jobs, police and traffic"},
    {"2 PLAYER", 2, "Split screen free roam. No police at all"},
    {"3 PLAYER", 3, "Player 1 gets the top half, 2 and 3 share the bottom"},
    {"4 PLAYER", 4, "Four way split screen. Bumpers hop and drift for a boost"},
    {"QUIT", 0, ""},
};
constexpr int kMenuCount = sizeof(kMenu) / sizeof(kMenu[0]);
const char* kPause[] = {"RESUME", "MAIN MENU", "QUIT"};

// Screen rectangles (x, y, w, h) of the players in the 320x180 picture (game/twoplayer.py layout).
void layout(int n, int i, int& x, int& y, int& w, int& h) {
    const int W = App::kW, H = App::kH, hw = W / 2, hh = H / 2;
    if (n <= 1) { x = 0; y = 0; w = W; h = H; return; }
    if (n == 2) { x = i * hw; y = 0; w = hw; h = H; return; }
    if (n == 3) {
        if (i == 0) { x = 0; y = 0; w = W; h = hh; }
        else { x = (i - 1) * hw; y = hh; w = hw; h = hh; }
        return;
    }
    x = (i % 2) * hw; y = (i / 2) * hh; w = hw; h = hh;
}

Mat4 box_model(double x, double y, double z, double heading, double len, double wid, float height) {
    Mat4 m = translate({static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)}) *
             rotate_z(static_cast<float>(heading));
    const float l = static_cast<float>(len), w = static_cast<float>(wid);
    m.at(0, 0) *= l; m.at(1, 0) *= l;
    m.at(0, 1) *= w; m.at(1, 1) *= w;
    m.at(2, 2) = height;
    return m;
}
}  // namespace

App::App(SDL_Window* window, World& world, RoadGraph& graph, Junctions& junctions, const Options& opt)
    : window_(window), world_(world), graph_(graph), junctions_(junctions), opt_(opt), renderer_(world),
      target_(kW, kH), hud_(kW, kH) {
    start_x_ = opt.at ? opt.at_x : world.map().spawn_x;
    start_y_ = opt.at ? opt.at_y : world.map().spawn_y;
    start_heading_ = opt.has_heading ? opt.heading : world.map().spawn_heading;
    const bool menu = opt.menu || (opt.players == 0 && opt.frames < 0);
    renderer_.warm(static_cast<float>(start_x_), static_cast<float>(start_y_));
    if (menu) mode_ = Mode::Menu;
    else start(std::max(1, opt.players));
}

void App::start(int players) {
    players_ = std::max(1, std::min(4, players));
    mode_ = players_ == 1 ? Mode::Single : Mode::Split;
    paused_ = false;
    input_.set_players(players_);
    town_ = std::make_unique<Town>(world_, graph_, &junctions_, 1, opt_.traffic, opt_.cops && players_ == 1);
    if (players_ == 1) town_->add_heat(opt_.heat);
    town_->set_free_roam(players_ > 1);
    jobs_ = std::make_unique<MissionManager>(world_, graph_, *town_);
    spawn_players();
}

// The start grid (game/twoplayer.py _spawn): one player at the start; two side by side; three or four in two rows,
// the second 7 m behind.
void App::spawn_players() {
    cars_.clear();
    cams_.clear();
    const double h = start_heading_, rx = -std::sin(h), ry = std::cos(h);
    for (int i = 0; i < players_; ++i) {
        double side = 0, back = 0;
        if (players_ == 2) side = i == 0 ? -2.6 : 2.6;
        else if (players_ > 2) { side = i % 2 == 0 ? -1.7 : 1.7; back = 7.0 * (i / 2); }
        const double x = start_x_ + rx * side - std::cos(h) * back, y = start_y_ + ry * side - std::sin(h) * back;
        auto car = std::make_unique<Car>(x, y, h, player_model(i));
        car->z = world_.height_at(static_cast<float>(x), static_cast<float>(y), world_.ground(static_cast<float>(x), static_cast<float>(y)));
        cams_.emplace_back(*car);
        cams_.back().update(*car, world_, 0.0, true);
        cars_.push_back(std::move(car));
    }
    std::vector<Car*> extra;
    for (int i = 1; i < players_; ++i) extra.push_back(cars_[i].get());
    town_->set_extra(extra);
}

void App::respawn(int i) {
    if (i < 0 || i >= players_) return;
    const int keep = players_;
    std::vector<std::unique_ptr<Car>> old;
    old.swap(cars_);
    spawn_players();  // a fresh grid, then keep everyone else where they were
    for (int k = 0; k < keep; ++k)
        if (k != i) {
            *cars_[k] = *old[k];
            cams_[k] = ChaseCamera(*cars_[k]);
            cams_[k].update(*cars_[k], world_, 0.0, true);
        }
}

void App::menu_input(const Actions& a) {
    if (a.up) menu_index_ = (menu_index_ + kMenuCount - 1) % kMenuCount;
    if (a.down) menu_index_ = (menu_index_ + 1) % kMenuCount;
    if (a.back || a.quit) quit_ = true;
    if (a.confirm) {
        if (kMenu[menu_index_].players == 0) quit_ = true;
        else start(kMenu[menu_index_].players);
    }
}

void App::pause_input(const Actions& a) {
    if (a.up) pause_index_ = (pause_index_ + 2) % 3;
    if (a.down) pause_index_ = (pause_index_ + 1) % 3;
    if (a.back) { paused_ = false; return; }
    if (a.confirm) {
        if (pause_index_ == 0) paused_ = false;
        else if (pause_index_ == 1) { paused_ = false; mode_ = Mode::Menu; }
        else quit_ = true;
    }
}

void App::step(double dt) {
    if (mode_ == Mode::Menu || paused_) return;
    for (int i = 0; i < players_; ++i) {
        Car& car = *cars_[i];
        const Controls c = input_.controls(i, players_);
        CarInput drive{c.throttle, c.brake, c.steer, c.handbrake, c.bumper};
        if (opt_.frames >= 0) drive = CarInput{true, false, 0.0, false, false};  // drive itself, straight (tests)
        if (town_->state() != TownState::Playing && i == 0) drive = CarInput{false, false, 0.0, true, false};
        const double px = car.x, py = car.y;  // as game/main.py steps the player
        car.power = players_ == 1 ? town_->player_power() : 1.0;
        car.update(dt, drive, world_.on_road(car.x, car.y));
        const double impact = move_and_slide(car, world_, px, py, true);
        settle_height(car, world_, dt);
        if (i == 0) {
            town_->update(dt, car, players_ == 1 ? impact : 0.0);
        }
    }
    if (players_ == 1) {
        jobs_->update(dt, *cars_[0]);
        if (town_->consume_reset()) {  // after BUSTED or WRECKED: back to the start, fresh car, quiet town
            town_->reset();
            jobs_->abort();
            spawn_players();
        }
    }
    for (int i = 0; i < players_; ++i) cams_[i].update(*cars_[i], world_, dt);
}

int App::run() {
    Uint64 last = SDL_GetPerformanceCounter();
    const double freq = static_cast<double>(SDL_GetPerformanceFrequency());
    double acc = 0.0;
    int frame = 0;
    while (!quit_) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) input_.handle(e);
        const Actions a = input_.actions();
        if (a.quit) break;
        if (mode_ == Mode::Menu) {
            menu_t_ += 1.0 / 60.0;
            menu_input(a);
        } else if (paused_) {
            pause_input(a);
        } else {
            if (a.pause) { paused_ = true; pause_index_ = 0; }
            if (mode_ == Mode::Split && a.respawn >= 0) respawn(a.respawn);
        }
        // fixed-timestep simulation; a test run steps exactly one 30 fps frame each loop
        const Uint64 now = SDL_GetPerformanceCounter();
        acc += opt_.frames >= 0 ? 1.0 / 30.0 : std::fmin(0.25, (now - last) / freq);
        last = now;
        while (acc >= 1.0 / kPhysicsHz) {
            step(1.0 / kPhysicsHz);
            acc -= 1.0 / kPhysicsHz;
        }
        draw(frame);
        ++frame;
        if (opt_.frames >= 0 && frame >= opt_.frames) break;
    }
    int code = 0;
    if (opt_.frames > 1)
        std::printf("frames: %d, draw avg %.2f ms, worst %.2f ms, %lld triangles a frame, %zu traffic cars, %d players\n",
                    frame, ms_total_ / (frame - 1), ms_worst_, tris_total_ / frame, town_ ? town_->cars().size() : 0,
                    mode_ == Mode::Menu ? 0 : players_);
    if (!opt_.screenshot.empty()) {
        if (target_.save_bmp(opt_.screenshot)) {
            if (!cars_.empty())
                std::printf("wrote %s (car at %.1f, %.1f, %.1f)\n", opt_.screenshot.c_str(), cars_[0]->x, cars_[0]->y, cars_[0]->z);
            else
                std::printf("wrote %s\n", opt_.screenshot.c_str());
        } else {
            std::fprintf(stderr, "could not save %s: %s\n", opt_.screenshot.c_str(), SDL_GetError());
            code = 1;
        }
    }
    return code;
}

void App::draw_cars() {
    for (int k = 0; k < players_ && mode_ != Mode::Menu; ++k) {
        const Car& c = *cars_[k];
        renderer_.draw_box(box_model(c.x, c.y, c.z + c.hop_z + 0.3, c.heading, c.length, c.width, 1.65f),
                           k == 0 ? palette::kCarYellow : kTags[k]);
    }
    if (!town_ || mode_ == Mode::Menu) return;
    for (const auto& c : town_->cars())  // traffic and police: boxes in their colours, for now
        renderer_.draw_box(box_model(c->x, c->y, c->z + 0.3, c->heading, c->length, c->width, 1.45f),
                           c->police ? palette::kPoliceBody : palette::kTraffic[c->colour]);
}

void App::draw_view(int i, int x, int y, int w, int h) {
    glViewport(x, kH - y - h, w, h);  // (OpenGL counts rows from the bottom)
    glEnable(GL_SCISSOR_TEST);
    glScissor(x, kH - y - h, w, h);
    glClearColor(palette::kFog[0] / 255.0f, palette::kFog[1] / 255.0f, palette::kFog[2] / 255.0f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    const FrameStats st = renderer_.draw(cams_[i].camera(kFovy), static_cast<float>(w) / h);
    tris_total_ += st.triangles;
    draw_cars();
    if (mode_ == Mode::Single) {
        for (const Marker& mk : jobs_->markers()) {  // job beacons: a glowing column and a pad on the ground
            const float mx = static_cast<float>(mk.x), my = static_cast<float>(mk.y), g = world_.ground(mx, my);
            const std::uint8_t col[3] = {mk.colour.r, mk.colour.g, mk.colour.b};
            Mat4 pad = translate({mx, my, g + 0.05f});
            pad.at(0, 0) = pad.at(1, 1) = static_cast<float>(mk.radius) * 1.6f;
            pad.at(2, 2) = 0.08f;
            renderer_.draw_box(pad, col);
            Mat4 beam = translate({mx, my, g});
            beam.at(0, 0) = beam.at(1, 1) = 0.8f;
            beam.at(2, 2) = mk.hub ? 14.0f : 30.0f;
            renderer_.draw_box(beam, col);
        }
    }
    glDisable(GL_SCISSOR_TEST);
}

void App::draw(int frame) {
    const Uint64 t0 = SDL_GetPerformanceCounter();
    target_.bind();
    hud_.begin();
    if (mode_ == Mode::Menu) {
        draw_menu();
    } else {
        for (int i = 0; i < players_; ++i) {
            int x, y, w, h;
            layout(players_, i, x, y, w, h);
            draw_view(i, x, y, w, h);
        }
        glViewport(0, 0, kW, kH);
        if (mode_ == Mode::Single) {
            hud_single();
        } else {
            if (players_ == 2) hud_.rect(kW / 2 - 1, 0, 2, kH, kInk);  // the split lines
            else {
                hud_.rect(0, kH / 2 - 1, kW, 2, kInk);
                hud_.rect(kW / 2 - 1, players_ == 4 ? 0 : kH / 2, 2, players_ == 4 ? kH : kH / 2, kInk);
            }
            for (int i = 0; i < players_; ++i) {
                int x, y, w, h;
                layout(players_, i, x, y, w, h);
                hud_split(i, x, y, w, h);
            }
        }
        if (paused_) draw_pause();
    }
    hud_.end();
    if (opt_.frames >= 0) {
        glFinish();  // honest timing in test runs: wait for the GPU
        const double ms = (SDL_GetPerformanceCounter() - t0) * 1000.0 / SDL_GetPerformanceFrequency();
        if (frame > 0) {
            ms_total_ += ms;
            ms_worst_ = std::max(ms_worst_, ms);
        }
    }
    int ww = 0, wh = 0;
    SDL_GL_GetDrawableSize(window_, &ww, &wh);
    target_.present(ww, wh);
    SDL_GL_SwapWindow(window_);
}

void App::hud_single() {
    const Car& car = *cars_[0];
    Town& town = *town_;
    MissionManager& jobs = *jobs_;
    const std::string kmh = std::to_string(static_cast<int>(std::lround(car.speed() * 3.6))) + " KM/H";
    hud_.text(6, kH - 14, kmh, palette::kHudText, 2);
    for (int k = 0; k < 4; ++k)
        hud_.rect(kW - 52 + k * 12, 6, 9, 9, k < town.stars() ? palette::kHudStar : palette::kHudStarOff);
    hud_.text(kW - 82, kH - 13, "DMG", palette::kHudText);
    hud_.rect(kW - 66, kH - 14, 60, 7, palette::kHudBar);
    hud_.rect(kW - 65, kH - 13, static_cast<int>(58 * town.damage() / 100.0), 5, palette::kHudDamage);
    hud_.text(6, 6, "$" + std::to_string(jobs.cash()), palette::kHudCash, 2);  // cash, in money green
    if (const Job* j = jobs.job()) {  // the job: timer, objective, and an arrow to the target
        const int secs = std::max(0, static_cast<int>(std::ceil(j->time_left)));
        const std::string clock = std::to_string(secs / 60) + ":" + (secs % 60 < 10 ? "0" : "") + std::to_string(secs % 60);
        const std::uint8_t jc[3] = {j->colour.r, j->colour.g, j->colour.b};
        hud_.text((kW - hud_.text_width(clock, 2)) / 2, 6, clock, secs < 15 ? palette::kHudRed : palette::kHudText, 2);
        const std::string obj = jobs.objective();
        hud_.text((kW - hud_.text_width(obj)) / 2, 20, obj, jc);
        double tx, ty;
        if (jobs.current_target(tx, ty)) {
            const double a = wrap_angle(std::atan2(ty - car.y, tx - car.x) - car.heading);  // + is to the right
            const float cx = kW / 2.0f, cy = 38.0f, r = 8.0f;
            const float sa = static_cast<float>(std::sin(a)), ca = static_cast<float>(std::cos(a));
            hud_.tri(cx + sa * r, cy - ca * r, cx - ca * 5 - sa * 4, cy - sa * 5 + ca * 4, cx + ca * 5 - sa * 4,
                     cy + sa * 5 + ca * 4, jc);
            const std::string dist = std::to_string(static_cast<int>(std::hypot(tx - car.x, ty - car.y))) + " M";
            hud_.text((kW - hud_.text_width(dist)) / 2, 50, dist, palette::kHudText);
        }
    } else {
        const std::string h = jobs.hint(car);
        if (!h.empty()) hud_.text((kW - hud_.text_width(h)) / 2, kH - 28, h, palette::kHudText);
    }
    if (!jobs.banner().empty()) {
        const std::uint8_t bc[3] = {jobs.banner_colour().r, jobs.banner_colour().g, jobs.banner_colour().b};
        hud_.text((kW - hud_.text_width(jobs.banner(), 2)) / 2, 66, jobs.banner(), bc, 2);
    }
    if (town.state() != TownState::Playing) {
        const std::string big = town.state() == TownState::Busted ? "BUSTED" : "WRECKED";
        hud_.text((kW - hud_.text_width(big, 4)) / 2, 60, big, palette::kHudRed, 4);
    }
}

// A split-screen player's corner: P1..P4 in their colour, speed, and the boost pips while sliding or boosting.
void App::hud_split(int i, int x, int y, int w, int h) {
    const Car& car = *cars_[i];
    hud_.text(x + 5, y + 4, "P" + std::to_string(i + 1), kTags[i], 2);
    const std::string kmh = std::to_string(static_cast<int>(car.speed() * 3.6)) + " KM/H";
    hud_.text(x + 5, y + h - 10, kmh, kOrange);
    if (car.drift || car.boost_t > 0.0)
        for (int k = 0; k < 3; ++k) {
            const bool lit = car.drift ? car.charge_level() > k : true;
            const int px = x + w / 2 - 21 + k * 15, py = y + h - 9;
            hud_.rect(px - 1, py - 1, 14, 7, kInk);
            hud_.rect(px, py, 12, 5, lit ? kPips[k] : kPipOff);
        }
}

void App::draw_menu() {
    // the town from a slowly turning camera above the start (game/menu.py)
    const double ang = menu_t_ * 0.12;
    Camera cam;
    cam.fovy = kFovy;
    const float sx = static_cast<float>(start_x_), sy = static_cast<float>(start_y_);
    cam.eye = {sx + static_cast<float>(std::cos(ang)) * 70.0f, sy + static_cast<float>(std::sin(ang)) * 70.0f, 0.0f};
    cam.eye.z = world_.ground(cam.eye.x, cam.eye.y) + 26.0f;
    const double yaw = ang + 3.14159265358979, pitch = 0.3;
    cam.target = {cam.eye.x + static_cast<float>(std::cos(yaw) * std::cos(pitch) * 10.0),
                  cam.eye.y + static_cast<float>(std::sin(yaw) * std::cos(pitch) * 10.0),
                  cam.eye.z - static_cast<float>(std::sin(pitch) * 10.0)};
    glViewport(0, 0, kW, kH);
    glClearColor(palette::kFog[0] / 255.0f, palette::kFog[1] / 255.0f, palette::kFog[2] / 255.0f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    renderer_.draw(cam, static_cast<float>(kW) / kH);
    hud_.text((kW - hud_.text_width("AMHERST RUN", 4)) / 2, 12, "AMHERST RUN", kYellow, 4);
    hud_.text((kW - hud_.text_width("AMHERST, NOVA SCOTIA")) / 2, 36, "AMHERST, NOVA SCOTIA", palette::kHudText);
    for (int i = 0; i < kMenuCount; ++i) {
        const int rx = 90, ry = 52 + i * 17, rw = 140, rh = 14;
        const bool sel = i == menu_index_;
        if (sel) {
            hud_.rect(rx - 2, ry - 2, rw + 4, rh + 4, kInk);
            hud_.rect(rx, ry, rw, rh, kOrange);
            if (static_cast<int>(menu_t_ * 3) % 2 == 0) hud_.tri(rx - 12, ry + 2, rx - 12, ry + 12, rx - 4, ry + 7, kYellow);
        } else {
            hud_.rect(rx - 1, ry - 1, rw + 2, rh + 2, kPanelEdge);
            hud_.rect(rx, ry, rw, rh, kPanel);
        }
        const std::string label = kMenu[i].label;
        hud_.text(rx + (rw - hud_.text_width(label, 2)) / 2, ry + 2, label, sel ? kInk : kDim, 2);
    }
    const std::string help = kMenu[menu_index_].help;
    hud_.text((kW - hud_.text_width(help)) / 2, 142, help, palette::kHudText);
    const int n = input_.pads();
    const std::string pads = n >= 2 ? std::to_string(n) + " gamepads found. LB/RB = hop + drift boost"
                           : n == 1 ? "1 gamepad found: it drives the LAST player (keyboard: WASD + arrows)"
                                    : "No gamepad: P1 WASD+Shift, P2 arrows+RShift";
    hud_.text((kW - hud_.text_width(pads)) / 2, 156, pads, kDim);
    const std::string credit = "Map data (c) OpenStreetMap contributors";
    hud_.text((kW - hud_.text_width(credit)) / 2, 168, credit, kPanelEdge);
}

void App::draw_pause() {
    const int bw = 140, bh = 82, bx = (kW - bw) / 2, by = (kH - bh) / 2;
    hud_.rect(bx - 2, by - 2, bw + 4, bh + 4, kInk);
    hud_.rect(bx, by, bw, bh, kPanel);
    hud_.text((kW - hud_.text_width("PAUSED", 2)) / 2, by + 6, "PAUSED", kYellow, 2);
    for (int i = 0; i < 3; ++i) {
        const int ry = by + 24 + i * 17, rx = bx + 15, rw = bw - 30, rh = 14;
        const bool sel = i == pause_index_;
        hud_.rect(rx, ry, rw, rh, sel ? kOrange : kPanel);
        if (!sel) hud_.rect(rx, ry + rh - 1, rw, 1, kPanelEdge);
        const std::string label = kPause[i];
        hud_.text(rx + (rw - hud_.text_width(label, 2)) / 2, ry + 2, label, sel ? kInk : kDim, 2);
    }
}

}  // namespace amr
