# Amherst Run (native)

The C++ version of [Amherst Run](https://github.com/jeremylegere-wq/amherst-run), a retro open-world arcade driving
game set in Amherst, Nova Scotia. Homebrew targets: Xbox One in Developer Mode (UWP), Android, Batocera and desktops.
The Python game is the reference; the plan is in its `docs/CPP_PORT_PLAN.md`.

**Status: phase 1 under way.** The real map (terrain, roads, overpass decks, buildings, land use, water) drawn at
320x180 and scaled up with chunky pixels, and player 1's car (still drawn as a box) with the Python game's arcade
physics: grip, handbrake, hop, power slides and boosts, the hills' pull, overpass decks, walls you scrape along and
trees and posts you bounce off (all 455,000 of the map's trees, simply drawn). It matches the Python game to under 2 mm
on its golden drives. Phase 2 has begun: civilian traffic on the road graph (16 cars downtown, fewer on quiet roads),
driven like the Python game's, stopping at lights and stop signs, giving way, and bumping into you; police who chase
you when you hit traffic, the wanted stars, damage, BUSTED and WRECKED, and a small HUD. Phase 3 has begun: the jobs
(Parcel Run to real addresses, Getaway, Heritage Tour, paying fines) with glowing beacons, a timer and an arrow; a
start screen, a pause screen, split screen for 2-4 players (free roam, no police), and Battle and VS Race with the
six items; the freight train on the real rail line, and the synthesized sound effects. Phase 4 (the look) has begun:
the street style's sky, clouds and distant hills, cartoon cars, road paint, kerbs and brick or concrete sidewalks, ink
outlines, see-through shadows, the landmark props (the border lighthouse, the cenotaph, the dam, the horse), and the
minimap with the street-name and speed-limit signs.
Not yet: the cars' real shapes, landmarks,
the street style's detail. The Xbox and Android builds (phase 0) are still to do.

| Target | Status |
|---|---|
| Linux (desktop, Batocera) | builds and runs |
| Xbox One, Developer Mode (UWP) | in progress: GitHub's Windows runner builds SDL2 for UWP; the game's package next (see `platform/uwp/README.md`) |
| Android | CI builds a debug APK (artifact `amherst-run-android-debug`); not yet tried on a device (see `platform/android/README.md`) |
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
`tools/golden.py`). The `world` test checks this game's heights against the Python game's on every golden sample, and the `drive` test
replays the Python game's scripted drives (into a house, up an overpass, a handbrake turn, a power slide, a hill, into
a tree) and
checks the car follows the same path. The `traffic` test checks the road graph's A* routes and nearest nodes and
replays the Python game's traffic drivers (including at a junction with lights, a stop sign and an all-way stop),
then the police chases (boxing in, chasing, standing down) and the town's rules (heat, arrests, damage), then runs a
minute of town and checks the cars keep moving on the roads. The `jobs` test checks drop-point addresses, route lengths
and the Heritage Tours against the Python game, and that tours pay and banks start getaways. The `items` test checks the item boxes and the flight of pucks and seekers
against the Python game, and the battle rules (pops, rams, slicks, winning). The `train` test checks the train's blocks
and push-outs against the Python game and its timetable; `sound` checks the synthesized effects.

Controls (as the Python game): left stick or D-pad steers, up is gas, down brakes and reverses (RT/LT work too), A is
the handbrake, LB/RB hop (hold through a turn at speed to power-slide, let go to boost). Start or Back pauses (resume,
main menu, quit). Keyboard: arrows or WASD, Space handbrake, Shift hop, Esc or P pauses.
Split screen: player 1 drives with WASD + Space + Left Shift, player 2 with the arrows + Right Ctrl + Right Shift; pads
go to the last players first (one pad and two players: the pad drives player 2). Y (or R / Backspace) respawns.
`--no-train` / `--train-in S` turn the train off / bring it after S seconds. `--players N` skips the start screen (1 the full game, 2-4 split screen); `--battle balloon|race` starts a battle.
Battle: player 1 fires with E or Left Shift and hops with Space, player 2 fires with / or Right Ctrl and hops with
Right Shift or Enter; on a pad X or B fires and A or LB/RB hop. After a win, A / Start / Enter is a rematch.
`--heat H` starts wanted (0-4), `--no-cops` turns the police off, `--traffic N` sets the traffic.
`--at X,Y --heading DEG` starts at a map point (metres; the map's y grows south) facing a compass bearing.
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
