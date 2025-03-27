#-----------------------------------------------------------------------------
# Create executables
#-----------------------------------------------------------------------------
if(${CMAKE_VERSION} VERSION_GREATER 3.12)
  add_library(mercury_perf OBJECT mercury_perf.c)
  target_link_libraries(mercury_perf mercury_test_common)
  if(BUILD_SHARED_LIBS)
    set_property(TARGET mercury_perf PROPERTY POSITION_INDEPENDENT_CODE TRUE)
  endif()
  if(MERCURY_ENABLE_COVERAGE)
    set_coverage_flags(mercury_perf)
  endif()
endif()

set(HG_PERF_TARGETS hg_rate hg_first hg_bw_read hg_bw_write hg_perf_server)
foreach(perf ${HG_PERF_TARGETS})
  if(${CMAKE_VERSION} VERSION_GREATER 3.12)
    add_executable(${perf} ${perf}.c)
    target_link_libraries(${perf} mercury_perf)
  else()
    add_executable(${perf} ${perf}.c mercury_perf.c)
    target_link_libraries(${perf} mercury_test_common)
  endif()
  mercury_set_exe_options(${perf} MERCURY)
  if(MERCURY_ENABLE_COVERAGE)
    set_coverage_flags(${perf})
  endif()
endforeach()

#-----------------------------------------------------------------------------
# Add Target(s) to CMake Install
#-----------------------------------------------------------------------------
install(
  TARGETS
    ${HG_PERF_TARGETS}
  RUNTIME DESTINATION ${MERCURY_INSTALL_BIN_DIR}
)