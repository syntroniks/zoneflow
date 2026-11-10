# zf_web: Local web UI, REST API and live status stream (spec 4.1). Phase 1 stub.
idf_component_register(SRCS src/web.cpp
                       INCLUDE_DIRS include
                       REQUIRES zf_core
                       PRIV_REQUIRES log)
