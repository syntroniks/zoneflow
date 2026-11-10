# Host unit tests for zf_core. No ESP-IDF needed:
#   ./test/host/run.sh
cmake_minimum_required(VERSION 3.24)
project(zoneflow_host_tests LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

# GoogleTest: use the system package if present, else fetch a pinned release.
FetchContent_Declare(googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.tar.gz
    FIND_PACKAGE_ARGS NAMES GTest)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

# cJSON: same library and version the firmware gets from ESP-IDF v5.5.6
# (components/json, cJSON 1.7.19). Built directly from its sources.
FetchContent_Declare(cjson_src
    URL https://github.com/DaveGamble/cJSON/archive/refs/tags/v1.7.19.tar.gz
    SOURCE_SUBDIR do-not-add)

FetchContent_MakeAvailable(googletest cjson_src)

add_library(cjson STATIC "${cjson_src_SOURCE_DIR}/cJSON.c")
target_include_directories(cjson PUBLIC "${cjson_src_SOURCE_DIR}")

add_subdirectory(../../components/zf_core zf_core)

add_executable(zf_host_tests
    civil_test.cpp
    config_test.cpp
    controller_test.cpp
    diagnostics_test.cpp
    event_bus_test.cpp
    output_enable_test.cpp
    persist_test.cpp
    scheduler_test.cpp
    time_service_test.cpp
    time_zone_test.cpp
    zero_cross_test.cpp
    zone_driver_test.cpp
)
target_compile_options(zf_host_tests PRIVATE -Wall -Wextra)
target_link_libraries(zf_host_tests PRIVATE zf_core cjson GTest::gtest_main)

enable_testing()
include(GoogleTest)
gtest_discover_tests(zf_host_tests)
