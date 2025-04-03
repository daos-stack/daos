#------------------------------------------------------------------------------
# External dependencies
#------------------------------------------------------------------------------
# For htonl etc
if(WIN32)
  set(MERCURY_TEST_EXT_LIB_DEPENDENCIES ws2_32 ${MERCURY_TEST_EXT_LIB_DEPENDENCIES})
endif(WIN32)

#------------------------------------------------------------------------------
# Set up test macros
#------------------------------------------------------------------------------
if(${CMAKE_VERSION} VERSION_GREATER 3.12)
  add_library(na_test_common_lookup OBJECT na_test_common.c)
  target_link_libraries(na_test_common_lookup na_test_common)
  if(MERCURY_ENABLE_COVERAGE)
    set_coverage_flags(na_test_common_lookup)
  endif()
endif()

#
# na prefix is added to executable
#
function(build_na_test_lookup test_name)
  if(${CMAKE_VERSION} VERSION_GREATER 3.12)
    add_executable(na_test_${test_name} test_${test_name}.c)
    target_link_libraries(na_test_${test_name} na_test_common_lookup)
  else()
    add_executable(na_test_${test_name} test_${test_name}.c na_test_common.c)
    target_link_libraries(na_test_${test_name} na_test_common)
  endif()
  if(MERCURY_ENABLE_COVERAGE)
    set_coverage_flags(na_test_${test_name})
  endif()
endfunction()

macro(add_na_test_comm test_name server client comm protocol)
  # Set full test name
  set(full_test_name ${test_name})
  set(opt_names ${comm} ${protocol})
  foreach(opt_name ${opt_names})
    set(full_test_name ${full_test_name}_${opt_name})
  endforeach()

  # Set test arguments
  set(test_args --comm ${comm})
  set(test_args ${test_args} --protocol ${protocol})

  # Static client/server test
  if(${comm} STREQUAL "mpi" AND ${protocol} STREQUAL "static")
    set(static_test_args ${test_args} --mpi_static)
    add_test(NAME "na_${full_test_name}"
      COMMAND ${MPIEXEC} ${MPIEXEC_NUMPROC_FLAG} 1
      ${MPIEXEC_PREFLAGS} $<TARGET_FILE:na_test_${server}> ${MPIEXEC_POSTFLAGS}
      ${static_test_args} : ${MPIEXEC_NUMPROC_FLAG} ${MPIEXEC_MAX_NUMPROCS}
      ${MPIEXEC_PREFLAGS} $<TARGET_FILE:na_test_${client}> ${static_test_args}
    )
  else()
    # Dynamic client/server test
    add_test(NAME "na_${full_test_name}"
      COMMAND $<TARGET_FILE:mercury_test_driver>
      --server $<TARGET_FILE:na_test_${server}>
      --client $<TARGET_FILE:na_test_${client}> ${test_args}
    )
  endif()
endmacro()

function(add_na_test test_name server client)
  foreach(comm ${NA_PLUGINS})
    string(TOUPPER ${comm} upper_comm)
    foreach(protocol ${NA_${upper_comm}_TESTING_PROTOCOL})
      add_na_test_comm(${test_name} ${server} ${client} ${comm} ${protocol})
    endforeach()
  endforeach()
endfunction()

#------------------------------------------------------------------------------
# Network abstraction test
#------------------------------------------------------------------------------
build_na_test_lookup(lookup)
build_na_test_lookup(lookup_server)

#------------------------------------------------------------------------------
# Set list of tests

# Client / server test with all enabled NA plugins
#add_na_test(simple server client)
#add_na_test(cancel cancel_server cancel_client)
