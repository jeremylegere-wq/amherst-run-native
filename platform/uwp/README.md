# Xbox One (Developer Mode) build - in progress

No Windows PC is needed: GitHub's Windows runners build it, and the package is installed from a web browser
through the Xbox Device Portal.

What CI has shown (job `windows-uwp-probe`): the runner has Visual Studio Enterprise 2022 with the UWP workload and
Windows SDKs up to 10.0.26100, and SDL2 2.32.10's `VisualC-WinRT/SDL-UWP.vcxproj` builds for x64.

Still to do:
1. ANGLE (OpenGL ES on Direct3D 11), which SDL2's UWP port uses for OpenGL ES. It is a separate library: it needs
   the project owner's OK before it is added.
2. A UWP app project for the game (`Package.appxmanifest` declaring a Game, so the console gives it up to 5 GB of
   memory and the larger CPU and GPU share), built and signed in CI, the package kept as an artifact.
3. On the console: a Partner Center developer account, the Xbox Dev Mode app, then upload the package (and its
   test certificate if asked) in the Device Portal and, in Dev Home, set the app's type to Game.
