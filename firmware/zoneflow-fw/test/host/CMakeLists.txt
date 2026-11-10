# Host unit tests for zf_core and the display logic. No ESP-IDF needed:
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
# Display logic (no LVGL): navigation, pickers, program editor, input decoding.
add_subdirectory(../../components/zf_display zf_display)

# Host-testable logic of the network, web, OTA and MQTT modules. These sources
# include no ESP-IDF headers; the device glue in the same components does.
set(ZF_COMPONENTS "${CMAKE_CURRENT_LIST_DIR}/../../components")
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
    ${ZF_COMPONENTS}/zf_mqtt/src/mqtt_logic.cpp
)
target_include_directories(zf_ext PUBLIC
    ${ZF_COMPONENTS}/zf_net/include
    ${ZF_COMPONENTS}/zf_ota/include
    ${ZF_COMPONENTS}/zf_web/include
    ${ZF_COMPONENTS}/zf_mqtt/include)
target_compile_options(zf_ext PRIVATE -Wall -Wextra -Wpedantic -Werror -fno-exceptions -fno-rtti)
target_link_libraries(zf_ext PUBLIC zf_core cjson)

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
    ui_format_test.cpp
    ui_input_test.cpp
    ui_navigation_test.cpp
    ui_picker_test.cpp
    ui_program_edit_test.cpp
    zero_cross_test.cpp
    zone_driver_test.cpp
    crypto_test.cpp
    net_logic_test.cpp
    ota_logic_test.cpp
    web_api_test.cpp
    web_auth_test.cpp
    web_http_test.cpp
    web_openapi_test.cpp
    schedule_vectors_test.cpp
    mqtt_config_test.cpp
    mqtt_logic_test.cpp
    mqtt_discovery_test.cpp
    web_mqtt_api_test.cpp
)
target_compile_options(zf_host_tests PRIVATE -Wall -Wextra)
target_compile_definitions(zf_host_tests PRIVATE
    ZF_OPENAPI_PATH="${CMAKE_CURRENT_LIST_DIR}/../../../../docs/firmware/openapi.json"
    ZF_SCHEDULE_VECTORS_PATH="${CMAKE_CURRENT_LIST_DIR}/../vectors/schedule_preview.json"
    ZF_HA_SCHEMA_PATH="${CMAKE_CURRENT_LIST_DIR}/data/ha_mqtt_discovery.json")
target_link_libraries(zf_host_tests PRIVATE zf_ui zf_core zf_ext cjson GTest::gtest_main)

enable_testing()
include(GoogleTest)
gtest_discover_tests(zf_host_tests)
