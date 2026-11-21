// Amherst Run (native): the real Amherst map (baked by amherst-run's tools/bake_native.py) drawn at 320x180 and
// scaled up with chunky pixels, and a placeholder car you drive over it with a pad or the keyboard. The car follows
// the ground and the overpass decks; the real arcade physics and collisions come next (docs: amherst-run
// docs/CPP_PORT_PLAN.md, phase 1).
//
//   amherst-run                                  play (pad: left stick steer, RT gas, LT brake/reverse, Back quits)
//   amherst-run --map path/to/amherst.amr        another map file (default: assets/amherst.amr beside the game)
//   amherst-run --at X,Y [--heading DEG]         start at this map point (metres), facing DEG (0 east, 90 north)
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
#include "render/map_renderer.h"
#include "render/palette.h"
#include "render/target.h"
#include "world/map_file.h"
#include "world/world.h"

#ifndef AMR_VERSION
#define AMR_VERSION "dev"
#endif

namespace {

constexpr int kInternalW = 320, kInternalH = 180;  // the Python game's game/config.py INTERNAL_W/H
constexpr double kPhysicsHz = 60.0;                 // fixed timestep, as PHYSICS_HZ

// A placeholder car: just enough to feel the pad. Phase 1 ports the real arcade physics (game/car.py).
struct TestCar {
    float x = 0, y = 0, z = 0, heading = 0, speed = 0;
    void step(const amr::Controls& c, float dt, const amr::World& w) {
        const float accel = 9.0f, brake = 14.0f, drag = 0.35f, max_rev = -6.0f;
        speed += c.throttle * accel * dt;
        speed -= c.brake * (speed > 0 ? brake : accel * 0.5f) * dt;
        speed -= speed * drag * dt;
        if (speed < max_rev) speed = max_rev;
        float grip = std::fmin(1.0f, std::fabs(speed) / 6.0f);           // no turning on the spot
        heading -= c.steer * 1.9f * grip * dt * (speed >= 0 ? 1.0f : -1.0f);
        const float nx = x + std::cos(heading) * speed * dt, ny = y + std::sin(heading) * speed * dt;
        if (!w.in_bounds(nx, ny)) {  // the map edge stops you
            speed = 0;
            return;
        }
        x = nx;
        y = ny;
        z = w.height_at(x, y, z + 1.0f);  // on the ground, or on the deck you are driving on
    }
};

struct Args {
    int frames = -1;
    std::string screenshot, map;
    bool at = false, has_heading = false;
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
        } else if (!std::strcmp(argv[i], "--map") && i + 1 < argc) {
            a.map = argv[++i];
        } else if (!std::strcmp(argv[i], "--at") && i + 1 < argc) {
            a.at = std::sscanf(argv[++i], "%f,%f", &a.at_x, &a.at_y) == 2;
            if (!a.at) return false;
        } else if (!std::strcmp(argv[i], "--heading") && i + 1 < argc) {
            a.heading = static_cast<float>(std::atof(argv[++i]) * 3.14159265358979 / 180.0);  // degrees from east
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
    std::printf("Amherst Run (native) %s: %s\n", AMR_VERSION, reinterpret_cast<const char*>(glGetString(GL_VERSION)));

    std::string err;
    amr::MapData map;
    const std::string map_path = args.map.empty() ? default_map() : args.map;
    const Uint64 t_load = SDL_GetPerformanceCounter();
    if (!amr::load_map(map_path, map, err)) {
        std::fprintf(stderr, "map: %s\n(bake it with amherst-run's tools/bake_native.py, or get it with "
                             "tools/fetch_map.py)\n", err.c_str());
        return 1;
    }
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
    TestCar car;
    car.x = args.at ? args.at_x : world.map().spawn_x;
    car.y = args.at ? args.at_y : world.map().spawn_y;
    car.heading = args.has_heading ? args.heading : world.map().spawn_heading;
    car.z = world.height_at(car.x, car.y, world.ground(car.x, car.y));
    double frame_ms_total = 0, frame_ms_worst = 0;
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
        if (args.frames >= 0) c = amr::Controls{0.0f, 1.0f, 0.0f, false, false};    // drive itself, straight (CI)

        // fixed-timestep simulation; a frame-limited test run steps exactly one 30 fps frame each loop
        Uint64 now = SDL_GetPerformanceCounter();
        acc += args.frames >= 0 ? 1.0 / 30.0 : std::fmin(0.25, (now - last) / freq);
        last = now;
        while (acc >= 1.0 / kPhysicsHz) {
            car.step(c, static_cast<float>(1.0 / kPhysicsHz), world);
            acc -= 1.0 / kPhysicsHz;
        }

        // draw the small picture
        const Uint64 t_frame = SDL_GetPerformanceCounter();
        target.bind();
        glClearColor(amr::palette::kFog[0] / 255.0f, amr::palette::kFog[1] / 255.0f, amr::palette::kFog[2] / 255.0f, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        amr::Camera cam;
        const float back = 9.0f;
        cam.eye = {car.x - std::cos(car.heading) * back, car.y - std::sin(car.heading) * back, car.z + 3.6f};
        cam.eye.z = std::fmax(cam.eye.z, world.height_at(cam.eye.x, cam.eye.y, cam.eye.z) + 1.5f);  // never in a hill
        cam.target = {car.x, car.y, car.z + 1.2f};
        const amr::FrameStats st = renderer.draw(cam, static_cast<float>(kInternalW) / kInternalH);
        amr::Mat4 body = amr::translate({car.x, car.y, car.z + 0.05f}) * amr::rotate_z(car.heading);
        body.at(0, 0) *= 4.2f; body.at(1, 0) *= 4.2f;        // 4.2 x 1.9 x 1.6 m: the yellow SUV's size
        body.at(0, 1) *= 1.9f; body.at(1, 1) *= 1.9f;
        body.at(2, 2) = 1.6f;
        renderer.draw_box(body, amr::palette::kCarYellow);
        if (args.frames >= 0) {
            glFinish();  // honest timing in test runs: wait for the GPU
            const double ms = (SDL_GetPerformanceCounter() - t_frame) * 1000.0 / SDL_GetPerformanceFrequency();
            if (frame > 0) {  // the first frame builds the chunks around the start
                frame_ms_total += ms;
                frame_ms_worst = std::fmax(frame_ms_worst, ms);
            }
            tris_total += st.triangles;
        }

        int ww = 0, wh = 0;
        SDL_GL_GetDrawableSize(win, &ww, &wh);
        target.present(ww, wh);
        SDL_GL_SwapWindow(win);
        ++frame;
        if (args.frames >= 0 && frame >= args.frames) running = false;
    }
    int code = 0;
    if (args.frames > 1)
        std::printf("frames: %d, draw avg %.2f ms, worst %.2f ms, %lld triangles a frame\n", frame, frame_ms_total / (frame - 1),
                    frame_ms_worst, tris_total / frame);
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
