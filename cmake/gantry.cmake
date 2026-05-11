# gantry_stow_generate(YAML <path> [OUTPUT_DIR <dir>])
#
# Runs the stow generator over the given YAML and attaches the generated
# C sources / headers to the calling CMakeLists' `app` target.
#
# OUTPUT_DIR defaults to ${CMAKE_CURRENT_BINARY_DIR}/gantry_generated.
function(gantry_stow_generate)
  cmake_parse_arguments(ARG "" "YAML;OUTPUT_DIR" "" ${ARGN})

  if(NOT ARG_YAML)
    message(FATAL_ERROR "gantry_stow_generate: YAML <path> is required")
  endif()
  if(NOT IS_ABSOLUTE "${ARG_YAML}")
    set(ARG_YAML "${CMAKE_CURRENT_SOURCE_DIR}/${ARG_YAML}")
  endif()
  if(NOT EXISTS "${ARG_YAML}")
    message(FATAL_ERROR "gantry_stow_generate: YAML not found: ${ARG_YAML}")
  endif()

  if(NOT ARG_OUTPUT_DIR)
    set(ARG_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/gantry_generated)
  endif()

  find_program(GANTRY_UV_CMD NAMES uv REQUIRED)
  set(_gen_dir ${ZEPHYR_GANTRY_MODULE_DIR}/tools/stow)

  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${ARG_YAML})

  file(MAKE_DIRECTORY ${ARG_OUTPUT_DIR}/inc/stow)
  file(MAKE_DIRECTORY ${ARG_OUTPUT_DIR}/src/stow)

  message(STATUS "Gantry: generating stow from ${ARG_YAML}")
  execute_process(
    COMMAND ${GANTRY_UV_CMD} run ${_gen_dir}/generate_stow.py
            --yaml       ${ARG_YAML}
            --output-dir ${ARG_OUTPUT_DIR}
    WORKING_DIRECTORY ${ZEPHYR_GANTRY_MODULE_DIR}
    RESULT_VARIABLE _gen_result
  )
  if(NOT _gen_result EQUAL 0)
    message(FATAL_ERROR "Gantry stow generation failed (exit ${_gen_result})")
  endif()

  file(GLOB _generated_struct_sources
    ${ARG_OUTPUT_DIR}/src/stow/generated_struct_*.c)

  target_sources(app PRIVATE
    ${ARG_OUTPUT_DIR}/src/stow/generated_stow_items.c
    ${ARG_OUTPUT_DIR}/src/stow/generated_stow_enums.c
    ${_generated_struct_sources}
  )
  zephyr_include_directories(${ARG_OUTPUT_DIR}/inc/stow)
endfunction()

# gantry_register_analysis()
#
# Wires up analysis targets for the consumer application's src/ directory.
# The resource checker always runs against the app. Cppcheck runs only when
# CONFIG_GANTRY_ANALYSIS_CPPCHECK=y. Both tools also cover the module's lib/ when
# CONFIG_GANTRY_ANALYSIS_CHECK_LIBRARY=y.
function(gantry_register_analysis)
  set(_module_lib ${ZEPHYR_GANTRY_MODULE_DIR}/lib)
  set(_app_src ${APPLICATION_SOURCE_DIR}/src)

  if(CONFIG_GANTRY_ANALYSIS_CPPCHECK)
    if(CONFIG_GANTRY_ANALYSIS_CHECK_LIBRARY)
      set(_cppcheck_dirs ${_module_lib} ${_app_src})
      set(_cppcheck_comment "Gantry: running cppcheck on library and application")
    else()
      set(_cppcheck_dirs ${_app_src})
      set(_cppcheck_comment "Gantry: running cppcheck on application")
    endif()

    find_program(GANTRY_CPPCHECK_CMD NAMES cppcheck)
    if(GANTRY_CPPCHECK_CMD)
      add_custom_target(gantry_cppcheck ALL
        COMMAND ${GANTRY_CPPCHECK_CMD}
          --quiet
          --inline-suppr
          --enable=warning,performance,portability
          --check-level=exhaustive
          --error-exitcode=1
          --inconclusive
          ${_cppcheck_dirs}
        WORKING_DIRECTORY ${APPLICATION_SOURCE_DIR}
        COMMENT "${_cppcheck_comment}"
        VERBATIM)
      add_dependencies(app gantry_cppcheck)
    else()
      message(WARNING "Gantry: cppcheck not found; skipping static analysis")
    endif()
  endif()

  set(_cocci ${ZEPHYR_GANTRY_MODULE_DIR}/tools/resource_check/generated_resource_check.cocci)
  find_program(GANTRY_SPATCH_CMD NAMES spatch)
  if(GANTRY_SPATCH_CMD AND EXISTS "${_cocci}")
    if(CONFIG_GANTRY_ANALYSIS_CHECK_LIBRARY)
      set(_spatch_comment "Gantry: running resource checker on library and application")
      add_custom_target(gantry_spatch ALL
        COMMAND ${GANTRY_SPATCH_CMD}
          --sp-file ${_cocci}
          --dir     ${_module_lib}
          --no-includes
          --very-quiet
        COMMAND ${GANTRY_SPATCH_CMD}
          --sp-file ${_cocci}
          --dir     ${_app_src}
          --no-includes
          --very-quiet
        WORKING_DIRECTORY ${APPLICATION_SOURCE_DIR}
        COMMENT "${_spatch_comment}"
        VERBATIM)
    else()
      add_custom_target(gantry_spatch ALL
        COMMAND ${GANTRY_SPATCH_CMD}
          --sp-file ${_cocci}
          --dir     ${_app_src}
          --no-includes
          --very-quiet
        WORKING_DIRECTORY ${APPLICATION_SOURCE_DIR}
        COMMENT "Gantry: running resource checker on application"
        VERBATIM)
    endif()
    add_dependencies(app gantry_spatch)
  elseif(NOT GANTRY_SPATCH_CMD)
    message(WARNING "Gantry: spatch not found; skipping resource-leak analysis")
  endif()
endfunction()
