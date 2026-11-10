# zf_ota: firmware update with rollback (spec 4.1). ota_logic.cpp is pure C++
# and also built by test/host.
idf_component_register(SRCS src/ota.cpp src/ota_logic.cpp
                       INCLUDE_DIRS include
                       REQUIRES zf_core esp_common
                       PRIV_REQUIRES app_update esp_https_ota esp_http_client esp_app_format esp_partition mbedtls
                                     esp_timer esp_system freertos log)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra)
