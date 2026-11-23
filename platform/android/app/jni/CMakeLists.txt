# The native part of the Android app: SDL2 from its source (SDL_DIR) as libSDL2.so, and the game as libmain.so
# (SDL's Java activity loads both and calls the game's main()). OpenGL ES 3 comes with Android.
cmake_minimum_required(VERSION 3.16)
project(AmherstRunAndroid LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(${SDL_DIR} SDL2-build EXCLUDE_FROM_ALL)

set(GAME_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../../../src)
file(GLOB_RECURSE GAME_SOURCES ${GAME_SRC}/*.cpp)
add_library(main SHARED ${GAME_SOURCES})
target_include_directories(main PRIVATE ${GAME_SRC})
target_compile_definitions(main PRIVATE AMR_VERSION="0.1.2-android")
target_link_libraries(main PRIVATE SDL2::SDL2 GLESv3 EGL log android)
