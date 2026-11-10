# zf_web: local web UI, REST API (/api/v1) and live event stream (spec 4.1).
# api.cpp, auth.cpp, command_watch.cpp, crypto.cpp, http_util.cpp, json_map.cpp and routes.cpp
# are pure C++ and also built by test/host.
idf_component_register(SRCS src/api.cpp
                            src/auth.cpp
                            src/command_watch.cpp
                            src/crypto.cpp
                            src/http_util.cpp
                            src/json_map.cpp
                            src/routes.cpp
                            src/tls.cpp
                            src/web.cpp
                       INCLUDE_DIRS include
                       REQUIRES zf_core zf_net zf_ota zf_mqtt esp_common
                       PRIV_REQUIRES zf_board zf_platform esp_http_server esp_https_server http_parser json mbedtls
                                     esp_timer esp_system esp_hw_support esp_app_format heap freertos lwip log vfs
                       EMBED_TXTFILES builtin/fallback.html builtin/setup.html)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra)

# Web UI files: firmware/zoneflow-fw/www/ (built by the UI project, not
# committed). When it holds an index.html it is packed into the "www"
# LittleFS partition image (build/www.bin) and flashed by `idf.py flash`.
# Contract: docs/firmware/web-ui-contract.md.
set(ZF_WWW_DIR "${CMAKE_CURRENT_LIST_DIR}/../../www")
if(EXISTS "${ZF_WWW_DIR}/index.html" OR EXISTS "${ZF_WWW_DIR}/index.html.gz")
    littlefs_create_partition_image(www "${ZF_WWW_DIR}" FLASH_IN_PROJECT)
else()
    message(STATUS "zf_web: no ${ZF_WWW_DIR}/index.html; www partition image not built (built-in page only)")
endif()
