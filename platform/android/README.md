# Android build - not set up yet

Phase 0 still needs this. What it takes (works on Linux):

1. Android SDK + NDK (Android Studio, or the command-line tools) and a JDK.
2. SDL2's `android-project` template (in the SDL2 source): copy it here, point its `jni/` at SDL2's source and this
   repository's `src/`, set the app id (for example `ca.amherstrun.game`) and landscape orientation.
3. `./gradlew assembleDebug` builds an APK to sideload; a pad (Bluetooth or USB) works through SDL's GameController.
   Touch controls come in phase 5.
