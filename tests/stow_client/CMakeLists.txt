cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(stow_client_test)

set(GANTRY_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(GANTRY_LIB ${GANTRY_ROOT}/lib)
set(GANTRY_INC ${GANTRY_ROOT}/include)
set(GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/gantry_generated)

find_program(UV_CMD NAMES uv REQUIRED)

set(TEST_STOW_YAML ${CMAKE_CURRENT_SOURCE_DIR}/stow_test.yaml)
set(VARIANT_STOW_YAML ${CMAKE_CURRENT_SOURCE_DIR}/variant.stow.yaml)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${TEST_STOW_YAML} ${VARIANT_STOW_YAML})

file(MAKE_DIRECTORY ${GENERATED_DIR}/inc/stow)
file(MAKE_DIRECTORY ${GENERATED_DIR}/src/stow)

foreach(_gen
    "generate_stow.py;--yaml;${TEST_STOW_YAML}"
    "generate_stow_client.py;--yaml;${TEST_STOW_YAML};--prefix;peer"
    "generate_stow_client.py;--yaml;${VARIANT_STOW_YAML};--prefix;variant")
  list(POP_FRONT _gen _script)
  execute_process(
      COMMAND ${UV_CMD} run ${GANTRY_ROOT}/tools/stow/${_script} ${_gen} --output-dir ${GENERATED_DIR}
      WORKING_DIRECTORY ${GANTRY_ROOT}
      RESULT_VARIABLE GEN_RESULT
      OUTPUT_QUIET
  )
  if(NOT GEN_RESULT EQUAL 0)
      message(FATAL_ERROR "Failed to run ${_script} ${_gen}")
  endif()
endforeach()

set(VECTORS_FILE ${GANTRY_ROOT}/protocols/stow/vectors.json)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${VECTORS_FILE})

execute_process(
    COMMAND ${UV_CMD} run ${GANTRY_ROOT}/tests/stow_protocol/generate_vectors.py
            --vectors ${VECTORS_FILE}
            --output  ${GENERATED_DIR}/inc/stow/generated_vectors.h
    WORKING_DIRECTORY ${GANTRY_ROOT}
    RESULT_VARIABLE VECTOR_RESULT
    OUTPUT_QUIET
)
if(NOT VECTOR_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to generate golden vectors from ${VECTORS_FILE}")
endif()

file(GLOB GENERATED_SOURCES ${GENERATED_DIR}/src/stow/*.c)

target_sources(app PRIVATE
    src/test_common.c
    src/test_stow_client_loopback.c
    src/test_stow_client_compat.c
    src/test_stow_client_faults.c
    src/test_stow_client_vectors.c
    ${GANTRY_ROOT}/tests/stow_protocol/src/stow_storage_stub.c
    ${GENERATED_SOURCES}
    ${GANTRY_LIB}/memory.c
    ${GANTRY_LIB}/event.c
    ${GANTRY_LIB}/string_utils.c
    ${GANTRY_LIB}/stow/stow.c
    ${GANTRY_LIB}/stow/stow_describe.c
    ${GANTRY_LIB}/stow/stow_event.c
    ${GANTRY_LIB}/stow/stow_serde.c
    ${GANTRY_LIB}/stow/stow_wire.c
    ${GANTRY_LIB}/stow/stow_protocol.c
    ${GANTRY_LIB}/stow/stow_client_serde.c
    ${GANTRY_LIB}/stow/stow_client.c
    ${GANTRY_LIB}/stow/stow_client_slots.c
    ${GANTRY_LIB}/stow/stow_client_verify.c
    ${GANTRY_LIB}/stow/types/stow_type_enum.c
    ${GANTRY_LIB}/stow/types/stow_type_int.c
    ${GANTRY_LIB}/stow/types/stow_type_float.c
    ${GANTRY_LIB}/stow/types/stow_type_string.c
    ${GANTRY_LIB}/stow/types/stow_type_byte_array.c
    ${GANTRY_LIB}/stow/types/stow_type_buffer.c
)

target_include_directories(app PRIVATE
    ${GENERATED_DIR}/inc/stow
    ${GANTRY_INC}
)
