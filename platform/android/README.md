# Android build

CI builds a debug APK on every push (`.github/workflows/ci.yml`, job `android`); download it from the run's
artifacts (`amherst-run-android-debug`). It holds the game and SDL2 for arm64, armv7 and x86_64 phones and the baked
map. **Untested on a phone**: nobody has played it on one yet.

Controls: touch the screen and the on-screen buttons appear (steer left / right with the left thumb; GAS, BRAKE,
HAND, HOP and TAG with the right; PAUSE top left); tap a menu item to pick it. A Bluetooth or USB gamepad works too (the
buttons hide while it is used). The phone's Back button pauses. Leaving the app pauses the game and its sound. On a
slow phone pick QUALITY: LOW in the pause menu (170 m draw distance, no see-through shadows).

To install it on a phone: allow installs from unknown sources (or turn on USB debugging and use
`adb install app-debug.apk`), then open the APK.

To build it yourself (Linux, macOS or Windows): the Android SDK with an NDK and CMake, a JDK 17 and Gradle 8.7.

    python3 tools/fetch_map.py                                   # then copy assets/amherst.amr to app/assets/
    # unpack SDL2-2.32.10's source release as platform/android/SDL2
    cd platform/android && gradle assembleDebug -PndkVersion=<ndk folder name> -PcmakeVersion=<cmake folder name>

The project: `app/build.gradle` (Gradle), `app/jni/CMakeLists.txt` (SDL2 from source as libSDL2.so, the game as
libmain.so), `AmherstRunActivity` (SDL's activity: landscape, full screen, OpenGL ES 3.0).
