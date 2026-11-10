# zf_ota: OTA update with rollback (spec 4.1). Phase 1 stub.
idf_component_register(SRCS src/ota.cpp
                       INCLUDE_DIRS include
                       REQUIRES zf_core
                       PRIV_REQUIRES log app_update)
