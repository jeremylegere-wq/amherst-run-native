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
    int battle;   // 1 balloon battle, 2 VS race
    const char* help;
};
const MenuOption kMenu[] = {
    {"1 PLAYER", 1, 0, "The full game: jobs, police and traffic"},
    {"2 PLAYER", 2, 0, "Split screen free roam. No police at all"},
    {"3 PLAYER", 3, 0, "Player 1 gets the top half, 2 and 3 share the bottom"},
    {"4 PLAYER", 4, 0, "Four way split screen. Bumpers hop and drift for a boost"},
    {"BATTLE", 2, 1, "2 players: item boxes downtown, pop the other car's balloons"},
    {"VS RACE", 2, 2, "2 players race to a random Amherst landmark, items on the way"},
    {"QUIT", 0, 0, ""},
};
const std::uint8_t kBalloon[4][3] = {{90, 170, 255}, {255, 120, 90}, {120, 230, 120}, {240, 210, 80}};
const std::uint8_t kBeacon[3] = {255, 220, 60}, kBoxCol[3] = {250, 200, 60}, kSlickCol[3] = {34, 30, 40};
const std::uint8_t kWindow[3] = {40, 46, 70};

// A saturated colour around the colour wheel (h in 0..1): the ghost's rainbow body (game/battle.py _rainbow).
void rainbow(double h, std::uint8_t out[3]) {
    const double k = h * 6.0;
    const int i = static_cast<int>(k) % 6;
    const double f = k - static_cast<int>(k);
    const std::uint8_t q = static_cast<std::uint8_t>(255 * (1 - f)), t = static_cast<std::uint8_t>(255 * f);
    const std::uint8_t c[6][3] = {{255, t, 0}, {q, 255, 0}, {0, 255, t}, {0, q, 255}, {t, 0, 255}, {255, 0, q}};
    out[0] = c[i][0]; out[1] = c[i][1]; out[2] = c[i][2];
}
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
    const bool menu = opt.menu || (opt.players == 0 && opt.battle == 0 && opt.frames < 0);
    renderer_.warm(static_cast<float>(start_x_), static_cast<float>(start_y_));
    if (menu) mode_ = Mode::Menu;
    else if (opt.battle) start(2, opt.battle);
    else start(std::max(1, opt.players));
}

