#------------------------------------------------------------------------------
# Include source and build directories
#------------------------------------------------------------------------------
include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}
)

#------------------------------------------------------------------------------
# External dependencies
#------------------------------------------------------------------------------
# mercury
find_package(MERCURY REQUIRED)

# Snappy
find_package(SNAPPY)
if(SNAPPY_FOUND)
  message(STATUS "Snappy include directory: ${SNAPPY_INCLUDE_DIR}")
  include_directories(${SNAPPY_INCLUDE_DIR})
else()
  message(STATUS "Could not find Snappy.")
endif()

#------------------------------------------------------------------------------
# Examples
#------------------------------------------------------------------------------
function(build_rpc_example example_name)
  add_executable(hg_example_${example_name} example_rpc.c example_rpc_engine.c example_${example_name}.c)
  target_link_libraries(hg_example_${example_name} mercury)
endfunction()

function(build_snappy_example example_name)
  add_executable(hg_example_${example_name} example_snappy.c example_${example_name}.c)
  target_link_libraries(hg_example_${example_name} mercury ${SNAPPY_LIBRARIES})
endfunction()

build_rpc_example(rpc_client)
build_rpc_example(rpc_server)

if(SNAPPY_FOUND)
  build_snappy_example(snappy_client)
  build_snappy_example(snappy_server)
endif()

