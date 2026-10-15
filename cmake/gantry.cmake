# gantry_stow_add_fragment(<path>)
#
# Registers an additional stow YAML fragment to be merged into the app's
# Stow definition, at lower precedence than the app's own stow.yaml (which
# is applied on top of all registered fragments) and lower than any
# board-specific fragment (which always wins).
#
# By convention fragment files end in `.stow.yaml`.
function(gantry_stow_add_fragment path)
  if(NOT IS_ABSOLUTE "${path}")
    set(path "${CMAKE_CURRENT_SOURCE_DIR}/${path}")
  endif()
  if(NOT EXISTS "${path}")
    message(FATAL_ERROR "gantry_stow_add_fragment: fragment not found: ${path}")
  endif()
  set_property(GLOBAL APPEND PROPERTY GANTRY_STOW_FRAGMENTS "${path}")
endfunction()

# _gantry_stow_run(WHAT <what> SCRIPT <script> OUTPUT_DIR <dir> YAML <path>
#                  [FRAGMENTS <path>...] [ARGS <arg>...])
#
# Runs a Stow generator script over a stow.yaml and its fragments, which
# become configure dependencies, and adds the generated headers to the
# include path.
function(_gantry_stow_run)
  cmake_parse_arguments(ARG "" "WHAT;SCRIPT;OUTPUT_DIR;YAML" "FRAGMENTS;ARGS" ${ARGN})

  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${ARG_YAML} ${ARG_FRAGMENTS})
  set(_fragment_args)
  foreach(_fragment ${ARG_FRAGMENTS})
    list(APPEND _fragment_args --fragment ${_fragment})
  endforeach()

  file(MAKE_DIRECTORY ${ARG_OUTPUT_DIR}/inc/stow)
  file(MAKE_DIRECTORY ${ARG_OUTPUT_DIR}/src/stow)

  find_program(GANTRY_UV_CMD NAMES uv REQUIRED)
  execute_process(
    COMMAND ${GANTRY_UV_CMD} run ${ZEPHYR_GANTRY_MODULE_DIR}/tools/stow/${ARG_SCRIPT}
            --yaml       ${ARG_YAML}
            ${_fragment_args}
            ${ARG_ARGS}
            --output-dir ${ARG_OUTPUT_DIR}
    WORKING_DIRECTORY ${ZEPHYR_GANTRY_MODULE_DIR}
    RESULT_VARIABLE _gen_result
  )
  if(NOT _gen_result EQUAL 0)
    message(FATAL_ERROR "Gantry ${ARG_WHAT} generation failed (exit ${_gen_result})")
  endif()

  zephyr_include_directories(${ARG_OUTPUT_DIR}/inc/stow)
endfunction()

