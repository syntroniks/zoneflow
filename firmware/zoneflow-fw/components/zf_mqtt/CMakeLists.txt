# zf_mqtt: MQTT client with Home Assistant discovery (spec 4.1). Phase 1 stub.
idf_component_register(SRCS src/mqtt.cpp
                       INCLUDE_DIRS include
                       REQUIRES zf_core
                       PRIV_REQUIRES log)
