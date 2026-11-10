idf_component_register(SRCS app_main.cpp
                       PRIV_REQUIRES zf_core zf_board zf_platform zf_net zf_web zf_mqtt zf_ota zf_display
                                     esp_driver_gpio esp_event esp_netif esp_system freertos log)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra)