# gantry_stow_generate([YAML <path>] [OUTPUT_DIR <dir>])
#
# Composes the app's stow.yaml with any fragments registered via
# gantry_stow_add_fragment() and any auto-discovered board fragment, runs the
# stow generator over the merged result, and attaches the generated C
# sources / headers to the calling CMakeLists' `app` target.
#
# YAML defaults to the GANTRY_STOW_YAML cache entry (set by the root
# CMakeLists.txt) and OUTPUT_DIR defaults to ${CMAKE_BINARY_DIR}/gantry_generated.
#
# NOTE: Invoked via a deferred call (see root CMakeLists.txt) so that
# fragments registered later in the app's own CMakeLists.txt are visible.
function(gantry_stow_generate)
  cmake_parse_arguments(ARG "" "YAML;OUTPUT_DIR" "" ${ARGN})

  if(NOT ARG_YAML)
    set(ARG_YAML "${GANTRY_STOW_YAML}")
  endif()
  if(NOT ARG_YAML)
    message(FATAL_ERROR "gantry_stow_generate: YAML <path> is required (or set GANTRY_STOW_YAML)")
  endif()
  if(NOT IS_ABSOLUTE "${ARG_YAML}")
    set(ARG_YAML "${CMAKE_CURRENT_SOURCE_DIR}/${ARG_YAML}")
  endif()
  if(NOT EXISTS "${ARG_YAML}")
    message(FATAL_ERROR "gantry_stow_generate: YAML not found: ${ARG_YAML}")
  endif()

  if(NOT ARG_OUTPUT_DIR)
    set(ARG_OUTPUT_DIR ${CMAKE_BINARY_DIR}/gantry_generated)
  endif()

  get_property(_fragments GLOBAL PROPERTY GANTRY_STOW_FRAGMENTS)

  set(_board_full "${BOARD}")
  if(BOARD_QUALIFIERS)
    set(_board_full "${BOARD}/${BOARD_QUALIFIERS}")
  endif()
  string(REPLACE "/" "_" _board_file "${_board_full}")
  set(_board_yaml "${APPLICATION_CONFIG_DIR}/boards/${_board_file}.stow.yaml")

  set(_board_args)
  if(EXISTS "${_board_yaml}")
    set(_board_args --board-yaml ${_board_yaml})
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_board_yaml})
  endif()

  set_property(GLOBAL PROPERTY GANTRY_STOW_MERGED_YAML ${ARG_OUTPUT_DIR}/merged.stow.yaml)

  message(STATUS "Gantry: generating stow from ${ARG_YAML} (${_fragments})")
  _gantry_stow_run(
    WHAT       stow
    SCRIPT     generate_stow.py
    OUTPUT_DIR ${ARG_OUTPUT_DIR}
    YAML       ${ARG_YAML}
    FRAGMENTS  ${_fragments}
    ARGS       ${_board_args} --app-dir ${APPLICATION_SOURCE_DIR}
  )

  file(GLOB _generated_struct_sources
    ${ARG_OUTPUT_DIR}/src/stow/generated_struct_*.c)

  target_sources(app PRIVATE
    ${ARG_OUTPUT_DIR}/src/stow/generated_stow_items.c
    ${ARG_OUTPUT_DIR}/src/stow/generated_stow_enums.c
    ${_generated_struct_sources}
  )
endfunction()

# gantry_stow_client_generate(PREFIX <prefix> YAML <path> [FRAGMENTS <path>...]
#                             [ITEMS <name>...] [OUTPUT_DIR <dir>])
#
# Generates Stow client bindings for a peer: the peer's stow.yaml is composed
# with its FRAGMENTS and validated as in the peer's own build, then the named
# ITEMS (every schema item when omitted) are bound under PREFIX, so several
# peers and the local Stow can share one image. When the build has a local
# Stow, peer enums and structs identical to local ones reuse the local types.
# The generated sources are attached to the `app` target.
#
# OUTPUT_DIR defaults to ${CMAKE_BINARY_DIR}/gantry_generated.
#
# NOTE: Generation is deferred until after the local Stow is generated.
function(gantry_stow_client_generate)
  cmake_parse_arguments(ARG "" "PREFIX;YAML;OUTPUT_DIR" "FRAGMENTS;ITEMS" ${ARGN})

  if(NOT ARG_PREFIX OR NOT ARG_YAML)
    message(FATAL_ERROR "gantry_stow_client_generate: PREFIX and YAML are required")
  endif()
  if(NOT ARG_OUTPUT_DIR)
    set(ARG_OUTPUT_DIR ${CMAKE_BINARY_DIR}/gantry_generated)
  endif()

  set(_inputs)
  foreach(_path ${ARG_YAML} ${ARG_FRAGMENTS})
    if(NOT IS_ABSOLUTE "${_path}")
      set(_path "${CMAKE_CURRENT_SOURCE_DIR}/${_path}")
    endif()
    if(NOT EXISTS "${_path}")
      message(FATAL_ERROR "gantry_stow_client_generate: not found: ${_path}")
    endif()
    list(APPEND _inputs ${_path})
  endforeach()
  list(POP_FRONT _inputs _yaml)

  set(_call "cmake_language(DEFER DIRECTORY [==[${CMAKE_SOURCE_DIR}]==] CALL _gantry_stow_client_run")
  foreach(_arg PREFIX ${ARG_PREFIX} OUTPUT_DIR ${ARG_OUTPUT_DIR} YAML ${_yaml} FRAGMENTS ${_inputs} ITEMS ${ARG_ITEMS})
    string(APPEND _call " [==[${_arg}]==]")
  endforeach()
  cmake_language(EVAL CODE "${_call})")
