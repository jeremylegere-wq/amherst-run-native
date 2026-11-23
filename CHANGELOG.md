# Changelog

All notable changes to Amherst Run (native). The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)
and versions follow [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added
- Controller rumble: crashes (by how hard), boosts, drift charge, spraying and finished walls, BUSTED and WRECKED,
  the train. *Pause > Rumble* switches it off; the choice is saved.
- Prompts name the device you use last ("HOLD X TO TAG" on a controller, "HOLD E TO TAG" on a keyboard, the TAG
  button by touch); the start screen tells touch players to tap.
- The game pauses when a controller disconnects mid-game.
- Android: USB and Bluetooth controllers declared as optional hardware, low-latency display on TVs, and the tilt
  sensor is no longer exposed as a joystick.

## [0.1.1] - 2026-11-23

Still an **untested preview** on the Xbox and Android.

### Fixed
- The map is amherst-run v1.0.6: the North Nova Scotia Highlanders mural is on the wall facing Ratchford and
  Havelock Streets (it was on the Dominion Building), and graffiti no longer covers a mural.

### Added
- `--autopilot` (player 1 drives itself along the road graph like traffic) and `--record DIR` (save frames for GIFs).
- `tools/make_media.sh`, which remakes the README's screenshots and GIFs from the game.
- Documentation: a new README, [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) and [docs/COMMAND_LINE.md](docs/COMMAND_LINE.md).

## [0.1.0] - 2026-11-23

The first public build, an **untested preview**: the Xbox and Android builds have not yet been played on real hardware.

### Added
- The real town of Amherst from the Python game's baked map (amherst-run v1.0.5), drawn at 320×180 with OpenGL ES 3.
- The Python game's car physics, traffic, police, junction rules, train, jobs, split screen, battles and items, checked
  against its golden runs.
- Tagging: 45 walls, a rival crew that holds a third of them and strikes back, and five three-spray big pieces.
- The street look: ink lines on every crease, cool-tinted cel shadows, building details, graffiti and murals, puffy
  clouds, a sticker HUD, and five weather presets.
- Original music and sound effects, synthesized in code.
- Saved progress, touch controls, a quality setting, a TV-safe screen fit, and a log on the device.
- An Android APK and a signed Xbox (UWP) package, built in CI.

[Unreleased]: https://github.com/jeremylegere-wq/amherst-run-native/compare/v0.1.1...HEAD
[0.1.1]: https://github.com/jeremylegere-wq/amherst-run-native/compare/v0.1.0...v0.1.1
[0.1.0]: https://github.com/jeremylegere-wq/amherst-run-native/releases/tag/v0.1.0
