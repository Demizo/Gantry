if(CONFIG_GANTRY)
  zephyr_include_directories(include)
endif()

if(CONFIG_GANTRY_STOW)
  zephyr_linker_sources(SECTIONS cmake/stow_static_subscription.ld)
endif()

if(CONFIG_STOW_CLIENT)
  zephyr_linker_sources(SECTIONS cmake/stow_client_cb.ld)
endif()

if(CONFIG_GANTRY_MODULE)
  zephyr_linker_sources(SECTIONS cmake/gantry_module.ld)
endif()

if(CONFIG_GANTRY_BUILD_LIBRARY)
  zephyr_library_named(gantry)

  zephyr_library_sources(lib/string_utils.c)

  if(CONFIG_GANTRY_MEMORY)
    zephyr_library_sources(lib/memory.c)
  endif()

  if(CONFIG_GANTRY_EVENTS)
    zephyr_library_sources(lib/event.c)
  endif()

  if(CONFIG_GANTRY_MODULE)
    zephyr_library_sources(lib/module.c)
  endif()

  include(${CMAKE_CURRENT_LIST_DIR}/cmake/gantry.cmake)

  if(CONFIG_GANTRY_COBS_FRAMER)
    zephyr_library_sources(lib/cobs_framer.c)
  endif()

  set(ZEPHYR_GANTRY_MODULE_DIR "${ZEPHYR_GANTRY_MODULE_DIR}" CACHE INTERNAL "")

  if(CONFIG_GANTRY_STOW_TYPES)
    zephyr_library_sources(
      lib/stow/types/stow_type_enum.c
      lib/stow/types/stow_type_int.c
      lib/stow/types/stow_type_float.c
      lib/stow/types/stow_type_string.c
      lib/stow/types/stow_type_byte_array.c
      lib/stow/types/stow_type_buffer.c
    )
  endif()

  if(CONFIG_STOW_PROTOCOL OR CONFIG_STOW_CLIENT)
    zephyr_library_sources(lib/stow/stow_wire.c)
  endif()

  if(CONFIG_STOW_COBS_TRANSPORT)
    zephyr_library_sources(lib/stow/stow_cobs_transport.c)
  endif()

  if(CONFIG_STOW_CLIENT)
    zephyr_library_sources(
      lib/stow/stow_client_serde.c
      lib/stow/stow_client.c
      lib/stow/stow_client_slots.c
      lib/stow/stow_client_verify.c
      lib/stow/stow_mux.c
    )
  endif()

  if(CONFIG_GANTRY_STOW)
    zephyr_library_sources(
      lib/stow/stow.c
      lib/stow/stow_describe.c
      lib/stow/stow_event.c
      lib/stow/stow_storage.c
    )

    if(CONFIG_STOW_PROTOCOL)
      zephyr_library_sources(
        lib/stow/stow_serde.c
        lib/stow/stow_protocol.c
      )
    endif()

    if(CMAKE_VERSION VERSION_LESS 3.29)
      message(FATAL_ERROR
        "CONFIG_GANTRY_STOW requires CMake >= 3.29 for deferred Stow generation; found ${CMAKE_VERSION}")
    endif()

    set(_app_yaml ${APPLICATION_SOURCE_DIR}/stow.yaml)
    if(NOT EXISTS "${_app_yaml}")
      message(FATAL_ERROR "Expected stow.yaml at ${APPLICATION_SOURCE_DIR}")
    endif()

    set(GANTRY_STOW_YAML "${_app_yaml}" CACHE INTERNAL "")

    cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL gantry_stow_generate)
  endif()

  zephyr_library_compile_options(-Wall -Wextra -Wfatal-errors)

  gantry_register_analysis()
endif()
