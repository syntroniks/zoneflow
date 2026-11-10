# zf_display: OLED (SSD1306/SH1106 128x64) and local controls (spec 4.1). Phase 1 stub.
idf_component_register(SRCS src/display.cpp
                       INCLUDE_DIRS include
                       REQUIRES zf_core esp_driver_i2c
                       PRIV_REQUIRES log)
