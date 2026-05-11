cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(stow_test)

set(ZDS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(ZDS_LIB ${ZDS_ROOT}/lib)
set(ZDS_INC ${ZDS_ROOT}/include)
set(GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/zds_generated)

find_program(UV_CMD NAMES uv REQUIRED)

set(TEST_STOW_YAML ${CMAKE_CURRENT_SOURCE_DIR}/stow_test.yaml)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${TEST_STOW_YAML})

file(MAKE_DIRECTORY ${GENERATED_DIR}/inc/stow)
file(MAKE_DIRECTORY ${GENERATED_DIR}/src/stow)

execute_process(
    COMMAND ${UV_CMD} run ${ZDS_ROOT}/tools/stow/generate_stow.py
            --yaml       ${TEST_STOW_YAML}
            --output-dir ${GENERATED_DIR}
    WORKING_DIRECTORY ${ZDS_ROOT}
    RESULT_VARIABLE GEN_RESULT
    OUTPUT_QUIET
)
if(NOT GEN_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to generate test stow items from ${TEST_STOW_YAML}")
endif()

file(GLOB GENERATED_STRUCT_SOURCES
    ${GENERATED_DIR}/src/stow/generated_struct_*.c)

target_sources(app PRIVATE
    src/main.c
    src/stow_storage_stub.c
    ${GENERATED_DIR}/src/stow/generated_stow_items.c
    ${GENERATED_DIR}/src/stow/generated_stow_enums.c
    ${GENERATED_STRUCT_SOURCES}
    ${ZDS_LIB}/memory.c
    ${ZDS_LIB}/event.c
    ${ZDS_LIB}/string_utils.c
    ${ZDS_LIB}/stow/stow.c
    ${ZDS_LIB}/stow/stow_describe.c
    ${ZDS_LIB}/stow/stow_event.c
    ${ZDS_LIB}/stow/types/stow_type_enum.c
    ${ZDS_LIB}/stow/types/stow_type_int.c
    ${ZDS_LIB}/stow/types/stow_type_float.c
    ${ZDS_LIB}/stow/types/stow_type_string.c
    ${ZDS_LIB}/stow/types/stow_type_byte_array.c
    ${ZDS_LIB}/stow/types/stow_type_buffer.c
)

target_include_directories(app PRIVATE
    ${GENERATED_DIR}/inc/stow
    ${ZDS_INC}
)
