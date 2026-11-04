cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(stow_test)

set(GANTRY_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(GANTRY_LIB ${GANTRY_ROOT}/lib)
set(GANTRY_INC ${GANTRY_ROOT}/include)

gantry_stow_generate(YAML stow.yaml)

target_sources(app PRIVATE
    src/test_stow_int.c
    src/test_stow_float.c
    src/test_stow_enum.c
    src/test_stow_string.c
    src/test_stow_byte_array.c
    src/test_stow_buffer.c
    src/test_stow_access.c
    src/test_stow_subscribe.c
    src/test_stow_static_subscribe.c
    src/test_stow_struct.c
    src/test_stow_nested_struct.c
    src/test_stow_custom.c
    src/test_stow_isr.c
    src/test_stow_concurrency.c
    src/test_stow_watermark.c
    src/test_stow_persistence.c
    src/stow_storage_stub.c
    ${GANTRY_LIB}/memory.c
    ${GANTRY_LIB}/event.c
    ${GANTRY_LIB}/stow/stow.c
    ${GANTRY_LIB}/stow/stow_event.c
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
