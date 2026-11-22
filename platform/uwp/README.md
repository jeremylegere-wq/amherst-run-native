# Xbox One (Developer Mode) / Windows package (UWP)

No Windows PC is needed: CI (`.github/workflows/ci.yml`, job `xbox`, on GitHub's Windows runner) builds the game
for UWP with SDL2 (from source) and ANGLE (Microsoft's ANGLE.WindowsStore 2.1.14 package: OpenGL ES 3 on
Direct3D 11), signs it with a test certificate made fresh for each build, and keeps the result as the artifact
`amherst-run-xbox-uwp`:

    AmherstRun_0.1.0.0_x64_Test/AmherstRun_0.1.0.0_x64.msix       the game (with the map)
    AmherstRun_0.1.0.0_x64_Test/AmherstRun_0.1.0.0_x64.cer        its test certificate
    AmherstRun_0.1.0.0_x64_Test/Dependencies/x64/Microsoft.VCLibs.x64.14.00.appx   the C++ runtime it needs

Not yet tried on a console.

## Installing it on an Xbox One

1. Developer Mode: install the "Xbox Dev Mode" app from the Store on the console, sign up for a Partner Center
   developer account to get an activation code, enter it, and restart into Developer Mode (Microsoft's
   "Xbox One Developer Mode activation" guide).
2. In Dev Home turn on Remote Access (the Device Portal) with a user name and password; it shows the address,
   e.g. `https://192.168.x.x:11443`.
3. On any computer's browser open that address (accept the console's own certificate warning), then
   Home -> My games & apps -> Add: pick the `.msix`, then add `Microsoft.VCLibs.x64.14.00.appx` as a dependency
   (and the `.cer` if it asks for a certificate). Install.
4. In Dev Home, select Amherst Run, press View -> View details, and set its type from App to **Game** (games get more
   memory and the larger share of the CPU and GPU). Launch it from Dev Home.

## Building it yourself (Windows, Visual Studio 2022 with the UWP C++ tools)

See the comment at the top of `CMakeLists.txt` and the CI job's steps. `make_logos.py` redraws the tile pictures.
