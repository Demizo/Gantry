cmake_minimum_required(VERSION 3.13.1)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(datastore_test)

find_program(UV_CMD NAMES uv REQUIRED)

set(TEST_DATASTORE_YAML ${CMAKE_CURRENT_SOURCE_DIR}/datastore_test.yaml)
set(GENERATOR_DIR       ${CMAKE_CURRENT_SOURCE_DIR}/../../../tools/datastore)
set(GENERATED_DIR       ${CMAKE_CURRENT_BINARY_DIR}/generated)

file(MAKE_DIRECTORY ${GENERATED_DIR}/inc/datastore)
file(MAKE_DIRECTORY ${GENERATED_DIR}/src/datastore)

execute_process(
    COMMAND ${UV_CMD} run ${GENERATOR_DIR}/generate_datastore.py
            --yaml       ${TEST_DATASTORE_YAML}
            --output-dir ${GENERATED_DIR}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../../../
    RESULT_VARIABLE GEN_RESULT
    OUTPUT_QUIET
)

if(NOT GEN_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to generate test datastore items from ${TEST_DATASTORE_YAML}")
endif()

set(APP_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../../app/src)

target_sources(app PRIVATE
    src/main.c
    src/datastore_storage_stub.c
    ${GENERATED_DIR}/src/datastore/generated_datastore_items.c
    ${APP_SRC}/memory.c
    ${APP_SRC}/event.c
    ${APP_SRC}/string_utils.c
    ${APP_SRC}/datastore/datastore.c
    ${APP_SRC}/datastore/datastore_describe.c
    ${APP_SRC}/datastore/datastore_event.c
    ${APP_SRC}/datastore/types/datastore_type_enum.c
    ${APP_SRC}/datastore/types/datastore_type_int.c
    ${APP_SRC}/datastore/types/datastore_type_float.c
    ${APP_SRC}/datastore/types/datastore_type_string.c
    ${APP_SRC}/datastore/types/datastore_type_byte_array.c
    ${APP_SRC}/datastore/types/datastore_type_buffer.c
)

target_include_directories(app PRIVATE
    ${GENERATED_DIR}/inc/datastore
    ../../../app/inc
    ../../../app/inc/datastore
    ../../../app/inc/datastore/types
)
