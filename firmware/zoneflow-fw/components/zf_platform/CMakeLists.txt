# ESP-IDF implementations of the zf_core interfaces.
idf_component_register(
    SRCS src/event_log.cpp
         src/outputs.cpp
         src/persist_writer.cpp
         src/safety.cpp
         src/sensors.cpp
         src/time_storage.cpp
         src/zero_cross.cpp
    INCLUDE_DIRS include
    REQUIRES zf_core esp_driver_i2c freertos esp_event
    PRIV_REQUIRES zf_board esp_driver_gpio esp_driver_gptimer esp_driver_pcnt esp_adc esp_timer esp_netif esp_system
                  esp_hw_support hal soc nvs_flash lwip log)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra)

# Runs __wrap_esp_panic_handler (safety.cpp) ahead of the IDF panic handler.
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=esp_panic_handler" "-u __wrap_esp_panic_handler")
