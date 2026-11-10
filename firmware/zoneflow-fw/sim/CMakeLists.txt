# Desktop simulator of the ZoneFlow display: the same UI logic and LVGL view
# as the firmware, on the real controller core with fake hardware.
#
#   ./sim/run.sh                       interactive window (needs SDL2)
#   ./sim/run.sh --screenshots <dir>   headless, writes a PNG of every screen
cmake_minimum_required(VERSION 3.24)
project(zoneflow_sim LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

include(FetchContent)

# LVGL: the version pinned for the firmware in
# components/zf_display/idf_component.yml. Point ZF_LVGL_DIR at an existing
# copy (for example ../managed_components/lvgl__lvgl after `idf.py build`) to
# skip the download.
set(ZF_LVGL_VERSION 9.6.0)
set(ZF_LVGL_DIR "" CACHE PATH "Existing LVGL ${ZF_LVGL_VERSION} source tree")
if(ZF_LVGL_DIR)
    set(lvgl_src_SOURCE_DIR "${ZF_LVGL_DIR}")
else()
    FetchContent_Declare(lvgl_src
        URL https://github.com/lvgl/lvgl/archive/refs/tags/v${ZF_LVGL_VERSION}.tar.gz
        SOURCE_SUBDIR do-not-add)
    FetchContent_MakeAvailable(lvgl_src)
endif()
if(NOT EXISTS "${lvgl_src_SOURCE_DIR}/lvgl.h")
    message(FATAL_ERROR "No LVGL source tree at ${lvgl_src_SOURCE_DIR}")
endif()
# A tree given by hand must be the pinned version, or the simulator would
# draw with a different LVGL than the firmware.
file(STRINGS "${lvgl_src_SOURCE_DIR}/include/lvgl/lv_version.h" lvgl_version_lines
     REGEX "#define LVGL_VERSION_(MAJOR|MINOR|PATCH) ")
string(REGEX REPLACE "[^0-9;]" "" lvgl_version_numbers "${lvgl_version_lines}")
string(REPLACE ";" "." lvgl_found_version "${lvgl_version_numbers}")
if(NOT lvgl_found_version STREQUAL ZF_LVGL_VERSION)
    message(FATAL_ERROR "LVGL at ${lvgl_src_SOURCE_DIR} is ${lvgl_found_version}, need ${ZF_LVGL_VERSION}")
endif()

# LVGL's own CMake builds demos and examples too; the library alone is the
# C files under src/, configured by sim/lv_conf.h.
file(GLOB_RECURSE LVGL_SOURCES CONFIGURE_DEPENDS "${lvgl_src_SOURCE_DIR}/src/*.c")
add_library(lvgl STATIC ${LVGL_SOURCES})
target_include_directories(lvgl PUBLIC "${lvgl_src_SOURCE_DIR}" "${CMAKE_CURRENT_LIST_DIR}")
target_compile_definitions(lvgl PUBLIC LV_CONF_INCLUDE_SIMPLE)
target_compile_options(lvgl PRIVATE -w)

# cJSON: same version as the firmware and the host tests.
FetchContent_Declare(cjson_src
    URL https://github.com/DaveGamble/cJSON/archive/refs/tags/v1.7.19.tar.gz
    SOURCE_SUBDIR do-not-add)
FetchContent_MakeAvailable(cjson_src)
add_library(cjson STATIC "${cjson_src_SOURCE_DIR}/cJSON.c")
target_include_directories(cjson PUBLIC "${cjson_src_SOURCE_DIR}")

add_subdirectory(../components/zf_core zf_core)
add_subdirectory(../components/zf_display zf_display)
# Board header, generated from hardware/pinmap.yaml: the simulated rev A
# output stage uses its expander bit map.
add_subdirectory(../components/zf_board zf_board)
# GCC 16 at -O2 reports -Warray-bounds in zf_core's scheduler.cpp
# (zoneEnabled[z] with z < zoneCount; zoneCount is validated to be at most
# kMaxZones, which the compiler cannot see). zf_core builds with -Werror;
# the host tests build unoptimised and do not hit it. Reported to the core's
# owner in the pull request that added this simulator.
target_compile_options(zf_core PRIVATE -Wno-array-bounds)

add_executable(zoneflow-sim
    main.cpp
    scenes.cpp
    sim.cpp
)
# The harness (controller on fake hardware) is shared with the host tests.
target_include_directories(zoneflow-sim PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../test/host")
target_compile_options(zoneflow-sim PRIVATE -Wall -Wextra)
target_link_libraries(zoneflow-sim PRIVATE zf_view zf_ui zf_core zf_board cjson lvgl m)

# The window is optional: without SDL2 the simulator is headless only.
option(ZF_SIM_SDL "Build the interactive window (needs SDL2)" ON)
if(ZF_SIM_SDL)
    find_package(SDL2 QUIET)
    if(SDL2_FOUND)
        target_sources(zoneflow-sim PRIVATE window_sdl.cpp)
        target_compile_definitions(zoneflow-sim PRIVATE ZF_SIM_HAVE_SDL=1)
        if(TARGET SDL2::SDL2)
            target_link_libraries(zoneflow-sim PRIVATE SDL2::SDL2)
        else()
            target_include_directories(zoneflow-sim PRIVATE ${SDL2_INCLUDE_DIRS})
            target_link_libraries(zoneflow-sim PRIVATE ${SDL2_LIBRARIES})
        endif()
    else()
        message(STATUS "SDL2 not found: building the simulator headless only")
    endif()
endif()
