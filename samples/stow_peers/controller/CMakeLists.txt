cmake_minimum_required(VERSION 3.20.0)

set(APPLICATION_CONFIG_DIR ${CMAKE_CURRENT_LIST_DIR}/../common)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(stow_peers_controller)

gantry_stow_client_generate(PREFIX sensor YAML ${CMAKE_CURRENT_SOURCE_DIR}/../sensor/stow.yaml)

target_include_directories(app PRIVATE ../common/inc)

target_sources(app PRIVATE
    src/main.c
    ../common/src/peer_link.c
    ../common/src/peer_console.c
)
