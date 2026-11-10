# zf_net: WiFi station, BLE/SoftAP provisioning, hostname and mDNS (spec 4.1).
# net_logic.cpp is pure C++ and also built by test/host.
idf_component_register(SRCS src/net.cpp src/net_logic.cpp src/nvs_store.cpp
                       INCLUDE_DIRS include
                       REQUIRES zf_core esp_common
                       PRIV_REQUIRES esp_wifi esp_netif esp_event esp_timer esp_system esp_hw_support esp_app_format
                                     nvs_flash wifi_provisioning protocomm bt lwip log)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra)
