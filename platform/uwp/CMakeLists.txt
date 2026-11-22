# The Xbox One (Developer Mode) / Windows UWP package. Configure with the Visual Studio generator for WindowsStore:
#   cmake -S platform/uwp -B build-uwp -G "Visual Studio 17 2022" -A x64 -DCMAKE_SYSTEM_NAME=WindowsStore
#         -DCMAKE_SYSTEM_VERSION=10.0 -DSDL_DIR=<SDL2 source> -DSDL_LIB_DIR=<SDL-UWP build output>
#         -DANGLE_DIR=<unpacked ANGLE.WindowsStore nupkg> -DMAP_FILE=<amherst.amr>
# SDL2 is built from its own VisualC-WinRT project; ANGLE (OpenGL ES 3 on Direct3D 11) is Microsoft's prebuilt
# ANGLE.WindowsStore package. CI does all of this (.github/workflows/ci.yml, job "xbox").
cmake_minimum_required(VERSION 3.20)
project(AmherstRunUWP LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "WindowsStore")
  message(FATAL_ERROR "configure with -DCMAKE_SYSTEM_NAME=WindowsStore -DCMAKE_SYSTEM_VERSION=10.0")
endif()
foreach(v SDL_DIR SDL_LIB_DIR ANGLE_DIR MAP_FILE)
  if(NOT ${v})
    message(FATAL_ERROR "set -D${v}=...")
  endif()
  file(TO_CMAKE_PATH "${${v}}" ${v})   # forward slashes (Windows paths break CMake's source properties)
endforeach()
set(AMR_VERSION "0.1.1-xbox" CACHE STRING "version shown by the game")
# the package's version: CI gives each build a higher one (0.1.<run>.0) so a new build installs over the last
set(AMR_PKG_VERSION "0.1.0.0" CACHE STRING "package version (four numbers)")
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/Package.appxmanifest.in ${CMAKE_CURRENT_BINARY_DIR}/Package.appxmanifest @ONLY)

set(GAME_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../src)
file(GLOB_RECURSE GAME_SOURCES ${GAME_SRC}/*.cpp)

# SDL's UWP entry point (WinMain -> SDL_WinRTRunApp -> the game's main); it has to be C++/CX (/ZW)
set(WINRT_MAIN ${SDL_DIR}/src/main/winrt/SDL_winrt_main_NonXAML.cpp)
set_source_files_properties(${WINRT_MAIN} PROPERTIES COMPILE_FLAGS "/ZW")

# what goes into the package beside the .exe
set(ANGLE_BIN ${ANGLE_DIR}/bin/UAP/x64)
set(RUNTIME_DLLS ${SDL_LIB_DIR}/SDL2.dll ${ANGLE_BIN}/libEGL.dll ${ANGLE_BIN}/libGLESv2.dll)
set_source_files_properties(${RUNTIME_DLLS} PROPERTIES VS_DEPLOYMENT_CONTENT 1)
file(GLOB TILES ${CMAKE_CURRENT_SOURCE_DIR}/Assets/*.png)
set_source_files_properties(${TILES} PROPERTIES VS_DEPLOYMENT_CONTENT 1 VS_DEPLOYMENT_LOCATION Assets)
set_source_files_properties(${MAP_FILE} PROPERTIES VS_DEPLOYMENT_CONTENT 1 VS_DEPLOYMENT_LOCATION assets)

add_executable(AmherstRun WIN32 ${GAME_SOURCES} ${WINRT_MAIN} ${CMAKE_CURRENT_BINARY_DIR}/Package.appxmanifest
               ${TILES} ${MAP_FILE} ${RUNTIME_DLLS})
target_include_directories(AmherstRun PRIVATE ${GAME_SRC} ${SDL_DIR}/include ${ANGLE_DIR}/Include)
# (GL_GLEXT_PROTOTYPES: ANGLE.WindowsStore's older GLES headers declare the functions only with it)
target_compile_definitions(AmherstRun PRIVATE AMR_VERSION="${AMR_VERSION}" _CRT_SECURE_NO_WARNINGS
                           GL_GLEXT_PROTOTYPES)
target_compile_options(AmherstRun PRIVATE /W3 /EHsc)
target_link_libraries(AmherstRun PRIVATE ${SDL_LIB_DIR}/SDL2.lib ${ANGLE_BIN}/libGLESv2.lib ${ANGLE_BIN}/libEGL.lib)
# only SDL's entry point is C++/CX, so the C++/CX runtime has to be named before the C runtime by hand
target_link_options(AmherstRun PRIVATE /NODEFAULTLIB:vccorlib.lib /NODEFAULTLIB:msvcrt.lib)
target_link_libraries(AmherstRun PRIVATE vccorlib.lib msvcrt.lib)
