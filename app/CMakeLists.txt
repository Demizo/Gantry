cmake_minimum_required(VERSION 3.13.1)

find_package(Zephyr REQUIRED HINTS ../zephyr)
project(app)

zephyr_cc_option(-Wfatal-errors)

find_program(UV_CMD NAMES uv)
if(UV_CMD)
    set(DATASTORE_YAML  ${CMAKE_CURRENT_SOURCE_DIR}/datastore.yaml)
    set(GENERATED_H     ${CMAKE_CURRENT_SOURCE_DIR}/inc/datastore/generated_datastore_items.h)
    set(GENERATED_C     ${CMAKE_CURRENT_SOURCE_DIR}/src/datastore/generated_datastore_items.c)
    set(GENERATOR_DIR   ${CMAKE_CURRENT_SOURCE_DIR}/../tools/datastore)

    add_custom_command(
        OUTPUT  ${GENERATED_H} ${GENERATED_C}
        COMMAND ${UV_CMD} run ${GENERATOR_DIR}/generate_datastore.py
                --yaml       ${DATASTORE_YAML}
                --output-dir ${CMAKE_CURRENT_SOURCE_DIR}
        DEPENDS
            ${DATASTORE_YAML}
            ${GENERATOR_DIR}/generate_datastore.py
            ${GENERATOR_DIR}/validator.py
            ${GENERATOR_DIR}/generator.py
            ${GENERATOR_DIR}/templates/generated_datastore_items.h.j2
            ${GENERATOR_DIR}/templates/generated_datastore_items.c.j2
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
        COMMENT "Generating datastore items from datastore.yaml..."
        VERBATIM
    )
    add_custom_target(generate_datastore_items
        DEPENDS ${GENERATED_H} ${GENERATED_C}
    )
    add_dependencies(app generate_datastore_items)
else()
    message(WARNING "uv not found: datastore items will not be regenerated automatically")
endif()

target_sources(app PRIVATE
    src/main.c
    src/string_utils.c
    src/memory.c
    src/event.c
    src/datastore/datastore.c
    src/datastore/datastore_event.c
    src/datastore/datastore_storage.c
    src/datastore/generated_datastore_items.c
    src/datastore/types/datastore_type_enum.c
    src/datastore/types/datastore_type_int.c
    src/datastore/types/datastore_type_float.c
    src/datastore/types/datastore_type_string.c
    src/datastore/types/datastore_type_byte_array.c
    src/datastore/types/datastore_type_buffer.c
)

target_include_directories(app PRIVATE 
    inc
    inc/datastore
    inc/datastore/types
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
