# Changelog

All notable changes to Amherst Run (native). The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)
and versions follow [Semantic Versioning](https://semver.org/).

## [Unreleased]

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

[Unreleased]: https://github.com/jeremylegere-wq/amherst-run-native/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/jeremylegere-wq/amherst-run-native/releases/tag/v0.1.0
