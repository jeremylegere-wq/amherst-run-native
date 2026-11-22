cmake_minimum_required(VERSION 3.16)
project(AmherstRunNative VERSION 0.1.0 LANGUAGES CXX)

# Amherst Run, native C++ version (see README.md). One dependency: SDL2 (approved). Graphics: OpenGL ES 3.0
# (system GLES on Linux/Batocera/Android; ANGLE.WindowsStore on Xbox UWP: platform/uwp).

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(SDL2 REQUIRED)
find_library(GLES3_LIB NAMES GLESv2 GLESv3 REQUIRED)

add_library(amr_core STATIC
  src/core/input.cpp
  src/core/log.cpp
  src/core/touch.cpp
  src/core/sound.cpp
  src/core/music.cpp
  src/render/target.cpp
  src/render/shader.cpp
  src/render/mesh.cpp
  src/render/map_renderer.cpp
  src/render/hud.cpp
  src/render/car_mesh.cpp
  src/render/minimap.cpp
  src/render/landmarks.cpp
  src/render/building_detail.cpp
  src/world/map_file.cpp
  src/world/world.cpp
  src/sim/car.cpp
  src/sim/collision.cpp
  src/sim/roadgraph.cpp
  src/sim/ai.cpp
  src/sim/junctions.cpp
  src/sim/town.cpp
  src/sim/train.cpp
  src/game/missions.cpp
  src/game/tagging.cpp
  src/game/save.cpp
  src/game/items.cpp
  src/game/battle.cpp
  src/game/app.cpp)
target_include_directories(amr_core PUBLIC src)
target_link_libraries(amr_core PUBLIC SDL2::SDL2 ${GLES3_LIB})
if(MSVC)
  target_compile_options(amr_core PRIVATE /W4)
else()
  target_compile_options(amr_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(amherst-run src/main.cpp)
target_link_libraries(amherst-run PRIVATE amr_core)
target_compile_definitions(amherst-run PRIVATE AMR_VERSION="${PROJECT_VERSION}")

enable_testing()
add_executable(test_math tests/test_math.cpp)
target_include_directories(test_math PRIVATE src)
add_test(NAME math COMMAND test_math)

add_executable(test_world tests/test_world.cpp)
target_link_libraries(test_world PRIVATE amr_core)
set(AMR_MAP "${CMAKE_SOURCE_DIR}/assets/amherst.amr" CACHE FILEPATH "baked map for the parity tests")
set(AMR_GOLDEN "${CMAKE_SOURCE_DIR}/assets/golden" CACHE PATH "golden files from amherst-run tools/golden.py")
add_test(NAME world COMMAND test_world "${AMR_MAP}" "${AMR_GOLDEN}/heights.csv")
set_tests_properties(world PROPERTIES SKIP_RETURN_CODE 77)

add_executable(test_drive tests/test_drive.cpp)
target_link_libraries(test_drive PRIVATE amr_core)
add_test(NAME drive COMMAND test_drive "${AMR_MAP}" "${AMR_GOLDEN}/drive.csv")
set_tests_properties(drive PROPERTIES SKIP_RETURN_CODE 77)

add_executable(test_traffic tests/test_traffic.cpp)
target_link_libraries(test_traffic PRIVATE amr_core)
add_test(NAME traffic COMMAND test_traffic "${AMR_MAP}" "${AMR_GOLDEN}")
set_tests_properties(traffic PROPERTIES SKIP_RETURN_CODE 77)

add_executable(test_jobs tests/test_jobs.cpp)
target_link_libraries(test_jobs PRIVATE amr_core)
add_test(NAME jobs COMMAND test_jobs "${AMR_MAP}" "${AMR_GOLDEN}")
set_tests_properties(jobs PROPERTIES SKIP_RETURN_CODE 77)

add_executable(test_items tests/test_items.cpp)
target_link_libraries(test_items PRIVATE amr_core)
add_test(NAME items COMMAND test_items "${AMR_MAP}" "${AMR_GOLDEN}")
set_tests_properties(items PROPERTIES SKIP_RETURN_CODE 77)

add_executable(test_train tests/test_train.cpp)
target_link_libraries(test_train PRIVATE amr_core)
add_test(NAME train COMMAND test_train "${AMR_MAP}" "${AMR_GOLDEN}")
set_tests_properties(train PROPERTIES SKIP_RETURN_CODE 77)

add_executable(test_sound tests/test_sound.cpp)
target_link_libraries(test_sound PRIVATE amr_core)
add_test(NAME sound COMMAND test_sound)

add_executable(test_touch tests/test_touch.cpp)
target_link_libraries(test_touch PRIVATE amr_core)
add_test(NAME touch COMMAND test_touch)

add_executable(test_tagging tests/test_tagging.cpp)
target_link_libraries(test_tagging PRIVATE amr_core)
add_test(NAME tagging COMMAND test_tagging "${AMR_MAP}")
set_tests_properties(tagging PROPERTIES SKIP_RETURN_CODE 77)

add_executable(test_save tests/test_save.cpp)
target_link_libraries(test_save PRIVATE amr_core)
add_test(NAME save COMMAND test_save "${CMAKE_CURRENT_BINARY_DIR}")

add_executable(test_music tests/test_music.cpp)
target_link_libraries(test_music PRIVATE amr_core)
add_test(NAME music COMMAND test_music)
