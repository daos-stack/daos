#------------------------------------------------------------------------------
# NA protocols used for testing
# (case where the NA plugin defines multiple protocols)
#------------------------------------------------------------------------------
if(NA_USE_BMI)
  set(NA_BMI_TESTING_PROTOCOL "tcp" CACHE STRING "Protocol(s) used for testing (e.g., tcp;ib).")
  mark_as_advanced(NA_BMI_TESTING_PROTOCOL)
endif()

if(NA_USE_UCX)
  set(NA_UCX_TESTING_PROTOCOL "all" CACHE STRING "Protocol(s) used for testing (e.g., tcp;ib).")
  mark_as_advanced(NA_UCX_TESTING_PROTOCOL)
endif()

if(NA_USE_MPI)
  set(NA_MPI_TESTING_PROTOCOL "dynamic;static" CACHE STRING "Protocol(s) used for testing (e.g., dynamic;static).")
  mark_as_advanced(NA_MPI_TESTING_PROTOCOL)
endif()

if(NA_USE_OFI)
  set(NA_OFI_TESTING_PROTOCOL "sockets;tcp" CACHE STRING "Protocol(s) used for testing (e.g., sockets;psm2;verbs).")
  mark_as_advanced(NA_OFI_TESTING_PROTOCOL)
endif()

if(NA_USE_SM)
  set(NA_SM_TESTING_PROTOCOL "sm" CACHE STRING "Protocol(s) used for testing (e.g., sm).")
  mark_as_advanced(NA_SM_TESTING_PROTOCOL)
endif()

if(NA_USE_PSM)
  set(NA_PSM_TESTING_PROTOCOL "psm" CACHE STRING "Protocol(s) used for testing (e.g., psm).")
  mark_as_advanced(NA_PSM_TESTING_PROTOCOL)
endif()

if(NA_USE_PSM2)
  set(NA_PSM2_TESTING_PROTOCOL "psm2" CACHE STRING "Protocol(s) used for testing (e.g., psm2).")
  mark_as_advanced(NA_PSM2_TESTING_PROTOCOL)
endif()

#------------------------------------------------------------------------------
# Options
#------------------------------------------------------------------------------
option(BUILD_TESTING_UNIT "Build unit tests." ON)
option(BUILD_TESTING_PERF "Build performance tests." ON)
mark_as_advanced(BUILD_TESTING_UNIT BUILD_TESTING_PERF)

#------------------------------------------------------------------------------
# Common libraries
#------------------------------------------------------------------------------
add_subdirectory(common)

#------------------------------------------------------------------------------
# Compile kwsys library and setup TestDriver
#------------------------------------------------------------------------------
if(BUILD_TESTING_UNIT)
  add_subdirectory(driver)
endif()

#------------------------------------------------------------------------------
# Unit tests
#------------------------------------------------------------------------------
if(BUILD_TESTING_UNIT)
  add_subdirectory(unit)
endif()

#------------------------------------------------------------------------------
# Perf tests
#------------------------------------------------------------------------------
if(BUILD_TESTING_PERF)
  add_subdirectory(perf)
endif()