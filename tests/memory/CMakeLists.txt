cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(memory_test)

set(GANTRY_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

target_sources(app PRIVATE
    src/main.c
)

# `src/main.c` does `#include "memory.c"` to exercise static functions, so the
# library directory must come before the public include path.
target_include_directories(app PRIVATE
    ${GANTRY_ROOT}/lib
    ${GANTRY_ROOT}/include
)
