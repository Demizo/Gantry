cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zds_example_app)

zephyr_cc_option(-Wfatal-errors)

target_sources(app PRIVATE src/main.c)
target_compile_options(app PRIVATE -Wall -Wextra)
