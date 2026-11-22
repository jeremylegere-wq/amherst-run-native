# Amherst Run (native)

The C++ version of [Amherst Run](https://github.com/jeremylegere-wq/amherst-run), a retro open-world arcade driving
game set in Amherst, Nova Scotia. Homebrew targets: Xbox One in Developer Mode (UWP), Android, Batocera and desktops.
The Python game is the reference; the plan is in its `docs/CPP_PORT_PLAN.md`.

> **Untested on real hardware.** The Xbox and Android builds are made by CI and have never been played on a console
> or a phone. The Linux build runs on a desktop and passes its tests. Expect rough edges; reports (and the log, see
> below) are welcome.

## Download

Each [release](https://github.com/jeremylegere-wq/amherst-run-native/releases) has:

- `amherst-run-android-debug.apk`: Android 5.0+ (arm64, armv7, x86_64). Allow installs from unknown sources, open it.
- `amherst-run-xbox-uwp.zip`: Xbox One / Series in Developer Mode. Unzip; `INSTALL.txt` inside is the step-by-step
  guide (Dev Mode, the Device Portal, setting the app type to Game, the controls).

The latest builds of `main` are also on every CI run's page (Actions > CI > artifacts).

## What is in it

- **The real town**: the Python game's baked map of Amherst (terrain, roads, overpass decks, 6,600 buildings, land use,
  water, 455,000 trees, the rail line), drawn at 320x180 and scaled up with chunky pixels.
- **The Python game's driving**: grip, handbrake, hop, power slides and boosts, hills, decks, walls and trees; checked
  against the Python game's golden drives to under 2 mm.
- **A living town**: traffic on the road graph that stops at lights and stop signs and gives way; police, wanted stars,
  damage, BUSTED and WRECKED; the freight train.
- **The jobs**: Parcel Run to real civic addresses, Getaway, Heritage Tour, fines; split screen for 2-4 players, and
  Battle and VS Race with six items.
- **Tagging** (this game's own): 45 walls around town marked by spinning spray cans; pull up beside one and hold TAG
  (E, pad X, the touch TAG button) to spray your crew's tag. Blank walls pay $25; a rival crew holds about a third
  ($50 to cover) and goes over one of yours every 2.5 minutes; five big pieces take three sprays ($100). Police nearby
  raise the heat.
- **The street look**: cel shading with cool-tinted shadows, ink lines on every crease, see-through hard shadows,
  house details, shop fronts, steeples, the clock tower, graffiti and murals, puffy clouds, a sticker HUD, and the
  weather presets (`--weather clear|overcast|fog|golden|snow`, F4).
- **Music and sound made in code**: an original funky breakbeat with a chase layer while you are wanted, and
  synthesized effects. No sound files.
- **Phones and consoles**: touch buttons, Back pauses, pausing in the background, a quality setting, a TV-safe screen
  fit; progress (cash, walls, settings) is saved; a log for diagnosing problems.

| Target | Status |
|---|---|
| Linux (desktop) | builds, runs, tests pass |
| Batocera | should run (Linux, SDL2, GLES); untested |
| Xbox One / Series, Developer Mode (UWP) | CI builds a signed test package; **untested on a console** (`platform/uwp/INSTALL.txt`) |
| Android | CI builds a debug APK; **untested on a phone** (`platform/android/README.md`) |
| Windows, macOS desktop | should build with CMake; not tried |

If something goes wrong on a console or a phone, the game writes `log.txt` (and the run before, `log-previous.txt`)
in its data folder: on the Xbox, the Device Portal's file explorer, LocalAppData > AmherstRun > LocalState >
AmherstRun > AmherstRun.

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

## Licence and credits

The code is under the MIT License (LICENSE). The builds include SDL2 (zlib licence) and, in the Xbox package, ANGLE
(BSD licence): see THIRD_PARTY_NOTICES.md. All art, names, music and sounds in the game are original and made in code.

The map (`amherst.amr`, downloaded from the [amherst-run](https://github.com/jeremylegere-wq/amherst-run) releases, not
kept here) is built from:
map data (c) OpenStreetMap contributors, under the [Open Database Licence](https://opendatacommons.org/licenses/odbl/);
the Nova Scotia Road Network, the Nova Scotia Civic Address File, land cover and zoning (Province of Nova Scotia,
Open Government Licence - Nova Scotia); building heights from Natural Resources Canada's HRDEM LiDAR (NS-NSDNR_2020,
Open Government Licence - Canada); elevation from Terrain Tiles on AWS (Mapzen; NRCan CDEM and SRTM); and facts from
the Canadian Register of Historic Places. See the amherst-run README for the full credits.
