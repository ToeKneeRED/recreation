function(recreation_embed_shaders target)
  set(headers)
  foreach(shader ${ARGN})
    get_filename_component(name ${shader} NAME)
    string(MAKE_C_IDENTIFIER ${name} symbol)
    set(spv ${CMAKE_CURRENT_BINARY_DIR}/shaders/${name}.spv)
    set(header ${CMAKE_BINARY_DIR}/generated/shaders/${symbol}.h)
    if(RECREATION_GLSLANG)
      set(compiler ${RECREATION_GLSLANG})
      set(compiler_dep)
    else()
      set(compiler $<TARGET_FILE:glslang-standalone>)
      set(compiler_dep glslang-standalone)
    endif()
    add_custom_command(OUTPUT ${spv}
      COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/shaders
      COMMAND ${compiler} -V --target-env vulkan1.3 -o ${spv}
              ${CMAKE_CURRENT_SOURCE_DIR}/${shader}
      DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${shader} ${compiler_dep}
      COMMENT "glsl ${name}")
    add_custom_command(OUTPUT ${header}
      COMMAND ${CMAKE_COMMAND} -DSPV=${spv} -DHEADER=${header} -DSYMBOL=${symbol}
              -P ${CMAKE_SOURCE_DIR}/cmake/embed_spv.cmake
      DEPENDS ${spv} ${CMAKE_SOURCE_DIR}/cmake/embed_spv.cmake
      COMMENT "embed ${name}")
    list(APPEND headers ${header})
  endforeach()
  add_custom_target(${target}_shaders DEPENDS ${headers})
  add_dependencies(${target} ${target}_shaders)
  target_include_directories(${target} PRIVATE ${CMAKE_BINARY_DIR}/generated)
endfunction()
