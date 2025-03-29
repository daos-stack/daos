#------------------------------------------------------------------------------
# Include source and build directories
#------------------------------------------------------------------------------
set(MERCURY_BUILD_INCLUDE_DEPENDENCIES
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}
)

#-----------------------------------------------------------------------------
# External dependencies
#-----------------------------------------------------------------------------
find_package(json-c CONFIG)
if(json-c_FOUND)
  set(MERCURY_INFO_INT_LIB_DEPENDENCIES
    ${MERCURY_INFO_INT_LIB_DEPENDENCIES}
    json-c::json-c
  )
  set(HG_INFO_HAS_JSON 1)
endif()

#------------------------------------------------------------------------------
# Configure module header files
#------------------------------------------------------------------------------
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/config.h.in
  ${CMAKE_CURRENT_BINARY_DIR}/config.h
)

#-----------------------------------------------------------------------------
# Create executable
#-----------------------------------------------------------------------------
add_executable(hg_info info.c getopt.c)
target_include_directories(hg_info
  PRIVATE "$<BUILD_INTERFACE:${MERCURY_BUILD_INCLUDE_DEPENDENCIES}>"
)
target_link_libraries(hg_info PRIVATE mercury
  ${MERCURY_INFO_INT_LIB_DEPENDENCIES})
mercury_set_exe_options(hg_info MERCURY)
if(MERCURY_ENABLE_COVERAGE)
  set_coverage_flags(hg_info)
endif()

#-----------------------------------------------------------------------------
# Add Target(s) to CMake Install
#-----------------------------------------------------------------------------
install(
  TARGETS
    hg_info
  RUNTIME DESTINATION ${MERCURY_INSTALL_BIN_DIR}
)