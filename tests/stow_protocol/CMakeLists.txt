cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(stow_protocol_test)

set(GANTRY_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(GANTRY_LIB ${GANTRY_ROOT}/lib)
set(GANTRY_INC ${GANTRY_ROOT}/include)

gantry_stow_generate(YAML ${GANTRY_ROOT}/protocols/stow/stow.yaml APP_DIR ${GANTRY_ROOT}/protocols/stow)

target_sources(app PRIVATE
    src/test_common.c
    src/test_stow_protocol_describe.c
    src/test_stow_protocol_subscribe.c
    src/test_stow_protocol_sessions.c
    src/test_stow_protocol_netbuf.c
    src/test_stow_protocol_transport.c
    src/test_stow_cobs_transport.c
    src/test_stow_protocol_errors.c
    src/test_stow_protocol_vectors.c
    ${GANTRY_ROOT}/tests/common/stow_storage_stub.c
    ${GANTRY_LIB}/memory.c
    ${GANTRY_LIB}/event.c
    ${GANTRY_LIB}/string_utils.c
    ${GANTRY_LIB}/cobs_framer.c
    ${GANTRY_LIB}/stow/stow.c
    ${GANTRY_LIB}/stow/stow_event.c
    ${GANTRY_LIB}/stow/stow_msg.c
    ${GANTRY_LIB}/stow/stow_protocol.c
    ${GANTRY_LIB}/stow/stow_runtime.c
    ${GANTRY_LIB}/stow/stow_cobs_transport.c
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

gantry_protocol_vectors(app ${GANTRY_ROOT}/protocols/stow stow)
