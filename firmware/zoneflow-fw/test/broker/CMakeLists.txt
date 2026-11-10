# Broker integration harness. Not part of CI: run ./run.sh, which builds a
# local Mosquitto and passes MOSQUITTO_SRC and MOSQUITTO_LIB.
cmake_minimum_required(VERSION 3.24)
project(zoneflow_broker_harness LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(cjson_src
    URL https://github.com/DaveGamble/cJSON/archive/refs/tags/v1.7.19.tar.gz
    SOURCE_SUBDIR do-not-add)
FetchContent_MakeAvailable(cjson_src)
add_library(cjson STATIC "${cjson_src_SOURCE_DIR}/cJSON.c")
target_include_directories(cjson PUBLIC "${cjson_src_SOURCE_DIR}")

set(ZF_COMPONENTS "${CMAKE_CURRENT_LIST_DIR}/../../components")
add_subdirectory(${ZF_COMPONENTS}/zf_core zf_core)
add_executable(harness
    harness.cpp
    ${ZF_COMPONENTS}/zf_mqtt/src/mqtt_logic.cpp
    ${ZF_COMPONENTS}/zf_net/src/net_logic.cpp)
target_include_directories(harness PRIVATE
    ${ZF_COMPONENTS}/zf_mqtt/include
    ${ZF_COMPONENTS}/zf_net/include
    ${ZF_COMPONENTS}/zf_ota/include
    ${MOSQUITTO_SRC}/include)
target_link_libraries(harness PRIVATE zf_core cjson ${MOSQUITTO_LIB})
