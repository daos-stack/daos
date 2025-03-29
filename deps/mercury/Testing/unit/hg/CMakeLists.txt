if(MERCURY_TESTING_ENABLE_PARALLEL)
  find_package(MPI REQUIRED)
endif()

set(HG_TEST_FAIL_REGULAR_EXPRESSION "[^a-z]Error;ERROR;Failed")

# List of plugins that support forward to self
# set(NA_TESTING_SELF "sm;ofi")

# List of progress modes to test
set(NA_TESTING_NO_BLOCK "true;false")

#------------------------------------------------------------------------------
# Set up test macros
#------------------------------------------------------------------------------
if(${CMAKE_VERSION} VERSION_GREATER 3.12)
  add_library(mercury_unit OBJECT mercury_unit.c mercury_rpc_cb.c)
  target_link_libraries(mercury_unit mercury_test_common)
  if(MERCURY_ENABLE_COVERAGE)
    set_coverage_flags(mercury_unit)
  endif()
endif()

#
# hg prefix is added to executable
#
function(build_mercury_test test_name)
  if(${CMAKE_VERSION} VERSION_GREATER 3.12)
    add_executable(hg_test_${test_name} test_${test_name}.c)
    target_link_libraries(hg_test_${test_name} mercury_unit)
  else()
    add_executable(hg_test_${test_name} test_${test_name}.c mercury_unit.c mercury_rpc_cb.c)
    target_link_libraries(hg_test_${test_name} mercury_test_common)
  endif()
  if(MERCURY_ENABLE_COVERAGE)
    set_coverage_flags(hg_test_${test_name})
  endif()
endfunction()

macro(add_mercury_test test_name comm protocol busy parallel self scalable
  ignore_server_err)
  # Set full test name
  set(full_test_name ${test_name})
  set(opt_names ${comm} ${protocol})
  foreach(opt_name ${opt_names})
    set(full_test_name ${full_test_name}_${opt_name})
  endforeach()
  if(${busy})
    set(full_test_name ${full_test_name}_busy)
  endif()
  if(${self})
    set(full_test_name ${full_test_name}_self)
  endif()
  if(${scalable})
    set(full_test_name ${full_test_name}_scalable)
  endif()

  # Set test arguments
  set(test_args --comm ${comm} --protocol ${protocol})
  if(${busy})
    set(test_args ${test_args} --busy)
  endif()
  if(NOT ${parallel})
    set(numprocs 1)
  else()
    set(numprocs ${MPIEXEC_MAX_NUMPROCS})
  endif()
  if(${self})
    set(test_args ${test_args} --self_send)
  endif()
  if(${scalable})
    set(test_args ${test_args} -X 2)
  endif()
  if(${ignore_server_err})
    set(driver_args ${driver_args} --allow-server-errors)
  endif()

  if(NOT ${self})
    # Static client/server test (MPI only)
    if(${comm} STREQUAL "mpi" AND ${protocol} STREQUAL "static")
      set(static_test_args ${test_args} --mpi_static)
      # NB. always parallel
      add_test(NAME "mercury_${full_test_name}"
        COMMAND ${MPIEXEC} ${MPIEXEC_NUMPROC_FLAG} 1
        ${MPIEXEC_PREFLAGS} $<TARGET_FILE:hg_test_server> ${MPIEXEC_POSTFLAGS}
        ${static_test_args} : ${MPIEXEC_NUMPROC_FLAG} ${numprocs}
        ${MPIEXEC_PREFLAGS} $<TARGET_FILE:hg_test_${test_name}> ${static_test_args}
      )
      set_tests_properties("mercury_${full_test_name}" PROPERTIES
        FAIL_REGULAR_EXPRESSION ${HG_TEST_FAIL_REGULAR_EXPRESSION}
      )
    else()
      # Dynamic client/server test
      set(driver_args
        ${driver_args}
        --server $<TARGET_FILE:hg_test_server>       ${test_args}
        --client $<TARGET_FILE:hg_test_${test_name}> ${test_args})
      if(NOT ${parallel})
        set(driver_args ${driver_args} --serial)
      endif()
      add_test(NAME "mercury_${full_test_name}"
        COMMAND $<TARGET_FILE:mercury_test_driver>
        ${driver_args}
      )
    endif()
  else()
    # Test without server
    if(NOT ${parallel})
      add_test(NAME "mercury_${full_test_name}"
      COMMAND $<TARGET_FILE:hg_test_${test_name}> ${test_args}
      )
      set_tests_properties("mercury_${full_test_name}" PROPERTIES
        FAIL_REGULAR_EXPRESSION ${HG_TEST_FAIL_REGULAR_EXPRESSION}
      )
    else()
      add_test(NAME "mercury_${full_test_name}"
        COMMAND ${MPIEXEC} ${MPIEXEC_NUMPROC_FLAG} ${numprocs}
        ${MPIEXEC_PREFLAGS} $<TARGET_FILE:hg_test_${test_name}> ${MPIEXEC_POSTFLAGS}
        ${test_args}
      )
      set_tests_properties("mercury_${full_test_name}" PROPERTIES
        FAIL_REGULAR_EXPRESSION ${HG_TEST_FAIL_REGULAR_EXPRESSION}
      )
    endif()
  endif()
