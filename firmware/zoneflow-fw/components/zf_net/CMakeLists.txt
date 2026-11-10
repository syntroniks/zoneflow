# zf_net: WiFi station, BLE provisioning and the fallback access point (spec 4.1). Phase 1 stub.
idf_component_register(SRCS src/net.cpp
                       INCLUDE_DIRS include
                       REQUIRES zf_core
                       PRIV_REQUIRES log)