void App::start(int players, int battle) {
    players_ = std::max(1, std::min(4, players));
    battle_kind_ = battle;
    mode_ = battle ? Mode::Battle : (players_ == 1 ? Mode::Single : Mode::Split);
    paused_ = false;
    input_.set_players(players_);
    battle_.reset();
    if (battle) {
        battle_ = std::make_unique<Battle>(world_, graph_, battle == 1 ? Battle::Mode::Balloon : Battle::Mode::Sprint,
                                           players_, battle_seed_++);
        double x, y, h;
        battle_->start(0, x, y, h);
        renderer_.warm(static_cast<float>(x), static_cast<float>(y));
    }
    const int traffic = battle ? std::min(opt_.traffic, Battle::kTraffic) : opt_.traffic;
    town_ = std::make_unique<Town>(world_, graph_, &junctions_, 1, traffic, opt_.cops && players_ == 1);
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
        double x = start_x_ + rx * side - std::cos(h) * back, y = start_y_ + ry * side - std::sin(h) * back, hh = h;
        if (battle_) battle_->start(i, x, y, hh);  // battles: the battle's own start spots
        auto car = std::make_unique<Car>(x, y, hh, player_model(i));
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
        else start(kMenu[menu_index_].players, kMenu[menu_index_].battle);
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
    std::vector<bool> fire(players_, false);
    for (int i = 0; i < players_; ++i) {
        Car& car = *cars_[i];
        const Controls c = input_.controls(i, players_, battle_ != nullptr);
        CarInput drive{c.throttle, c.brake, c.steer, c.handbrake, c.bumper};
        fire[i] = c.fire;
        if (opt_.frames >= 0) drive = CarInput{true, false, 0.0, false, false};  // drive itself, straight (tests)
        if (battle_) battle_->adjust(i, drive.throttle, drive.brake, drive.steer, drive.handbrake, drive.bumper);
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
    if (battle_) {
        std::vector<Car*> cars;
        for (auto& c : cars_) cars.push_back(c.get());
        std::vector<const Car*> town;
        for (const auto& c : town_->cars()) town.push_back(c.get());
        battle_->update(dt, cars, fire, town);
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
            if (battle_ && battle_->winner() != -2 && a.confirm) start(players_, battle_kind_);  // rematch
            else if (a.pause) { paused_ = true; pause_index_ = 0; }
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
    if (battle_ && opt_.frames > 1)
        std::printf("battle: %s, %zu item boxes%s%s\n", battle_->mode() == Battle::Mode::Balloon ? "balloon" : "VS race",
                    battle_->boxes().size(), battle_->has_target() ? ", race to " : "",
                    battle_->has_target() ? battle_->target_label().c_str() : "");
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
        std::uint8_t col[3] = {kTags[k][0], kTags[k][1], kTags[k][2]};
        if (k == 0) std::copy(palette::kCarYellow, palette::kCarYellow + 3, col);
        if (battle_ && battle_->racers()[k].ghost > 0.0) rainbow(std::fmod(battle_->time() * 3.0 + k, 1.0), col);
        renderer_.draw_box(box_model(c.x, c.y, c.z + c.hop_z + 0.3, c.heading, c.length, c.width, 1.65f), col);
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
    if (battle_) draw_battle_things(i);
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
            view_rect(i, x, y, w, h);
            draw_view(i, x, y, w, h);
        }
        glViewport(0, 0, kW, kH);
        if (mode_ == Mode::Single) {
            hud_single();
        } else {
            if (players_ == 2 && battle_) hud_.rect(0, kH / 2 - 1, kW, 2, kInk);  // the split lines
            else if (players_ == 2) hud_.rect(kW / 2 - 1, 0, 2, kH, kInk);
            else {
                hud_.rect(0, kH / 2 - 1, kW, 2, kInk);
                hud_.rect(kW / 2 - 1, players_ == 4 ? 0 : kH / 2, 2, players_ == 4 ? kH : kH / 2, kInk);
            }
            for (int i = 0; i < players_; ++i) {
                int x, y, w, h;
                view_rect(i, x, y, w, h);
                if (battle_) hud_battle(i, x, y, w, h);
                else hud_split(i, x, y, w, h);
            }
            if (battle_) {  // the middle: the opening banner, or who won
                if (battle_->winner() != -2) {
                    const int wnr = battle_->winner();
                    const std::string msg = wnr < 0 ? "DRAW!" : "PLAYER " + std::to_string(wnr + 1) + " WINS!";
                    hud_.text((kW - hud_.text_width(msg, 3)) / 2, kH / 2 - 14, msg, wnr < 0 ? palette::kHudText : kTags[wnr], 3);
                    const std::string sub = "A / START / ENTER: REMATCH    BACK / ESC: MENU";
                    hud_.text((kW - hud_.text_width(sub)) / 2, kH / 2 + 6, sub, palette::kHudText);
                } else if (!battle_->banner().empty()) {
                    hud_.text((kW - hud_.text_width(battle_->banner(), 2)) / 2, kH / 2 - 5, battle_->banner(), kYellow, 2);
                }
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

void App::view_rect(int i, int& x, int& y, int& w, int& h) const {
    if (battle_ && players_ == 2) {  // battles split top / bottom (game/battle.py "rows")
        x = 0; y = i * (kH / 2); w = kW; h = kH / 2;
        return;
    }
    layout(players_, i, x, y, w, h);
}

// Balloons (not your own: the HUD counts them), item boxes, shots, slicks, orbiting pucks, and the race beacon.
void App::draw_battle_things(int viewer) {
    const Battle& b = *battle_;
    const double t = b.time();
    auto ground = [&](double x, double y) { return world_.ground(static_cast<float>(x), static_cast<float>(y)); };
    for (int i = 0; i < players_; ++i) {
        const Car& car = *cars_[i];
        const Racer& r = b.racers()[i];
        if (b.mode() == Battle::Mode::Balloon && r.balloons > 0 && i != viewer && !(r.safe > 0.0 && static_cast<int>(t * 10) % 2)) {
            for (int k = 0; k < r.balloons; ++k) {
                const double side = (k - (r.balloons - 1) / 2.0) * 0.9;
                const double x = car.x - car.fx() * 2.6 - car.fy() * side, y = car.y - car.fy() * 2.6 + car.fx() * side;
                const double z = car.z + 2.6 + 0.15 * std::sin(t * 3.0 + k) + 0.25 * (k % 2);
                renderer_.draw_box(box_model(x, y, z, 0.0, 0.7, 0.7, 0.8f), kBalloon[i]);
                renderer_.draw_box(box_model(x, y, car.z + 1.4, 0.0, 0.05, 0.05, static_cast<float>(z - car.z - 1.4)), kInk);
            }
        }
        for (const auto& p : orbit_points(car, r.orbit, t))
            renderer_.draw_box(box_model(p.first, p.second, car.z + 0.4, 0.0, 0.7, 0.7, 0.2f), item_colour(Item::Puck));
    }
    for (const ItemBox& bx : b.boxes())
        if (bx.present())
            renderer_.draw_box(box_model(bx.x, bx.y, ground(bx.x, bx.y) + 0.6, t * 1.6, 1.2, 1.2, 1.2f), kBoxCol);
    for (const Shot& s : b.shots())
        renderer_.draw_box(box_model(s.x, s.y, ground(s.x, s.y) + 0.3, t * 8, 0.8, 0.8, 0.25f), item_colour(s.kind));
    for (const Slick& sl : b.slicks())
        renderer_.draw_box(box_model(sl.x, sl.y, ground(sl.x, sl.y) + 0.02, 0.0, kSlickRadius * 2, kSlickRadius * 2, 0.04f), kSlickCol);
    if (b.has_target()) {
        const float g = ground(b.target_x(), b.target_y());
        renderer_.draw_box(box_model(b.target_x(), b.target_y(), g + 0.05, 0.0, Battle::kFinish * 1.2, Battle::kFinish * 1.2, 0.08f), kBeacon);
        renderer_.draw_box(box_model(b.target_x(), b.target_y(), g, 0.0, 1.6, 1.6, 60.0f), kBeacon);
    }
}

// A battle player's corner (game/battle.py _battle_hud): the item window, P1/P2, balloons (or the distance and an
// arrow to the finish), speed and boost pips.
void App::hud_battle(int i, int x, int y, int w, int h) {
    const Car& car = *cars_[i];
    const Racer& r = battle_->racers()[i];
    hud_.rect(x + 3, y + 3, 22, 22, kInk);
    hud_.rect(x + 4, y + 4, 20, 20, kTags[i]);
    hud_.rect(x + 5, y + 5, 18, 18, kWindow);
    const Item shown = r.roulette > 0.0 ? r.shown : r.item;
    if (shown != Item::None) {
        const std::uint8_t* col = shown == Item::Slick ? kPanelEdge : item_colour(shown);
        if (shown == Item::Triple) {
            for (int k = 0; k < std::max(1, r.orbit); ++k) hud_.rect(x + 7 + k * 5, y + 11, 4, 4, col);
        } else {
            hud_.rect(x + 9, y + 9, 10, 10, col);
        }
        if (r.roulette <= 0.0) hud_.text(x + 28, y + 9, item_name(shown), palette::kHudText);
    }
    hud_.text(x + 5, y + 28, "P" + std::to_string(i + 1), kTags[i]);
    if (battle_->mode() == Battle::Mode::Balloon) {
        for (int k = 0; k < Battle::kBalloons; ++k) {
            const int cx = x + w - 12 - k * 11;
            hud_.rect(cx - 1, y + 5, 9, 9, kInk);
            hud_.rect(cx, y + 6, 7, 7, k < r.balloons ? kBalloon[i] : kPipOff);
            hud_.rect(cx + 3, y + 14, 1, 5, kInk);
        }
        if (r.out) hud_.text(x + (w - hud_.text_width("OUT", 2)) / 2, y + h / 2 - 5, "OUT", palette::kHudRed, 2);
    } else {
        const double d = std::hypot(battle_->target_x() - car.x, battle_->target_y() - car.y);
        char buf[32];
        if (d >= 1000.0) std::snprintf(buf, sizeof buf, "%.1f KM", d / 1000.0);
        else std::snprintf(buf, sizeof buf, "%d M", static_cast<int>(d));
        hud_.text(x + w - 8 - hud_.text_width(buf), y + 5, buf, kBeacon);
        const double a = wrap_angle(std::atan2(battle_->target_y() - car.y, battle_->target_x() - car.x) - car.heading);
        const float cx = x + w - 18.0f, cy = y + 22.0f, sa = static_cast<float>(std::sin(a)), ca = static_cast<float>(std::cos(a));
        hud_.tri(cx + sa * 7, cy - ca * 7, cx - ca * 4 - sa * 3, cy - sa * 4 + ca * 3, cx + ca * 4 - sa * 3, cy + sa * 4 + ca * 3, kBeacon);
    }
    hud_.text(x + 5, y + h - 10, std::to_string(static_cast<int>(car.speed() * 3.6)) + " KM/H", kOrange);
    if (car.drift || car.boost_t > 0.0)
        for (int k = 0; k < 3; ++k) {
            const bool lit = car.drift ? car.charge_level() > k : true;
            const int px = x + w / 2 - 21 + k * 15, py = y + h - 9;
            hud_.rect(px - 1, py - 1, 14, 7, kInk);
            hud_.rect(px, py, 12, 5, lit ? kPips[k] : kPipOff);
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
        const int rx = 90, ry = 46 + i * 14, rw = 140, rh = 12;
        const bool sel = i == menu_index_;
        if (sel) {
            hud_.rect(rx - 2, ry - 2, rw + 4, rh + 4, kInk);
            hud_.rect(rx, ry, rw, rh, kOrange);
            if (static_cast<int>(menu_t_ * 3) % 2 == 0) hud_.tri(rx - 12, ry + 1, rx - 12, ry + 11, rx - 4, ry + 6, kYellow);
        } else {
            hud_.rect(rx - 1, ry - 1, rw + 2, rh + 2, kPanelEdge);
            hud_.rect(rx, ry, rw, rh, kPanel);
        }
        const std::string label = kMenu[i].label;
        hud_.text(rx + (rw - hud_.text_width(label, 2)) / 2, ry + 1, label, sel ? kInk : kDim, 2);
    }
    const std::string help = kMenu[menu_index_].help;
    hud_.text((kW - hud_.text_width(help)) / 2, 147, help, palette::kHudText);
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
