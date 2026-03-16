cmake_minimum_required(VERSION 3.13.1)

find_package(Zephyr REQUIRED HINTS ../zephyr)
project(app)

zephyr_cc_option(-Wfatal-errors)

target_sources(app PRIVATE 
    src/main.c
    src/memory.c
    src/event.c
    src/datastore/datastore.c
    src/datastore/generated_datastore_items.c
    src/datastore/types/datastore_type_int.c
    src/datastore/types/datastore_type_string.c
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
            --enable=warning,performance,portability
            --library=${CMAKE_CURRENT_SOURCE_DIR}/../cppcheck.cfg
            --suppress=autovarInvalidDeallocation
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

target_compile_options(app PRIVATE -Wall -Wextra)
