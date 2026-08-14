cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(gantry_example_app)

zephyr_cc_option(-Wfatal-errors)

gantry_stow_add_fragment(${CMAKE_CURRENT_SOURCE_DIR}/ble.stow.yaml)

target_include_directories(app PRIVATE 
    inc
)

target_sources(app PRIVATE
    src/main.c
    src/session_manager.c
    src/msg_event.c
    src/uart_interface.c
    src/ble_manager.c
    src/ble_interface.c
)


target_compile_options(app PRIVATE -Wall -Wextra)
