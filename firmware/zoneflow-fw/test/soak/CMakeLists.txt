# System soak simulation of the ZoneFlow firmwares (README.md here):
#   ./test/soak/run.sh --ci         the deterministic subset CI runs
#   ./test/soak/run.sh --long       the long run
#   ./test/soak/run.sh --mode chaos --seeds 1-100 --days 30
cmake_minimum_required(VERSION 3.24)
project(zoneflow_soak LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

include(FetchContent)
# cJSON: the version the firmware gets from ESP-IDF v5.5.6, as in test/host.
FetchContent_Declare(cjson_src
    URL https://github.com/DaveGamble/cJSON/archive/refs/tags/v1.7.19.tar.gz
    SOURCE_SUBDIR do-not-add)
FetchContent_MakeAvailable(cjson_src)
add_library(cjson STATIC "${cjson_src_SOURCE_DIR}/cJSON.c")
target_include_directories(cjson PUBLIC "${cjson_src_SOURCE_DIR}")

set(ZF_COMPONENTS "${CMAKE_CURRENT_LIST_DIR}/../../components")
add_subdirectory(${ZF_COMPONENTS}/zf_core zf_core)
add_subdirectory(${ZF_COMPONENTS}/zf_board zf_board)
# See sim/CMakeLists.txt: GCC at -O2 reports a false -Warray-bounds in
# scheduler.cpp, and zf_core builds with -Werror.
target_compile_options(zf_core PRIVATE -Wno-array-bounds)

# The framework-free layers of the web and MQTT modules, as the browser demo
# and the host tests link them.
add_library(zf_ext STATIC
    ${ZF_COMPONENTS}/zf_net/src/net_logic.cpp
    ${ZF_COMPONENTS}/zf_ota/src/ota_logic.cpp
    ${ZF_COMPONENTS}/zf_web/src/api.cpp
    ${ZF_COMPONENTS}/zf_web/src/auth.cpp
    ${ZF_COMPONENTS}/zf_web/src/command_watch.cpp
    ${ZF_COMPONENTS}/zf_web/src/crypto.cpp
    ${ZF_COMPONENTS}/zf_web/src/http_util.cpp
    ${ZF_COMPONENTS}/zf_web/src/json_map.cpp
    ${ZF_COMPONENTS}/zf_web/src/routes.cpp
    ${ZF_COMPONENTS}/zf_mqtt/src/mqtt_logic.cpp)
target_include_directories(zf_ext PUBLIC
    ${ZF_COMPONENTS}/zf_net/include
    ${ZF_COMPONENTS}/zf_ota/include
    ${ZF_COMPONENTS}/zf_web/include
    ${ZF_COMPONENTS}/zf_mqtt/include)
target_compile_options(zf_ext PRIVATE -Wall -Wextra -fno-exceptions -fno-rtti)
target_link_libraries(zf_ext PUBLIC zf_core cjson)

# The ESPHome package's supervisor glue, built from its own tree.
set(ZF_ESPHOME "${CMAKE_CURRENT_LIST_DIR}/../../../esphome/components/zoneflow")
# And the on-device scheduler's logic (zoneflow_schedule), framework-free.
set(ZF_ESPHOME_SCHEDULE "${CMAKE_CURRENT_LIST_DIR}/../../../esphome/components/zoneflow_schedule")
add_library(zf_esphome_glue STATIC ${ZF_ESPHOME}/supervisor.cpp ${ZF_ESPHOME_SCHEDULE}/schedule_logic.cpp)
target_include_directories(zf_esphome_glue PUBLIC ${ZF_ESPHOME} ${ZF_ESPHOME_SCHEDULE})
target_compile_options(zf_esphome_glue PRIVATE -Wall -Wextra -fno-exceptions -fno-rtti)
target_link_libraries(zf_esphome_glue PUBLIC zf_core)

add_executable(zf_soak
    main.cpp
    alloc_count.cpp
    world.cpp
    physical_oracle.cpp
    scenario.cpp
    standalone.cpp
    esphome.cpp)
# The simulated board and the test doubles come from the host tests.
target_include_directories(zf_soak PRIVATE "${CMAKE_CURRENT_LIST_DIR}" "${CMAKE_CURRENT_LIST_DIR}/../host")
target_compile_options(zf_soak PRIVATE -Wall -Wextra)
target_link_libraries(zf_soak PRIVATE zf_ext zf_esphome_glue zf_board zf_core cjson)
