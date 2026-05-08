cmake_minimum_required(VERSION 3.13.1)

find_package(Zephyr REQUIRED HINTS ../zephyr)
project(app)

zephyr_cc_option(-Wfatal-errors)

find_program(UV_CMD NAMES uv)
if(UV_CMD)
    set(DATASTORE_YAML  ${CMAKE_CURRENT_SOURCE_DIR}/datastore.yaml)
    set(GENERATOR_DIR   ${CMAKE_CURRENT_SOURCE_DIR}/../tools/datastore)
    set(GENERATED_DIR   ${CMAKE_CURRENT_BINARY_DIR}/generated)
    set(GENERATED_H     ${GENERATED_DIR}/inc/datastore/generated_datastore_items.h)
    set(GENERATED_C     ${GENERATED_DIR}/src/datastore/generated_datastore_items.c)
    set(GENERATED_ENUMS_H ${GENERATED_DIR}/inc/datastore/generated_datastore_enums.h)
    set(GENERATED_ENUMS_C ${GENERATED_DIR}/src/datastore/generated_datastore_enums.c)

    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${DATASTORE_YAML})

    file(MAKE_DIRECTORY ${GENERATED_DIR}/inc/datastore)
    file(MAKE_DIRECTORY ${GENERATED_DIR}/src/datastore)

    message(STATUS "Generating datastore items from datastore.yaml...")
    execute_process(
        COMMAND ${UV_CMD} run ${GENERATOR_DIR}/generate_datastore.py
                --yaml       ${DATASTORE_YAML}
                --output-dir ${GENERATED_DIR}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
        RESULT_VARIABLE gen_result
    )

    if(NOT gen_result EQUAL 0)
        message(FATAL_ERROR "Datastore generation failed!")
    endif()

    file(GLOB GENERATED_STRUCT_SOURCES
        ${GENERATED_DIR}/src/datastore/generated_struct_*.c)

else()
    message(WARNING "uv not found: datastore items will not be regenerated automatically")
endif()

find_program(JUST_CMD NAMES just)
if(JUST_CMD)
    add_custom_target(
        code_format ALL

        COMMAND ${JUST_CMD}
            format
        
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Formatting code..."
        VERBATIM
    )
    add_dependencies(app code_format)
else()
    message(WARNING "just not found: code will not be formatted")
endif()


target_sources(app PRIVATE
    src/main.c
    src/string_utils.c
    src/memory.c
    src/event.c
    src/datastore/datastore.c
    src/datastore/datastore_describe.c
    src/datastore/datastore_event.c
    src/datastore/datastore_storage.c
    src/datastore/types/datastore_type_enum.c
    src/datastore/types/datastore_type_int.c
    src/datastore/types/datastore_type_float.c
    src/datastore/types/datastore_type_string.c
    src/datastore/types/datastore_type_byte_array.c
    src/datastore/types/datastore_type_buffer.c
    ${GENERATED_C}
    ${GENERATED_ENUMS_C}
    ${GENERATED_STRUCT_SOURCES}
)

target_include_directories(app PRIVATE 
    inc
    inc/datastore
    inc/datastore/types
    ${GENERATED_DIR}/inc/datastore
)

find_program(CPPCHECK_CMD NAMES cppcheck)
if(CPPCHECK_CMD)
    add_custom_target(
        static_analysis ALL

        COMMAND ${CPPCHECK_CMD}
            --quiet
            --inline-suppr
            --enable=warning,performance,portability
            --check-level=exhaustive
            --error-exitcode=1
            --inconclusive
            ${CMAKE_CURRENT_SOURCE_DIR}/src/
        
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Running cppcheck..."
        VERBATIM
    )
    add_dependencies(app static_analysis)
else()
    message(WARNING "cppcheck not found!")
endif()

find_program(SPATCH_CMD NAMES spatch)
if(SPATCH_CMD)
    add_custom_target(
        check_mem_leaks ALL

        COMMAND ${SPATCH_CMD}
            --sp-file "${CMAKE_CURRENT_SOURCE_DIR}/../tools/memory_analysis/generated_leak_check.cocci"
            --dir "${CMAKE_CURRENT_SOURCE_DIR}/src/"
            --no-includes
            --very-quiet
        
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Running memory leak check..."
        VERBATIM
    )
    add_dependencies(app check_mem_leaks)
else()
    message(WARNING "spatch not found!")
endif()

target_compile_options(app PRIVATE -Wall -Wextra)
