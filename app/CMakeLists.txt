cmake_minimum_required(VERSION 3.13.1)

find_package(Zephyr REQUIRED HINTS ../zephyr)
project(app)

zephyr_cc_option(-Wfatal-errors)

target_sources(app PRIVATE src/main.c)

target_compile_options(app PRIVATE -Wall -Wextra)
