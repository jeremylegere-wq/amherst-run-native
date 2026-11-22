// Amherst Run (native): the real Amherst map (baked by amherst-run's tools/bake_native.py) drawn at 320x180 and
// scaled up with chunky pixels, and player 1's car with the Python game's arcade physics: buildings and the map edge
// stop it, it rides the overpass decks and feels the hills (docs: amherst-run docs/CPP_PORT_PLAN.md, phase 1).
//
//   amherst-run                                  play (pad: left stick, A handbrake, LB/RB hop and slide, Back quits)
//   amherst-run --map path/to/amherst.amr        another map file (default: assets/amherst.amr beside the game)
//   amherst-run --traffic N                      civilian cars around you downtown (default 16, 0 for none)
//   amherst-run --no-cops | --heat H             no police at all | start with a wanted level (0-4)
//   amherst-run --at X,Y [--heading DEG]         start at this map point (metres), facing a compass bearing
//   amherst-run --frames 120 --screenshot out.bmp   drive itself for 120 frames, save the picture, report timing
#include <SDL.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "core/input.h"
#include "core/math.h"
#include "render/gl.h"
#include "render/shader.h"
#include "render/chase_camera.h"
#include "game/missions.h"
#include "render/hud.h"
#include "render/map_renderer.h"
#include "render/palette.h"
#include "render/target.h"
#include "sim/car.h"
#include "sim/collision.h"
#include "sim/junctions.h"
#include "sim/roadgraph.h"
#include "sim/town.h"
#include "world/map_file.h"
#include "world/world.h"

#ifndef AMR_VERSION
#define AMR_VERSION "dev"
#endif

