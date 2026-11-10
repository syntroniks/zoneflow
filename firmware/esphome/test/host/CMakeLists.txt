# Host unit tests for the ESPHome package's own C++ logic: the safety glue
# (components/zoneflow/supervisor.*) and the scheduler logic
# (components/zoneflow_schedule/schedule_logic.*). No ESPHome, no ESP-IDF.
#   ./test/host/run.sh
#
# zf_core, the board header and the simulated board come from the standalone
# firmware's tree: one copy of the shared code, tested by both test suites.
cmake_minimum_required(VERSION 3.24)
project(zoneflow_esphome_host_tests LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)
# Same pinned versions as firmware/zoneflow-fw/test/host.
FetchContent_Declare(googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.tar.gz
    FIND_PACKAGE_ARGS NAMES GTest)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_Declare(cjson_src
    URL https://github.com/DaveGamble/cJSON/archive/refs/tags/v1.7.19.tar.gz
    SOURCE_SUBDIR do-not-add)
FetchContent_MakeAvailable(googletest cjson_src)
add_library(cjson STATIC "${cjson_src_SOURCE_DIR}/cJSON.c")
target_include_directories(cjson PUBLIC "${cjson_src_SOURCE_DIR}")

set(ZF_FW "${CMAKE_CURRENT_LIST_DIR}/../../../zoneflow-fw")
add_subdirectory(${ZF_FW}/components/zf_core zf_core)
add_subdirectory(${ZF_FW}/components/zf_board zf_board)

set(ZF_COMPONENTS "${CMAKE_CURRENT_LIST_DIR}/../../components")
add_library(zf_esphome_logic STATIC
    ${ZF_COMPONENTS}/zoneflow/supervisor.cpp
    ${ZF_COMPONENTS}/zoneflow_schedule/schedule_logic.cpp)
target_include_directories(zf_esphome_logic PUBLIC ${ZF_COMPONENTS}/zoneflow ${ZF_COMPONENTS}/zoneflow_schedule)
target_compile_options(zf_esphome_logic PRIVATE -Wall -Wextra -Wpedantic -Werror -fno-exceptions -fno-rtti)
target_link_libraries(zf_esphome_logic PUBLIC zf_core)

add_executable(zf_esphome_tests supervisor_test.cpp schedule_logic_test.cpp)
target_include_directories(zf_esphome_tests PRIVATE ${ZF_FW}/test/host)
target_compile_options(zf_esphome_tests PRIVATE -Wall -Wextra)
target_link_libraries(zf_esphome_tests PRIVATE zf_esphome_logic zf_board GTest::gtest_main)

enable_testing()
include(GoogleTest)
gtest_discover_tests(zf_esphome_tests)
