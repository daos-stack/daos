#------------------------------------------------------------------------------
# Setup cmake module
#------------------------------------------------------------------------------
set(MERCURY_TEST_COMMON_CMAKE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/CMake")
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${MERCURY_TEST_COMMON_CMAKE_DIR})

#------------------------------------------------------------------------------
# Include source and build directories
#------------------------------------------------------------------------------
set(MERCURY_TEST_COMMON_BUILD_INCLUDE_DEPENDENCIES
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}
)
set(NA_TEST_COMMON_BUILD_INCLUDE_DEPENDENCIES
  ${MERCURY_TEST_COMMON_BUILD_INCLUDE_DEPENDENCIES}
)

#------------------------------------------------------------------------------
# External dependencies
#------------------------------------------------------------------------------

# Parallel testing
option(MERCURY_TESTING_ENABLE_PARALLEL
  "Enable testing in parallel (requires MPI)." OFF)
if(NA_USE_MPI AND NOT MERCURY_TESTING_ENABLE_PARALLEL)
  # Always force parallel testing if NA_USE_MPI is turned ON
  set(MERCURY_TESTING_ENABLE_PARALLEL "ON" CACHE BOOL
    "Enable testing in parallel (requires MPI)." FORCE)
endif()
if(MERCURY_TESTING_ENABLE_PARALLEL)
  find_package(MPI REQUIRED)
  message(STATUS "MPI include directory: ${MPI_INCLUDE_PATH}")
  set(HG_TEST_HAS_PARALLEL 1)
  set(NA_TEST_COMMON_EXT_INCLUDE_DEPENDENCIES
    ${NA_TEST_COMMON_EXT_INCLUDE_DEPENDENCIES}
    ${MPI_INCLUDE_PATH}
  )
  set(NA_TEST_COMMON_EXT_LIB_DEPENDENCIES
    ${NA_TEST_COMMON_EXT_LIB_DEPENDENCIES}
    ${MPI_LIBRARIES}
  )
else()
  set(HG_TEST_HAS_PARALLEL 0)
endif()
mark_as_advanced(MERCURY_TESTING_ENABLE_PARALLEL)

# Detect <sys/prctl.h>
check_include_files("sys/prctl.h" HG_TEST_HAS_SYSPRCTL_H)

# CXI
if(NA_USE_OFI)
  option(NA_OFI_TESTING_USE_CXI
    "Use CXI library for service configuration." OFF)
  mark_as_advanced(NA_OFI_TESTING_USE_CXI)
  if(NA_OFI_TESTING_USE_CXI)
    find_package(CXI REQUIRED)
    message(STATUS "CXI include directory: ${CXI_INCLUDE_DIRS}")
    set(HG_TEST_HAS_CXI 1)
    set(NA_TEST_COMMON_EXT_INCLUDE_DEPENDENCIES
      ${NA_TEST_COMMON_EXT_INCLUDE_DEPENDENCIES}
      ${CXI_INCLUDE_DIRS}
    )
    set(NA_TEST_COMMON_EXT_LIB_DEPENDENCIES
      ${NA_TEST_COMMON_EXT_LIB_DEPENDENCIES}
      ${CXI_LIBRARIES}
    )
  endif()
endif()

#------------------------------------------------------------------------------
# Set up test options
#------------------------------------------------------------------------------
set(MERCURY_TESTING_TEMP_DIRECTORY "." CACHE PATH
  "Location to use for temp data (default is current directory).")
set(HG_TEST_TEMP_DIRECTORY ${MERCURY_TESTING_TEMP_DIRECTORY})
mark_as_advanced(MERCURY_TESTING_TEMP_DIRECTORY)

#------------------------------------------------------------------------------
# Configure module header files
#------------------------------------------------------------------------------
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/mercury_test_config.h.in
  ${CMAKE_CURRENT_BINARY_DIR}/mercury_test_config.h
)

#------------------------------------------------------------------------------
# Set sources
#------------------------------------------------------------------------------
set(NA_TEST_COMMON_SRCS
  ${CMAKE_CURRENT_SOURCE_DIR}/na_test.c
  ${CMAKE_CURRENT_SOURCE_DIR}/na_test_getopt.c
  ${CMAKE_CURRENT_SOURCE_DIR}/na_test_mpi.c
)

set(MERCURY_TEST_COMMON_SRCS
  ${CMAKE_CURRENT_SOURCE_DIR}/mercury_test.c
)

#------------------------------------------------------------------------------
# Specify project private header files
#------------------------------------------------------------------------------
set(NA_TEST_COMMON_PRIVATE_HEADERS
  ${CMAKE_CURRENT_BINARY_DIR}/mercury_test_config.h
  ${CMAKE_CURRENT_SOURCE_DIR}/na_test.h
  ${CMAKE_CURRENT_SOURCE_DIR}/na_test_getopt.h
  ${CMAKE_CURRENT_SOURCE_DIR}/na_test_mpi.h
)

set(MERCURY_TEST_COMMON_PRIVATE_HEADERS
  ${CMAKE_CURRENT_BINARY_DIR}/mercury_test_config.h
  ${CMAKE_CURRENT_SOURCE_DIR}/mercury_test.h
)

#----------------------------------------------------------------------------
# Libraries
#----------------------------------------------------------------------------

# NA test common
add_library(na_test_common STATIC ${NA_TEST_COMMON_SRCS}
  ${NA_TEST_COMMON_PRIVATE_HEADERS})
target_include_directories(na_test_common
  PUBLIC  "$<BUILD_INTERFACE:${NA_TEST_COMMON_BUILD_INCLUDE_DEPENDENCIES}>"
)
target_include_directories(na_test_common
  SYSTEM PUBLIC ${NA_TEST_COMMON_EXT_INCLUDE_DEPENDENCIES}
)
target_link_libraries(na_test_common na mercury_util
  ${NA_TEST_COMMON_EXT_LIB_DEPENDENCIES}
)
if(MSVC)
  target_compile_definitions(na_test_common PRIVATE -D_CRT_SECURE_NO_WARNINGS)
endif()
if(BUILD_SHARED_LIBS)
  set_property(TARGET na_test_common PROPERTY POSITION_INDEPENDENT_CODE TRUE)
endif()
if(MERCURY_ENABLE_COVERAGE)
  set_coverage_flags(na_test_common)
endif()

# Mercury test common
add_library(mercury_test_common STATIC ${MERCURY_TEST_COMMON_SRCS}
  ${MERCURY_TEST_COMMON_PRIVATE_HEADERS})
target_include_directories(mercury_test_common
  PUBLIC  "$<BUILD_INTERFACE:${MERCURY_TEST_COMMON_BUILD_INCLUDE_DEPENDENCIES}>"
)
target_include_directories(mercury_test_common
  SYSTEM PUBLIC ${MERCURY_TEST_COMMON_EXT_INCLUDE_DEPENDENCIES}
)
target_link_libraries(mercury_test_common mercury na_test_common
  ${MERCURY_TEST_COMMON_EXT_LIB_DEPENDENCIES}
)
if(MSVC)
  target_compile_definitions(mercury_test_common PRIVATE -D_CRT_SECURE_NO_WARNINGS)
endif()
if(BUILD_SHARED_LIBS)
  set_property(TARGET mercury_test_common PROPERTY POSITION_INDEPENDENT_CODE TRUE)
endif()
if(MERCURY_ENABLE_COVERAGE)
  set_coverage_flags(mercury_test_common)
endif()