namespace {

constexpr int kInternalW = 320, kInternalH = 180;  // the Python game's game/config.py INTERNAL_W/H
constexpr double kPhysicsHz = 60.0;                 // fixed timestep, as PHYSICS_HZ

struct Args {
    int frames = -1;
    std::string screenshot, map;
    bool at = false, has_heading = false;
    int traffic = 16;
    bool cops = true;
    double heat = 0;
    float at_x = 0, at_y = 0, heading = 0;
};

std::string default_map() {
    std::string base;
    if (char* p = SDL_GetBasePath()) {
        base = p;
        SDL_free(p);
    }
    for (const char* rel : {"assets/amherst.amr", "../assets/amherst.amr"}) {
        std::string path = base + rel;
        if (SDL_RWops* f = SDL_RWFromFile(path.c_str(), "rb")) {
            SDL_RWclose(f);
            return path;
        }
    }
    return "assets/amherst.amr";
}

bool parse(int argc, char** argv, Args& a) {
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--version")) {
            std::printf("Amherst Run (native) %s\n", AMR_VERSION);
            std::exit(0);
        } else if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) {
            a.frames = std::atoi(argv[++i]);
        } else if (!std::strcmp(argv[i], "--screenshot") && i + 1 < argc) {
            a.screenshot = argv[++i];
        } else if (!std::strcmp(argv[i], "--no-cops")) {
            a.cops = false;
        } else if (!std::strcmp(argv[i], "--heat") && i + 1 < argc) {
            a.heat = std::atof(argv[++i]);
        } else if (!std::strcmp(argv[i], "--traffic") && i + 1 < argc) {
            a.traffic = std::atoi(argv[++i]);
        } else if (!std::strcmp(argv[i], "--map") && i + 1 < argc) {
            a.map = argv[++i];
        } else if (!std::strcmp(argv[i], "--at") && i + 1 < argc) {
            a.at = std::sscanf(argv[++i], "%f,%f", &a.at_x, &a.at_y) == 2;
            if (!a.at) return false;
        } else if (!std::strcmp(argv[i], "--heading") && i + 1 < argc) {
            const double b = std::atof(argv[++i]) * 3.14159265358979 / 180.0;  // compass bearing: 0 north, 90 east
            a.heading = static_cast<float>(std::atan2(-std::cos(b), std::sin(b)));  // map y grows south
            a.has_heading = true;
        } else {
            std::fprintf(stderr, "unknown option: %s\n", argv[i]);
            return false;
        }
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    Args args;
    if (!parse(argc, argv, args)) return 2;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    SDL_Window* win = SDL_CreateWindow("Amherst Run", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       kInternalW * 3, kInternalH * 3, flags);
    if (!win) {
        std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GLContext ctx = SDL_GL_CreateContext(win);
    if (!ctx) {
        std::fprintf(stderr, "OpenGL ES 3.0 context: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetSwapInterval(1);
    std::printf("Amherst Run (native) %s: %s, %s\n", AMR_VERSION, reinterpret_cast<const char*>(glGetString(GL_VERSION)),
                reinterpret_cast<const char*>(glGetString(GL_RENDERER)));

    std::string err;
    amr::MapData map;
    const std::string map_path = args.map.empty() ? default_map() : args.map;
    const Uint64 t_load = SDL_GetPerformanceCounter();
    if (!amr::load_map(map_path, map, err)) {
        std::fprintf(stderr, "map: %s\n(bake it with amherst-run's tools/bake_native.py, or get it with "
                             "tools/fetch_map.py)\n", err.c_str());
        return 1;
    }
    amr::RoadGraph graph(map);
    amr::World world(std::move(map));
    amr::MapRenderer renderer(world);
    amr::RenderTarget target(kInternalW, kInternalH);
    if (!renderer.ok() || !target.ok()) {
        std::fprintf(stderr, "could not set up the renderer\n");
        return 1;
    }
    std::printf("map %s: %zu roads, %zu buildings, %zu overpass decks (%s), loaded in %.2f s\n", map_path.c_str(),
                world.map().roads.size(), world.map().buildings.size(), world.map().spans.size(),
                world.map().source.c_str(),
                (SDL_GetPerformanceCounter() - t_load) / static_cast<double>(SDL_GetPerformanceFrequency()));
    amr::Input input;
    const float sx = args.at ? args.at_x : world.map().spawn_x, sy = args.at ? args.at_y : world.map().spawn_y;
    amr::Car car(sx, sy, args.has_heading ? args.heading : world.map().spawn_heading, amr::player_model(0));
    car.z = world.height_at(sx, sy, world.ground(sx, sy));
    amr::Junctions junctions(world.map(), graph);
    amr::Town town(world, graph, &junctions, 1, args.traffic, args.cops);
    town.add_heat(args.heat);
    amr::Hud hud(kInternalW, kInternalH);
    amr::MissionManager jobs(world, graph, town);
    const double start_x = sx, start_y = sy, start_heading = car.heading;
    amr::ChaseCamera chase(car);
    chase.update(car, world, 0.0, true);
    renderer.warm(sx, sy);
    double frame_ms_total = 0, frame_ms_worst = 0;
    int worst_frame = 0, worst_built = 0;
    long long tris_total = 0;

    Uint64 last = SDL_GetPerformanceCounter();
    const double freq = static_cast<double>(SDL_GetPerformanceFrequency());
    double acc = 0.0;
    int frame = 0;
    bool running = true;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) input.handle(e);
        amr::Controls c = input.poll();
        if (c.quit) break;
        amr::CarInput drive{c.throttle, c.brake, c.steer, c.handbrake, c.bumper};
        if (args.frames >= 0) drive = amr::CarInput{true, false, 0.0, false, false};  // drive itself, straight (CI)

        // fixed-timestep simulation; a frame-limited test run steps exactly one 30 fps frame each loop
        Uint64 now = SDL_GetPerformanceCounter();
        acc += args.frames >= 0 ? 1.0 / 30.0 : std::fmin(0.25, (now - last) / freq);
        last = now;
        while (acc >= 1.0 / kPhysicsHz) {
            const double dt = 1.0 / kPhysicsHz, px = car.x, py = car.y;  // as game/main.py steps the player
            if (town.state() != amr::TownState::Playing) drive = amr::CarInput{false, false, 0.0, true, false};
            car.power = town.player_power();
            car.update(dt, drive, world.on_road(car.x, car.y));
            const double impact = amr::move_and_slide(car, world, px, py, true);
            amr::settle_height(car, world, dt);
            town.update(dt, car, impact);
            jobs.update(dt, car);
            if (town.consume_reset()) {  // after BUSTED or WRECKED: back to the start, fresh car, quiet town
                car = amr::Car(start_x, start_y, start_heading, amr::player_model(0));
                car.z = world.height_at(static_cast<float>(start_x), static_cast<float>(start_y),
                                        world.ground(static_cast<float>(start_x), static_cast<float>(start_y)));
                town.reset();
                jobs.abort();
                chase.update(car, world, 0.0, true);
            }
            chase.update(car, world, dt);
            acc -= 1.0 / kPhysicsHz;
        }

        // draw the small picture
        const Uint64 t_frame = SDL_GetPerformanceCounter();
        target.bind();
        glClearColor(amr::palette::kFog[0] / 255.0f, amr::palette::kFog[1] / 255.0f, amr::palette::kFog[2] / 255.0f, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        const amr::Camera cam = chase.camera(0.88f);   // the street style's 80 degree view at 16:9
        const float cxf = static_cast<float>(car.x), cyf = static_cast<float>(car.y), czf = static_cast<float>(car.z);
        const amr::FrameStats st = renderer.draw(cam, static_cast<float>(kInternalW) / kInternalH);
        amr::Mat4 body = amr::translate({cxf, cyf, czf + static_cast<float>(car.hop_z) + 0.3f}) *
                         amr::rotate_z(static_cast<float>(car.heading));
        const float len = static_cast<float>(car.length), wid = static_cast<float>(car.width);
        body.at(0, 0) *= len; body.at(1, 0) *= len;          // the car's real size (player 1: 4.6 x 1.9 m)
        body.at(0, 1) *= wid; body.at(1, 1) *= wid;
        body.at(2, 2) = 1.65f;
        renderer.draw_box(body, amr::palette::kCarYellow);
        for (const auto& c : town.cars()) {  // traffic and police: boxes in their colours, for now
            amr::Mat4 m = amr::translate({static_cast<float>(c->x), static_cast<float>(c->y), static_cast<float>(c->z) + 0.3f}) *
                          amr::rotate_z(static_cast<float>(c->heading));
            const float l = static_cast<float>(c->length), w = static_cast<float>(c->width);
            m.at(0, 0) *= l; m.at(1, 0) *= l;
            m.at(0, 1) *= w; m.at(1, 1) *= w;
            m.at(2, 2) = 1.45f;
            renderer.draw_box(m, c->police ? amr::palette::kPoliceBody : amr::palette::kTraffic[c->colour]);
        }
        if (args.frames >= 0) {
            glFinish();  // honest timing in test runs: wait for the GPU
            const double ms = (SDL_GetPerformanceCounter() - t_frame) * 1000.0 / SDL_GetPerformanceFrequency();
            if (frame > 0) {  // the first frame builds the chunks around the start
                frame_ms_total += ms;
                if (ms > frame_ms_worst) {
                    frame_ms_worst = ms;
                    worst_frame = frame;
                    worst_built = st.chunks_built;
                }
            }
            tris_total += st.triangles;
        }

        for (const amr::Marker& mk : jobs.markers()) {  // job beacons: a glowing column and a pad on the ground
            const float mx = static_cast<float>(mk.x), my = static_cast<float>(mk.y);
            const float g = world.ground(mx, my);
            const std::uint8_t col[3] = {mk.colour.r, mk.colour.g, mk.colour.b};
            amr::Mat4 pad = amr::translate({mx, my, g + 0.05f});
            pad.at(0, 0) = pad.at(1, 1) = static_cast<float>(mk.radius) * 1.6f;
            pad.at(2, 2) = 0.08f;
            renderer.draw_box(pad, col);
            amr::Mat4 beam = amr::translate({mx, my, g});
            beam.at(0, 0) = beam.at(1, 1) = 0.8f;
            beam.at(2, 2) = mk.hub ? 14.0f : 30.0f;
            renderer.draw_box(beam, col);
        }
        hud.begin();  // speed, stars, damage, and the big BUSTED / WRECKED
        const std::string kmh = std::to_string(static_cast<int>(std::lround(car.speed() * 3.6))) + " KM/H";
        hud.text(6, kInternalH - 14, kmh, amr::palette::kHudText, 2);
        for (int k = 0; k < 4; ++k)
            hud.rect(kInternalW - 52 + k * 12, 6, 9, 9, k < town.stars() ? amr::palette::kHudStar : amr::palette::kHudStarOff);
        hud.text(kInternalW - 82, kInternalH - 13, "DMG", amr::palette::kHudText);
        hud.rect(kInternalW - 66, kInternalH - 14, 60, 7, amr::palette::kHudBar);
        hud.rect(kInternalW - 65, kInternalH - 13, static_cast<int>(58 * town.damage() / 100.0), 5, amr::palette::kHudDamage);
        hud.text(6, 6, "$" + std::to_string(jobs.cash()), amr::palette::kHudCash, 2);  // cash, in money green
        if (const amr::Job* j = jobs.job()) {  // the job: timer, objective, and an arrow to the target
            const int secs = std::max(0, static_cast<int>(std::ceil(j->time_left)));
            const std::string clock = std::to_string(secs / 60) + ":" + (secs % 60 < 10 ? "0" : "") + std::to_string(secs % 60);
            const std::uint8_t jc[3] = {j->colour.r, j->colour.g, j->colour.b};
            hud.text((kInternalW - hud.text_width(clock, 2)) / 2, 6, clock, secs < 15 ? amr::palette::kHudRed : amr::palette::kHudText, 2);
            const std::string obj = jobs.objective();
            hud.text((kInternalW - hud.text_width(obj)) / 2, 20, obj, jc);
            double tx, ty;
            if (jobs.current_target(tx, ty)) {
                const double a = amr::wrap_angle(std::atan2(ty - car.y, tx - car.x) - car.heading);  // + is to the right
                const float cx = kInternalW / 2.0f, cy = 38.0f, r = 8.0f;
                const float sa = static_cast<float>(std::sin(a)), ca = static_cast<float>(std::cos(a));
                hud.tri(cx + sa * r, cy - ca * r, cx - ca * 5 - sa * 4, cy - sa * 5 + ca * 4,
                        cx + ca * 5 - sa * 4, cy + sa * 5 + ca * 4, jc);
                const std::string dist = std::to_string(static_cast<int>(std::hypot(tx - car.x, ty - car.y))) + " M";
                hud.text((kInternalW - hud.text_width(dist)) / 2, 50, dist, amr::palette::kHudText);
            }
        } else {
            const std::string h = jobs.hint(car);
            if (!h.empty()) hud.text((kInternalW - hud.text_width(h)) / 2, kInternalH - 28, h, amr::palette::kHudText);
        }
        if (!jobs.banner().empty()) {
            const std::uint8_t bc[3] = {jobs.banner_colour().r, jobs.banner_colour().g, jobs.banner_colour().b};
            hud.text((kInternalW - hud.text_width(jobs.banner(), 2)) / 2, 66, jobs.banner(), bc, 2);
        }
        if (town.state() != amr::TownState::Playing) {
            const std::string big = town.state() == amr::TownState::Busted ? "BUSTED" : "WRECKED";
            hud.text((kInternalW - hud.text_width(big, 4)) / 2, 60, big, amr::palette::kHudRed, 4);
        }
        hud.end();

        int ww = 0, wh = 0;
        SDL_GL_GetDrawableSize(win, &ww, &wh);
        target.present(ww, wh);
        SDL_GL_SwapWindow(win);
        ++frame;
        if (args.frames >= 0 && frame >= args.frames) running = false;
    }
    int code = 0;
    if (args.frames > 1)
        std::printf("frames: %d, draw avg %.2f ms, worst %.2f ms (frame %d, %d chunks built), %lld triangles a frame, "
                    "%zu traffic cars\n", frame, frame_ms_total / (frame - 1), frame_ms_worst, worst_frame, worst_built,
                    tris_total / frame, town.cars().size());
    if (!args.screenshot.empty()) {
        if (target.save_bmp(args.screenshot)) {
            std::printf("wrote %s (car at %.1f, %.1f, %.1f)\n", args.screenshot.c_str(), car.x, car.y, car.z);
        } else {
            std::fprintf(stderr, "could not save %s: %s\n", args.screenshot.c_str(), SDL_GetError());
            code = 1;
        }
    }
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return code;
}
