cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(datastore_test)

set(ZDS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(ZDS_LIB ${ZDS_ROOT}/lib)
set(ZDS_INC ${ZDS_ROOT}/include)
set(GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/zds_generated)

find_program(UV_CMD NAMES uv REQUIRED)

set(TEST_DATASTORE_YAML ${CMAKE_CURRENT_SOURCE_DIR}/datastore_test.yaml)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${TEST_DATASTORE_YAML})

file(MAKE_DIRECTORY ${GENERATED_DIR}/inc/datastore)
file(MAKE_DIRECTORY ${GENERATED_DIR}/src/datastore)

execute_process(
    COMMAND ${UV_CMD} run ${ZDS_ROOT}/tools/datastore/generate_datastore.py
            --yaml       ${TEST_DATASTORE_YAML}
            --output-dir ${GENERATED_DIR}
    WORKING_DIRECTORY ${ZDS_ROOT}
    RESULT_VARIABLE GEN_RESULT
    OUTPUT_QUIET
)
if(NOT GEN_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to generate test datastore items from ${TEST_DATASTORE_YAML}")
endif()

file(GLOB GENERATED_STRUCT_SOURCES
    ${GENERATED_DIR}/src/datastore/generated_struct_*.c)

target_sources(app PRIVATE
    src/main.c
    src/datastore_storage_stub.c
    ${GENERATED_DIR}/src/datastore/generated_datastore_items.c
    ${GENERATED_DIR}/src/datastore/generated_datastore_enums.c
    ${GENERATED_STRUCT_SOURCES}
    ${ZDS_LIB}/memory.c
    ${ZDS_LIB}/event.c
    ${ZDS_LIB}/string_utils.c
    ${ZDS_LIB}/datastore/datastore.c
    ${ZDS_LIB}/datastore/datastore_describe.c
    ${ZDS_LIB}/datastore/datastore_event.c
    ${ZDS_LIB}/datastore/types/datastore_type_enum.c
    ${ZDS_LIB}/datastore/types/datastore_type_int.c
    ${ZDS_LIB}/datastore/types/datastore_type_float.c
    ${ZDS_LIB}/datastore/types/datastore_type_string.c
    ${ZDS_LIB}/datastore/types/datastore_type_byte_array.c
    ${ZDS_LIB}/datastore/types/datastore_type_buffer.c
)

target_include_directories(app PRIVATE
    ${GENERATED_DIR}/inc/datastore
    ${ZDS_INC}
)
