# Board definitions: the only place GPIO numbers and board constants live.
# The pin numbers themselves come from hardware/pinmap.yaml through a header
# generated at build time (zf_board_generate.cmake).
if(ESP_PLATFORM)
    set(zf_board_gen_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
    if(NOT CMAKE_BUILD_EARLY_EXPANSION)
        include(${CMAKE_CURRENT_LIST_DIR}/zf_board_generate.cmake)
        idf_build_get_property(zf_python PYTHON)
        zf_board_generate("${zf_board_gen_dir}" "${zf_python}")
    endif()
    idf_component_register(INCLUDE_DIRS include "${zf_board_gen_dir}")
else()
    # Host builds (tests, display simulator): an interface library.
    include(${CMAKE_CURRENT_LIST_DIR}/zf_board_generate.cmake)
    zf_board_generate("${CMAKE_CURRENT_BINARY_DIR}/generated")
    add_library(zf_board INTERFACE)
    target_include_directories(zf_board INTERFACE "${CMAKE_CURRENT_LIST_DIR}/include"
                                                  "${CMAKE_CURRENT_BINARY_DIR}/generated")
endif()
