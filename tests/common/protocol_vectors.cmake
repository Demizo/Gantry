# protocol_vectors_generate(<target> <folder> <prefix>)
#
# Renders the vectors of a protocol folder as C data named <prefix>_*,
# declared by protocol_vectors.h, and adds it to <target>. Editing any file
# in the folder, or the tool, renders it again.
function(protocol_vectors_generate target folder prefix)
  find_program(UV_CMD NAMES uv REQUIRED)
  set(tool ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../tools/protocol_spec)
  set(output ${CMAKE_CURRENT_BINARY_DIR}/${prefix}_vectors.c)
  file(GLOB inputs ${folder}/* ${tool}/src/protocol_spec/*.py ${tool}/src/protocol_spec/templates/*)
  add_custom_command(
    OUTPUT ${output}
    COMMAND ${UV_CMD} run --project ${tool}
            protocol-spec c ${folder} --prefix ${prefix} --output ${output}
    DEPENDS ${inputs}
    COMMENT "Rendering the ${prefix} protocol vectors"
    VERBATIM
  )
  target_sources(${target} PRIVATE ${output})
  target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_FUNCTION_LIST_DIR})
endfunction()
