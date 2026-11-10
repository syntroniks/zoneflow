# ZoneFlow standalone firmware (ESP-IDF v5.5, target esp32s3).
cmake_minimum_required(VERSION 3.16)

# -D ZF_DISPLAY=1 adds sdkconfig.display: the provisional pin map with the
# display on GPIO18, 47 and 48 (components/zf_board, "pin budget").
if(ZF_DISPLAY)
    set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;sdkconfig.display")
endif()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(zoneflow-fw)
