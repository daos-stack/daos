#-----------------------------------------------------------------------------
# Create executables
#-----------------------------------------------------------------------------
if(${CMAKE_VERSION} VERSION_GREATER 3.12)
  add_library(na_perf OBJECT na_perf.c)
  target_link_libraries(na_perf na_test_common)
  if(BUILD_SHARED_LIBS)
    set_property(TARGET na_perf PROPERTY POSITION_INDEPENDENT_CODE TRUE)
  endif()
  if(MERCURY_ENABLE_COVERAGE)
    set_coverage_flags(na_perf)
  endif()
endif()

set(NA_PERF_TARGETS na_lat na_bw_put na_bw_get na_perf_server)
foreach(perf ${NA_PERF_TARGETS})
  if(${CMAKE_VERSION} VERSION_GREATER 3.12)
    add_executable(${perf} ${perf}.c)
    target_link_libraries(${perf} na_perf)
  else()
    add_executable(${perf} ${perf}.c na_perf.c)
    target_link_libraries(${perf} na_test_common)
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
    ${NA_PERF_TARGETS}
  RUNTIME DESTINATION ${MERCURY_INSTALL_BIN_DIR}
)