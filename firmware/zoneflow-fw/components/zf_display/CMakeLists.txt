# zf_display: the local user interface (spec 4.1): 320x240 ST7789 TFT, rotary
# encoder with push switch, Back and Manual/Home buttons.
#
# Three layers, see docs/design/display-ui.md:
#   src/ui    logic, no LVGL and no ESP-IDF  (host tests, simulator, device)
#   src/view  LVGL drawing                    (simulator, device)
#   src/port  ESP-IDF drivers and the UI task (device)
set(ZF_UI_SOURCES
    src/ui/faults.cpp
    src/ui/format.cpp
    src/ui/input.cpp
    src/ui/next_run.cpp
    src/ui/picker.cpp
    src/ui/program_edit.cpp
    src/ui/screen_util.cpp
    src/ui/screens_main.cpp
    src/ui/screens_programs.cpp
    src/ui/screens_setup.cpp
    src/ui/screens_status.cpp
    src/ui/strings.cpp
    src/ui/tz_table.cpp
    src/ui/ui.cpp
)
set(ZF_VIEW_SOURCES
    src/view/icons.cpp
    src/view/theme.cpp
    src/view/view.cpp
)

if(ESP_PLATFORM)
    idf_component_register(
        SRCS ${ZF_UI_SOURCES} ${ZF_VIEW_SOURCES}
             src/port/display.cpp
             src/port/inputs.cpp
             src/port/lcd.cpp
             src/port/settings.cpp
        INCLUDE_DIRS include
        REQUIRES zf_core
        PRIV_REQUIRES zf_board zf_platform lvgl__lvgl esp_lcd esp_driver_spi esp_driver_ledc
                      esp_driver_pcnt esp_driver_gpio esp_timer esp_app_format esp_system nvs_flash
                      heap freertos log)
    target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra)
else()
    # Host: the logic alone for unit tests; the view too when the including
    # project (sim/) provides an `lvgl` target.
    list(TRANSFORM ZF_UI_SOURCES PREPEND "${CMAKE_CURRENT_LIST_DIR}/")
    add_library(zf_ui STATIC ${ZF_UI_SOURCES})
    target_include_directories(zf_ui PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include")
    target_compile_features(zf_ui PUBLIC cxx_std_20)
    target_compile_options(zf_ui PRIVATE -Wall -Wextra -Wpedantic -Werror -fno-exceptions -fno-rtti)
    target_link_libraries(zf_ui PUBLIC zf_core)

    if(TARGET lvgl)
        list(TRANSFORM ZF_VIEW_SOURCES PREPEND "${CMAKE_CURRENT_LIST_DIR}/")
        add_library(zf_view STATIC ${ZF_VIEW_SOURCES})
        target_compile_options(zf_view PRIVATE -Wall -Wextra -Werror -fno-exceptions -fno-rtti)
        target_link_libraries(zf_view PUBLIC zf_ui lvgl)
    endif()
endif()
