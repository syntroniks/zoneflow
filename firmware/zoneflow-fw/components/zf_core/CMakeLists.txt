# zf_core: pure C++ logic. Builds as an ESP-IDF component and as a plain
# static library for host tests (test/host). No ESP-IDF headers allowed here.
set(ZF_CORE_SOURCES
    src/config.cpp
    src/config_json.cpp
    src/config_store.cpp
    src/controller.cpp
    src/current_monitor.cpp
    src/event_bus.cpp
    src/scheduler.cpp
    src/sensors.cpp
    src/strings.cpp
    src/time_service.cpp
    src/time_zone.cpp
    src/zone_driver.cpp
)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${ZF_CORE_SOURCES}
                           INCLUDE_DIRS include
                           PRIV_REQUIRES json)
    target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra)
else()
    list(TRANSFORM ZF_CORE_SOURCES PREPEND "${CMAKE_CURRENT_LIST_DIR}/")
    add_library(zf_core STATIC ${ZF_CORE_SOURCES})
    target_include_directories(zf_core PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include")
    target_compile_features(zf_core PUBLIC cxx_std_20)
    # Match the firmware build: no exceptions, no RTTI in core code.
    target_compile_options(zf_core PRIVATE -Wall -Wextra -Wpedantic -Werror -fno-exceptions -fno-rtti)
    target_link_libraries(zf_core PRIVATE cjson)
endif()
