project(MERCURY_TEST_DRIVER CXX)

include(CheckAsan)
include(CheckTsan)
include(CheckUbsan)

#----------------------------------------------------------------------------
# Build kwsys
#----------------------------------------------------------------------------
set(KWSYS_NAMESPACE mercury_sys)
set(KWSYS_USE_SystemTools 1)
set(KWSYS_USE_Process 1)
set(KWSYS_USE_RegularExpression 1)
add_subdirectory(kwsys)

#------------------------------------------------------------------------------
# Configure module header files
#------------------------------------------------------------------------------
if(MERCURY_TESTING_ENABLE_PARALLEL)
  find_package(MPI REQUIRED)
  message(STATUS "MPI include directory: ${MPI_INCLUDE_PATH}")
  set(HG_TEST_DRIVER_HAS_PARALLEL 1)
else()
  set(HG_TEST_DRIVER_HAS_PARALLEL 0)
endif()

set(MERCURY_TESTING_INIT_COMMAND "" CACHE STRING
  "Command to run before a client/server test begins. Multiple commands are separated by ';'.")
set(HG_TEST_DRIVER_INIT_COMMAND ${MERCURY_TESTING_INIT_COMMAND})
mark_as_advanced(MERCURY_TESTING_INIT_COMMAND)

configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/mercury_test_driver_config.h.in
  ${CMAKE_CURRENT_BINARY_DIR}/mercury_test_driver_config.h
)

#----------------------------------------------------------------------------
# Build driver
#----------------------------------------------------------------------------
set(MERCURY_TEST_DRIVER_BUILD_INCLUDE_DEPENDENCIES
  ${CMAKE_CURRENT_BINARY_DIR}/kwsys
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}
)

add_executable(mercury_test_driver mercury_test_driver.cxx)
target_include_directories(mercury_test_driver
  PUBLIC "$<BUILD_INTERFACE:${MERCURY_TEST_DRIVER_BUILD_INCLUDE_DEPENDENCIES}>"
)
target_link_libraries(mercury_test_driver mercury_sys)
