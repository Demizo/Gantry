cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(stow_runtime_test)

set(GANTRY_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(GANTRY_LIB ${GANTRY_ROOT}/lib)
set(GANTRY_INC ${GANTRY_ROOT}/include)

# The fixture's generated items set the nesting depth the message codec is built for
gantry_stow_generate(YAML ${GANTRY_ROOT}/protocols/stow/stow.yaml APP_DIR ${GANTRY_ROOT}/protocols/stow)

target_sources(app PRIVATE
    src/test_stow_runtime.c
    ${GANTRY_LIB}/memory.c
    ${GANTRY_LIB}/event.c
    ${GANTRY_LIB}/string_utils.c
    ${GANTRY_LIB}/stow/stow_msg.c
    ${GANTRY_LIB}/stow/stow_runtime.c
    ${GANTRY_LIB}/stow/types/stow_type_enum.c
    ${GANTRY_LIB}/stow/types/stow_type_int.c
    ${GANTRY_LIB}/stow/types/stow_type_float.c
    ${GANTRY_LIB}/stow/types/stow_type_string.c
    ${GANTRY_LIB}/stow/types/stow_type_byte_array.c
    ${GANTRY_LIB}/stow/types/stow_type_buffer.c
)

target_include_directories(app PRIVATE
    ${GANTRY_INC}
    ${GANTRY_ROOT}/tests/common
)
