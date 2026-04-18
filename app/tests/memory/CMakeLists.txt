cmake_minimum_required(VERSION 3.13.1)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(memory_test)

target_sources(app PRIVATE
    src/main.c
)

target_include_directories(app PRIVATE
    ../../../app/inc
    ../../../app/src
)
