if(CONFIG_ZDS)
  zephyr_include_directories(include)
endif()

if(CONFIG_ZDS_BUILD_LIBRARY)
  zephyr_library_named(zds)

  zephyr_library_sources(lib/string_utils.c)

  if(CONFIG_GANTRY_MEMORY)
    zephyr_library_sources(lib/memory.c)
  endif()

  if(CONFIG_GANTRY_EVENTS)
    zephyr_library_sources(lib/event.c)
  endif()

  include(${CMAKE_CURRENT_LIST_DIR}/cmake/zds.cmake)

  if(CONFIG_GANTRY_STOW)
    zephyr_library_sources(
      lib/stow/stow.c
      lib/stow/stow_describe.c
      lib/stow/stow_event.c
      lib/stow/stow_storage.c
      lib/stow/types/stow_type_enum.c
      lib/stow/types/stow_type_int.c
      lib/stow/types/stow_type_float.c
      lib/stow/types/stow_type_string.c
      lib/stow/types/stow_type_byte_array.c
      lib/stow/types/stow_type_buffer.c
    )

    set(_app_yaml ${APPLICATION_SOURCE_DIR}/stow.yaml)
    if(NOT EXISTS "${_app_yaml}")
      message(FATAL_ERROR "Expected stow.yaml at ${APPLICATION_SOURCE_DIR}")
    endif()
    zds_stow_generate(YAML ${_app_yaml})
  endif()

  zephyr_library_compile_options(-Wall -Wextra -Wfatal-errors)

  zds_register_analysis()
endif()
