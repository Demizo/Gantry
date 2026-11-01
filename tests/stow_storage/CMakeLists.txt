cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(stow_storage_test)

set(GANTRY_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(GANTRY_LIB ${GANTRY_ROOT}/lib)
set(GANTRY_INC ${GANTRY_ROOT}/include)

gantry_stow_generate(YAML stow_test.yaml)

target_sources(app PRIVATE
    src/test_stow_storage.c
    ${GANTRY_LIB}/memory.c
    ${GANTRY_LIB}/event.c
    ${GANTRY_LIB}/string_utils.c
    ${GANTRY_LIB}/stow/stow.c
    ${GANTRY_LIB}/stow/stow_event.c
    ${GANTRY_LIB}/stow/stow_storage.c
    ${GANTRY_LIB}/stow/types/stow_type_enum.c
    ${GANTRY_LIB}/stow/types/stow_type_int.c
    ${GANTRY_LIB}/stow/types/stow_type_float.c
    ${GANTRY_LIB}/stow/types/stow_type_string.c
    ${GANTRY_LIB}/stow/types/stow_type_byte_array.c
    ${GANTRY_LIB}/stow/types/stow_type_buffer.c
)

target_include_directories(app PRIVATE
    ${GANTRY_INC}
)
