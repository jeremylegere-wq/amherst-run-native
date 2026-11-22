# Android build

CI builds a debug APK on every push (`.github/workflows/ci.yml`, job `android`); download it from the run's
artifacts (`amherst-run-android-debug`). It holds the game and SDL2 for arm64, armv7 and x86_64 phones and the baked
map. Not yet tried on a device. No touch controls yet (phase 5): play it with a Bluetooth or USB gamepad.

To install it on a phone: allow installs from unknown sources (or turn on USB debugging and use
`adb install app-debug.apk`), then open the APK.

To build it yourself (Linux, macOS or Windows): the Android SDK with an NDK and CMake, a JDK 17 and Gradle 8.7.

    python3 tools/fetch_map.py                                   # then copy assets/amherst.amr to app/assets/
    # unpack SDL2-2.32.10's source release as platform/android/SDL2
    cd platform/android && gradle assembleDebug -PndkVersion=<ndk folder name> -PcmakeVersion=<cmake folder name>

The project: `app/build.gradle` (Gradle), `app/jni/CMakeLists.txt` (SDL2 from source as libSDL2.so, the game as
libmain.so), `AmherstRunActivity` (SDL's activity: landscape, full screen, OpenGL ES 3.0).
