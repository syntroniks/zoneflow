# Generates zf_board/pinmap_generated.hpp from hardware/pinmap.yaml.
#
#   include(.../components/zf_board/zf_board_generate.cmake)
#   zf_board_generate(<output directory> [<python interpreter>])
#
# Runs at configure time and again whenever the pin map or the generator
# changes (CMAKE_CONFIGURE_DEPENDS), so every build compiles against the
# pin map in the tree. The interpreter needs PyYAML: the ESP-IDF build
# passes its own Python, which has it. Without one, `uv run --script` reads
# the generator's inline dependency list, and the last resort is python3.
set(ZF_BOARD_GENERATOR "${CMAKE_CURRENT_LIST_DIR}/../../tools/gen_board_header.py")
get_filename_component(ZF_BOARD_GENERATOR "${ZF_BOARD_GENERATOR}" ABSOLUTE)
get_filename_component(ZF_PINMAP "${CMAKE_CURRENT_LIST_DIR}/../../../../hardware/pinmap.yaml" ABSOLUTE)

function(zf_board_generate out_dir)
    set(python "${ARGV1}")
    if(python)
        set(cmd "${python}" "${ZF_BOARD_GENERATOR}")
    else()
        find_program(ZF_UV uv)
        if(ZF_UV)
            set(cmd "${ZF_UV}" run --quiet --script "${ZF_BOARD_GENERATOR}")
        else()
            find_package(Python3 REQUIRED COMPONENTS Interpreter)
            set(cmd "${Python3_EXECUTABLE}" "${ZF_BOARD_GENERATOR}")
        endif()
    endif()
    execute_process(
        COMMAND ${cmd} --pinmap "${ZF_PINMAP}" --output "${out_dir}/zf_board/pinmap_generated.hpp"
        RESULT_VARIABLE rc
        ERROR_VARIABLE err)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "zf_board: generating the pin header from ${ZF_PINMAP} failed:\n${err}")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${ZF_PINMAP}" "${ZF_BOARD_GENERATOR}")
endfunction()
