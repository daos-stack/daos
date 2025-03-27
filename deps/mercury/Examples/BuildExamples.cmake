#------------------------------------------------------------------------------
# Builds the examples as a separate project using a custom target.
# This is included in Mercury/CMakeLists.txt to build examples as a separate
# project.

#------------------------------------------------------------------------------
# Make sure it uses the same build configuration as Mercury.
if(CMAKE_CONFIGURATION_TYPES)
  set(build_config_arg -C "${CMAKE_CFG_INTDIR}")
else()
  set(build_config_arg)
endif()

set(extra_params)
foreach(flag CMAKE_C_FLAGS_DEBUG
             CMAKE_C_FLAGS_RELEASE
             CMAKE_C_FLAGS_MINSIZEREL
             CMAKE_C_FLAGS_RELWITHDEBINFO
             CMAKE_CXX_FLAGS_DEBUG
             CMAKE_CXX_FLAGS_RELEASE
             CMAKE_CXX_FLAGS_MINSIZEREL
             CMAKE_CXX_FLAGS_RELWITHDEBINFO)
  if(${${flag}})
    set(extra_params ${extra_params}
      -D${flag}:STRING=${${flag}})
  endif()
endforeach()

set(examples_dependencies
  mercury
  )

add_custom_command(
  OUTPUT "${MERCURY_BINARY_DIR}/MercuryExamples.done"
  COMMAND ${CMAKE_CTEST_COMMAND}
  ARGS ${build_config_arg}
       --build-and-test
       ${MERCURY_SOURCE_DIR}/Examples
       ${MERCURY_BINARY_DIR}/Examples
       --build-noclean
       --build-two-config
       --build-project MercuryExamples
       --build-generator ${CMAKE_GENERATOR}
       --build-makeprogram ${CMAKE_MAKE_PROGRAM}
       --build-options -DMERCURY_DIR:PATH=${MERCURY_BINARY_DIR}
                       -DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}
                       -DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}
                       -DCMAKE_C_FLAGS:STRING=${CMAKE_C_FLAGS}
                       -DCMAKE_LIBRARY_OUTPUT_DIRECTORY:PATH=${CMAKE_LIBRARY_OUTPUT_DIRECTORY}
                       -DCMAKE_RUNTIME_OUTPUT_DIRECTORY:PATH=${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
                       ${extra_params}
                       --no-warn-unused-cli
  COMMAND ${CMAKE_COMMAND} -E touch
          "${MERCURY_BINARY_DIR}/MercuryExamples.done"
  COMMENT "Build examples as a separate project"
  DEPENDS ${examples_dependencies}
)

# Add custom target to ensure that the examples get built.
add_custom_target(examples ALL DEPENDS
  "${MERCURY_BINARY_DIR}/MercuryExamples.done")
