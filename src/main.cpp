// Amherst Run (native): the real Amherst map (baked by amherst-run's tools/bake_native.py) drawn at 320x180 and
// scaled up with chunky pixels, with the Python game's physics, traffic, police and jobs, a start screen, pause and
// split screen for 2-4 players (docs: amherst-run docs/CPP_PORT_PLAN.md).
//
//   amherst-run                                  the start screen (pad: stick/D-pad + A; Start or Back pauses)
//   amherst-run --players N                      skip the start screen: 1 = the full game, 2-4 = split screen
//   amherst-run --map path/to/amherst.amr        another map file (default: assets/amherst.amr beside the game)
//   amherst-run --traffic N                      civilian cars around you downtown (default 16, 0 for none)
//   amherst-run --no-cops | --heat H             no police at all | start with a wanted level (0-4)
//   amherst-run --at X,Y [--heading DEG]         start at this map point (metres), facing a compass bearing
//   amherst-run --frames 120 --screenshot out.bmp   drive itself for 120 frames, save the picture, report timing
//                                                   (add --menu to picture the start screen instead)
#include <SDL.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "game/app.h"
#include "render/gl.h"
#include "sim/junctions.h"
#include "sim/roadgraph.h"
#include "world/map_file.h"
#include "world/world.h"

#ifndef AMR_VERSION
#define AMR_VERSION "dev"
#endif

namespace {

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

bool parse(int argc, char** argv, amr::Options& a, std::string& map) {
    for (int i = 1; i < argc; ++i) {
        const char* s = argv[i];
        const bool more = i + 1 < argc;
        if (!std::strcmp(s, "--version")) {
            std::printf("Amherst Run (native) %s\n", AMR_VERSION);
            std::exit(0);
        } else if (!std::strcmp(s, "--frames") && more) {
            a.frames = std::atoi(argv[++i]);
        } else if (!std::strcmp(s, "--screenshot") && more) {
            a.screenshot = argv[++i];
        } else if (!std::strcmp(s, "--players") && more) {
            a.players = std::atoi(argv[++i]);
            if (a.players < 1 || a.players > 4) return false;
        } else if (!std::strcmp(s, "--menu")) {
            a.menu = true;
        } else if (!std::strcmp(s, "--no-cops")) {
            a.cops = false;
        } else if (!std::strcmp(s, "--heat") && more) {
            a.heat = std::atof(argv[++i]);
        } else if (!std::strcmp(s, "--traffic") && more) {
            a.traffic = std::atoi(argv[++i]);
        } else if (!std::strcmp(s, "--map") && more) {
            map = argv[++i];
        } else if (!std::strcmp(s, "--at") && more) {
            a.at = std::sscanf(argv[++i], "%lf,%lf", &a.at_x, &a.at_y) == 2;
            if (!a.at) return false;
        } else if (!std::strcmp(s, "--heading") && more) {
            const double b = std::atof(argv[++i]) * 3.14159265358979 / 180.0;  // compass bearing: 0 north, 90 east
            a.heading = std::atan2(-std::cos(b), std::sin(b));                    // map y grows south
            a.has_heading = true;
        } else {
            std::fprintf(stderr, "unknown option: %s\n", s);
            return false;
        }
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    amr::Options opt;
    std::string map_path;
    if (!parse(argc, argv, opt, map_path)) return 2;
    if (map_path.empty()) map_path = default_map();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    const Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    SDL_Window* win = SDL_CreateWindow("Amherst Run", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, amr::App::kW * 3,
                                       amr::App::kH * 3, flags);
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
    int code = 1;
    {
        std::string err;
        amr::MapData map;
        const Uint64 t_load = SDL_GetPerformanceCounter();
        if (!amr::load_map(map_path, map, err)) {
            std::fprintf(stderr, "map: %s\n(bake it with amherst-run's tools/bake_native.py, or get it with "
                                 "tools/fetch_map.py)\n", err.c_str());
        } else {
            amr::RoadGraph graph(map);
            amr::World world(std::move(map));
            amr::Junctions junctions(world.map(), graph);
            std::printf("map %s: %zu roads, %zu buildings, %zu overpass decks (%s), loaded in %.2f s\n",
                        map_path.c_str(), world.map().roads.size(), world.map().buildings.size(),
                        world.map().spans.size(), world.map().source.c_str(),
                        (SDL_GetPerformanceCounter() - t_load) / static_cast<double>(SDL_GetPerformanceFrequency()));
            amr::App app(win, world, graph, junctions, opt);
            if (!app.ok()) std::fprintf(stderr, "could not set up the renderer\n");
            else code = app.run();
        }
    }
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return code;
}
