# zf_mqtt: MQTT client with Home Assistant discovery (spec 4.1).
# mqtt_logic.cpp is pure C++ and also built by test/host.
idf_component_register(SRCS src/mqtt.cpp src/mqtt_logic.cpp
                       INCLUDE_DIRS include
                       REQUIRES zf_core zf_net zf_ota esp_common
                       PRIV_REQUIRES zf_board zf_platform zf_web mqtt mbedtls json esp_app_format esp_system freertos log)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra)
