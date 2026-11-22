# Command line and controls reference

Everything here is optional: `amherst-run` with no options opens the start screen.

## Game options

| Option | What it does |
|---|---|
| `--players N` | Skip the start screen: `1` is the full game, `2`–`4` split-screen free roam. |
| `--battle balloon` \| `--battle race` | Skip the start screen into a two-player balloon battle or VS race. |
| `--map PATH` | Load another map file. The default is `assets/amherst.amr` beside the game. |
| `--traffic N` | Civilian cars around you downtown. The default is 16; `0` turns traffic off. |
| `--no-cops` | No police at all. |
| `--heat H` | Start wanted, at a level from 0 to 4. |
| `--no-train` \| `--train-in S` | No train, or bring the first train after `S` seconds (normally 50–90). |
| `--weather NAME` | `clear`, `overcast`, `fog`, `golden` or `snow`. F4 cycles them while playing. |
| `--quality low\|high` | Low quality draws 170 m instead of 260 m and leaves out the see-through shadows. |
| `--screen full\|safe` | Use the whole window, or keep a 5% border for TVs that crop their edges (the Xbox default). |
| `--touch` | Show the on-screen touch buttons from the start; on a desktop the mouse works them. |
| `--no-music` | Start with the music off. |
| `--no-save` | Neither load nor save progress. |
| `--save-dir DIR` | Keep the save file in `DIR` instead of the system's app-data folder. |
| `--at X,Y` `--heading DEG` | Start at a map point (metres; the map's y axis grows south), facing a compass bearing. |
| `--version` | Print the version and exit. |

Options given on the command line win over the choices stored in the save file.

## Test and capture options

These are for automated runs, benchmarks and recording footage.

| Option | What it does |
|---|---|
| `--frames N` | Run `N` frames at a fixed 30 fps step, then exit and print the frame timings. Without other options the car drives straight ahead. Test runs never touch the save file. |
| `--screenshot FILE.bmp` | With `--frames`: save the last 320×180 picture. Add `--menu` to picture the start screen. |
| `--record DIR` | With `--frames`: save every second frame (15 per second) as `DIR/frame_00000.bmp`, and so on. |
| `--autopilot` `--pace P` | Player 1 drives itself along the road graph like traffic, at `P` times the posted limits (default 1.25). |
| `--tag-test` | With `--frames`: park and hold TAG (screenshots of a tag being sprayed). |

Set `SDL_VIDEODRIVER=offscreen` (and `SDL_AUDIODRIVER=dummy`) to run without a display, as CI does.
[`tools/make_media.sh`](../tools/make_media.sh) uses these options to make the screenshots and GIFs in `docs/media`.

## Split screen and battle keys

| | Player 1 | Player 2 |
|---|---|---|
| Drive | W A S D | Arrows |
| Handbrake | Space | Right Ctrl or Keypad 0 |
| Hop | Left Shift | Right Shift |
| Respawn | R | Backspace |

Pads go to the *last* players first: with one pad and two players, the pad drives player 2. A pad's Y respawns its
player.

In a battle, player 1 fires with E or Left Shift and hops with Space; player 2 fires with `/` or Right Ctrl and hops
with Right Shift or Enter. On a pad, X or B fires and A, LB or RB hops. After a win, A, Start or Enter starts a rematch.
