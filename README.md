# Amherst Run (native)

The C++ version of [Amherst Run](https://github.com/jeremylegere-wq/amherst-run), a retro open-world arcade driving
game set in Amherst, Nova Scotia. Homebrew targets: Xbox One in Developer Mode (UWP), Android, Batocera and desktops.
The Python game is the reference; the plan is in its `docs/CPP_PORT_PLAN.md`.

**Status: phase 1 under way.** The real map (terrain, roads, overpass decks, buildings, land use, water) drawn at
320x180 and scaled up with chunky pixels, and a placeholder box you drive over it with a pad or the keyboard: it
follows the ground and the decks, but has no proper car physics or collisions yet. The Xbox and Android builds (phase 0)
are still to do.

| Target | Status |
|---|---|
| Linux (desktop, Batocera) | builds and runs |
| Xbox One, Developer Mode (UWP) | not built yet: needs Windows + Visual Studio 2022 (see `platform/uwp/README.md`) |
| Android | not built yet: needs the Android SDK + NDK (see `platform/android/README.md`) |
| Windows, macOS desktop | should build with CMake; not tried yet |

## Build and run (Linux)

Needs a C++17 compiler, CMake 3.16+, SDL2 (2.30 or newer) and OpenGL ES 3 headers:

```bash
sudo apt install build-essential cmake ninja-build libsdl2-dev libgles-dev libegl-dev
python3 tools/fetch_map.py
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build
./build/amherst-run
```

The map is not in this repository. `tools/fetch_map.py` downloads `amherst.amr` and the golden test files from the
[amherst-run](https://github.com/jeremylegere-wq/amherst-run) release named in `MAP_VERSION` (checking their SHA-256),
or copies a local bake with `--from-dir ../amherst-run/dist` (made there by `tools/bake_native.py` and
`tools/golden.py`). The `world` test checks this game's heights against the Python game's on every golden sample.

Controls: left stick steers, RT gas, LT brake/reverse, Back quits (keyboard: arrows or WASD, Esc).
`--at X,Y --heading DEG` starts at a map point (metres; heading 0 is east, 90 north).
`--frames 120 --screenshot out.bmp` drives itself, saves the 320x180 picture and exits; with
`SDL_VIDEODRIVER=offscreen` it needs no display (CI uses this).

## Rules

- One dependency: **SDL2** (approved). Anything else needs the project owner's OK first.
- Graphics are **OpenGL ES 3.0** only (ANGLE turns it into Direct3D 11 on Xbox UWP).
- Controller buttons by name through SDL's GameController API, never raw button numbers.
- The map comes baked from the Python repository (`MAP_VERSION` names the release); world-building code is not
  ported. Everything original: no art, names or sounds from other games.

## Licence

MIT (see LICENSE). Map data (c) OpenStreetMap contributors, and the open data sources credited in the Python game.
