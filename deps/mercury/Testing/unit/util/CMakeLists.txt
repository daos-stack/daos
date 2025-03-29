#------------------------------------------------------------------------------
# Include source and build directories
#------------------------------------------------------------------------------
set(MERCURY_TEST_UTIL_BUILD_INCLUDE_DEPENDENCIES
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}
)

#------------------------------------------------------------------------------
# Mercury util tests
#------------------------------------------------------------------------------
#
# hg_prefix is added to executable
#
function(add_mercury_test_util test_name)
  add_executable(hg_test_${test_name} test_${test_name}.c)
  target_include_directories(hg_test_${test_name}
    PUBLIC  "$<BUILD_INTERFACE:${MERCURY_TEST_UTIL_BUILD_INCLUDE_DEPENDENCIES}>"
  )
  target_link_libraries(hg_test_${test_name} mercury_util)
  if(MERCURY_ENABLE_COVERAGE)
    set_coverage_flags(hg_test_${test_name})
  endif()
  add_test(NAME mercury_util_${test_name} COMMAND $<TARGET_FILE:hg_test_${test_name}>)
endfunction()

# add library for dl test
add_library(hg_test_dl_module MODULE test_dl_module.c)
target_include_directories(hg_test_dl_module
  PUBLIC  "$<BUILD_INTERFACE:${MERCURY_TEST_UTIL_BUILD_INCLUDE_DEPENDENCIES}>"
)
target_link_libraries(hg_test_dl_module mercury_util)

# HG_TEST_UTIL_OUTPUT_DIRECTORY
get_target_property(HG_TEST_UTIL_OUTPUT_DIRECTORY hg_test_dl_module LIBRARY_OUTPUT_DIRECTORY)

#------------------------------------------------------------------------------
# Configure module header files
#------------------------------------------------------------------------------
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/mercury_test_util_config.h.in
  ${CMAKE_CURRENT_BINARY_DIR}/mercury_test_util_config.h
)

#------------------------------------------------------------------------------
# Set list of tests
set(MERCURY_util_tests
  atomic
  atomic_queue
  hash_table
  mem
  mem_pool
  poll
  request
  thread
  thread_condition
  thread_mutex
  thread_spin
  threadpool
  time
)

if(NOT WIN32)
  set(MERCURY_util_tests ${MERCURY_util_tests} dl)
endif()

foreach(test_name ${MERCURY_util_tests})
  add_mercury_test_util(${test_name})
endforeach()
