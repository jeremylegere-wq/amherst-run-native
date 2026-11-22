# Xbox (Developer Mode)

> [!WARNING]
> **Untested on a console.** The package is built and signed by CI but has not yet been played on an Xbox.

Amherst Run runs on an Xbox One or Series console in **Developer Mode** as a homebrew UWP app. You don't need a
Windows PC: GitHub's Windows runner builds the package, and you install it from any computer's web browser through
the console's Device Portal.

## Install

Download `amherst-run-xbox-uwp.zip` from the [latest release](https://github.com/jeremylegere-wq/amherst-run-native/releases),
unzip it, and follow [`INSTALL.txt`](INSTALL.txt), which is also inside the zip. In short:

1. **Developer Mode.** Install the *Xbox Dev Mode* app from the Store and activate it with a code from a Partner Center
   developer account. The console restarts into Dev Home.
2. **Device Portal.** In Dev Home, turn on Remote Access and set a user name and password, then open the address it
   shows in a browser.
3. **Add the game.** Go to *My games & apps > Add*, choose the `.msix`, and add
   `Microsoft.VCLibs.x64.14.00.appx` as a dependency.
4. **Make it a Game.** In Dev Home, open the app's details and set its type to **Game**. Games get much more memory
   and a bigger share of the CPU and GPU than apps.

Each build has a higher package version (`0.1.<CI run>.0`), so a newer build installs over an older one. If the console
refuses an update, uninstall the old version in the Device Portal first.

## What is in the package

| File | Purpose |
|---|---|
| `AmherstRun.exe` | The game, built as a UWP app. |
| `SDL2.dll` | SDL2 2.32.10, built from source with its UWP project. |
| `libEGL.dll`, `libGLESv2.dll` | Microsoft's ANGLE for Windows Store 2.1.14: OpenGL ES 3 on Direct3D 11. |
| `assets/amherst.amr` | The baked map. |

## Made for the console

- **TV-safe picture.** A 5% border by default, so a TV that crops its edges keeps the HUD. Change it under
  *Pause > Screen*; the choice is saved.
- **Controller first.** Everything works from the pad, and B is the game's own back button, not the console's.
- **A log on the console.** `log.txt` records the graphics driver, the screen size, the connected pads and any error.
  It's in the Device Portal's file explorer under *LocalAppData > AmherstRun_… > LocalState > AmherstRun >
  AmherstRun*, next to the save file. Start-up failures also show a message box.

## Building it

CI does this on every push: the `xbox` job in [`.github/workflows/ci.yml`](../../.github/workflows/ci.yml). On a
Windows machine with Visual Studio 2022 and its UWP C++ tools, follow the same steps:

1. Build `SDL2-2.32.10/VisualC-WinRT/SDL-UWP.vcxproj` (Release, x64).
2. Unpack the `ANGLE.WindowsStore` 2.1.14 NuGet package.
3. Configure this folder with the WindowsStore generator:

   ```bat
   cmake -S platform/uwp -B build-uwp -G "Visual Studio 17 2022" -A x64 ^
     -DCMAKE_SYSTEM_NAME=WindowsStore -DCMAKE_SYSTEM_VERSION=10.0 ^
     -DSDL_DIR=<SDL2 source> -DSDL_LIB_DIR=<SDL-UWP output> -DANGLE_DIR=<unpacked ANGLE> -DMAP_FILE=<amherst.amr>
   ```

4. Build `build-uwp/AmherstRun.vcxproj` with MSBuild, setting `AppxPackageSigningEnabled` and your certificate.

`make_logos.py` redraws the tile and splash pictures in `Assets/`.