endfunction()

# _gantry_stow_client_run(PREFIX <prefix> OUTPUT_DIR <dir> YAML <path>
#                         [FRAGMENTS <path>...] [ITEMS <name>...])
#
# Generates the bindings gantry_stow_client_generate() deferred, once the
# local Stow's merged schema is known.
function(_gantry_stow_client_run)
  cmake_parse_arguments(ARG "" "PREFIX;OUTPUT_DIR;YAML" "FRAGMENTS;ITEMS" ${ARGN})

  set(_args --prefix ${ARG_PREFIX})
  foreach(_item ${ARG_ITEMS})
    list(APPEND _args --item ${_item})
  endforeach()
  get_property(_local_yaml GLOBAL PROPERTY GANTRY_STOW_MERGED_YAML)
  if(_local_yaml AND EXISTS "${_local_yaml}")
    list(APPEND _args --local-yaml ${_local_yaml})
  endif()

  message(STATUS "Gantry: generating Stow client bindings '${ARG_PREFIX}'")
  _gantry_stow_run(
    WHAT       "Stow client"
    SCRIPT     generate_stow_client.py
    OUTPUT_DIR ${ARG_OUTPUT_DIR}
    YAML       ${ARG_YAML}
    FRAGMENTS  ${ARG_FRAGMENTS}
    ARGS       ${_args}
  )

  file(GLOB _sources
    ${ARG_OUTPUT_DIR}/src/stow/generated_${ARG_PREFIX}_stow_client.c
    ${ARG_OUTPUT_DIR}/src/stow/generated_${ARG_PREFIX}_stow_enums.c
    ${ARG_OUTPUT_DIR}/src/stow/generated_${ARG_PREFIX}_struct_*.c)
  target_sources(app PRIVATE ${_sources})
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

  find_program(GANTRY_UV_CMD NAMES uv)
  if(GANTRY_UV_CMD)
    set(_resource_check_cmd
      ${GANTRY_UV_CMD} run --project ${ZEPHYR_GANTRY_MODULE_DIR}/tools/resource_check
      resource-check --annotations ${ZEPHYR_GANTRY_MODULE_DIR}/include)

    # The application's own headers are added so that its functions can carry annotations too.
    foreach(_app_inc ${APPLICATION_SOURCE_DIR}/inc ${APPLICATION_SOURCE_DIR}/include)
      if(IS_DIRECTORY "${_app_inc}")
        list(APPEND _resource_check_cmd --annotations ${_app_inc})
      endif()
    endforeach()

    if(CONFIG_GANTRY_ANALYSIS_CHECK_LIBRARY)
      set(_resource_check_dirs ${_module_lib} ${_app_src})
      set(_resource_check_comment "Gantry: running resource checker on library and application")
    else()
      set(_resource_check_dirs ${_app_src})
      set(_resource_check_comment "Gantry: running resource checker on application")
    endif()

    add_custom_target(gantry_resource_check ALL
      COMMAND ${_resource_check_cmd} ${_resource_check_dirs}
      WORKING_DIRECTORY ${APPLICATION_SOURCE_DIR}
      COMMENT "${_resource_check_comment}"
      VERBATIM)
    add_dependencies(app gantry_resource_check)
  else()
    message(WARNING "Gantry: uv not found; skipping resource-leak analysis")
  endif()
endfunction()
