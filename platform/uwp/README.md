# Xbox One (Developer Mode) build - not set up yet

Phase 0 still needs this. What it takes:

1. A Windows 10/11 PC with **Visual Studio 2022** and the *Universal Windows Platform development* and
   *Desktop development with C++* workloads.
2. **SDL2 source** (2.30.x, the last SDL line with UWP/WinRT support; SDL3 dropped it). Its `VisualC-WinRT` project
   builds SDL2 for UWP; SDL's `docs/README-winrt.md` explains the setup, including **ANGLE** for OpenGL ES.
3. A UWP app project here (`AmherstRun.vcxproj` + `Package.appxmanifest`) that compiles `src/` with SDL2-WinRT.
   The manifest must declare the app a **Game** so Xbox gives it the game resources (up to 5 GB of memory, the larger
   CPU and GPU share) rather than the 1 GB app limit.
4. The console: a Partner Center developer account, the *Xbox Dev Mode* app to switch the console to Developer Mode,
   then deploy from Visual Studio or upload the `.appx`/`.msix` in the Device Portal. In Dev Home, set the app's type
   to **Game**.

CI can build the package on GitHub's Windows runners once the project exists.