endmacro()

macro(add_mercury_test_standalone test_name)
  add_test(NAME mercury_${test_name} COMMAND $<TARGET_FILE:hg_test_${test_name}>)
endmacro()

# Loop over all combinations
function(add_mercury_test_comm test_name comm protocols progress_modes serial self ignore_server_err)
#  message("name=${test_name} comm=${comm} protocols=${protocols} progress_modes=${progress_modes} serial=${serial} self=${self}")
  foreach(protocol ${protocols})
    foreach(busy ${progress_modes})
      add_mercury_test(${test_name}
        ${comm} ${protocol} ${busy} ${serial} ${self} false ${ignore_server_err})
    endforeach()
  endforeach()
endfunction()

function(add_mercury_test_comm_scalable test_name comm protocols progress_modes serial)
  foreach(protocol ${protocols})
    foreach(busy ${progress_modes})
      # Restrict to OFI for now
      if(${comm} STREQUAL "ofi" AND ${protocol} STREQUAL "sockets")
          add_mercury_test(${test_name}
            ${comm} ${protocol} ${busy} ${serial} false true false)
      endif()
    endforeach()
  endforeach()
endfunction()

function(add_mercury_test_comm_all test_name)
  foreach(comm ${NA_PLUGINS})
    string(TOUPPER ${comm} upper_comm)
    # Forward to remote server
    add_mercury_test_comm(${test_name} ${comm}
      "${NA_${upper_comm}_TESTING_PROTOCOL}"
      "${NA_TESTING_NO_BLOCK}" ${MERCURY_TESTING_ENABLE_PARALLEL} false false)
    # Forward to self
    if(NOT ((${comm} STREQUAL "bmi") OR (${comm} STREQUAL "mpi")))
      add_mercury_test_comm(${test_name} ${comm}
        "${NA_${upper_comm}_TESTING_PROTOCOL}"
        "${NA_TESTING_NO_BLOCK}" false true false)
    endif()
    # Scalable test
    if(NOT APPLE)
      add_mercury_test_comm_scalable(${test_name} ${comm}
        "${NA_${upper_comm}_TESTING_PROTOCOL}"
        false ${MERCURY_TESTING_ENABLE_PARALLEL} true)
    endif()
  endforeach()
endfunction()

function(add_mercury_test_comm_all_serial test_name)
  foreach(comm ${NA_PLUGINS})
    string(TOUPPER ${comm} upper_comm)
    if(NOT ((${comm} STREQUAL "bmi")))
      # Forward to remote server
      add_mercury_test_comm(${test_name} ${comm}
        "${NA_${upper_comm}_TESTING_PROTOCOL}"
        "${NA_TESTING_NO_BLOCK}" false false false)
    endif()
    # Forward to self
    if(NOT ((${comm} STREQUAL "bmi") OR (${comm} STREQUAL "mpi")))
      add_mercury_test_comm(${test_name} ${comm}
        "${NA_${upper_comm}_TESTING_PROTOCOL}"
        "${NA_TESTING_NO_BLOCK}" false true false)
    endif()
  endforeach()
endfunction()

function(add_mercury_test_comm_kill_server test_name)
  foreach(comm ${NA_PLUGINS})
    string(TOUPPER ${comm} upper_comm)
    set(protocols "${NA_${upper_comm}_TESTING_PROTOCOL}")
    if (${comm} STREQUAL "ofi")
      # Do not run test with sockets provider because of fi_av_remove() issue
      list(REMOVE_ITEM protocols "sockets")
    endif()
    # Forward to remote server
    if(protocols AND (NOT ((${comm} STREQUAL "bmi") OR (${comm} STREQUAL "mpi") OR (${comm} STREQUAL "psm"))))
      add_mercury_test_comm(${test_name} ${comm} "${protocols}"
        "${NA_TESTING_NO_BLOCK}" false false true)
    endif()
  endforeach()
endfunction()

#------------------------------------------------------------------------------
# Tests and executables
#------------------------------------------------------------------------------

# Server used for testing
build_mercury_test(server)

# List of tests
build_mercury_test(rpc)
build_mercury_test(bulk)

build_mercury_test(lookup)
build_mercury_test(proc)

build_mercury_test(kill)

add_mercury_test_standalone(proc)

add_mercury_test_comm_all(rpc)
add_mercury_test_comm_all(bulk)

add_mercury_test_comm_kill_server(kill)
