# - Try to find UCX
# Once done this will define
#  UCX_FOUND - System has UCX
#  UCX_INCLUDE_DIRS - The UCX include directories
#  UCX_LIBRARIES - The libraries needed to use UCX

find_package(PkgConfig)
pkg_check_modules(PC_UCX QUIET ucx)

find_path(UCX_INCLUDE_DIR ucp/api/ucp.h
  HINTS ${PC_UCX_INCLUDEDIR} ${PC_UCX_INCLUDE_DIRS})

find_library(UCP_LIBRARY NAMES ucp
  HINTS ${PC_UCX_LIBDIR} ${PC_UCX_LIBRARY_DIRS})

find_library(UCT_LIBRARY NAMES uct
  HINTS ${PC_UCX_LIBDIR} ${PC_UCX_LIBRARY_DIRS})

find_library(UCS_LIBRARY NAMES ucs
  HINTS ${PC_UCX_LIBDIR} ${PC_UCX_LIBRARY_DIRS})

if(UCX_INCLUDE_DIR AND EXISTS "${UCX_INCLUDE_DIR}/ucp/api/ucp_version.h")
  file(STRINGS "${UCX_INCLUDE_DIR}/ucp/api/ucp_version.h" UCP_VERSION_H REGEX "^#define UCP_API_MAJOR [ 0-9]+$")
  string(REGEX MATCH "[0-9]+$" UCX_VERSION_MAJOR "${UCP_VERSION_H}")

  file(STRINGS "${UCX_INCLUDE_DIR}/ucp/api/ucp_version.h" UCP_VERSION_H REGEX "^#define UCP_API_MINOR [ 0-9]+$")
  string(REGEX MATCH "[0-9]+$" UCX_VERSION_MINOR "${UCP_VERSION_H}")
  set(UCX_VERSION_STRING "${UCX_VERSION_MAJOR}.${UCX_VERSION_MINOR}")

  set(UCX_MAJOR_VERSION "${UCX_VERSION_MAJOR}")
  set(UCX_MINOR_VERSION "${UCX_VERSION_MINOR}")
endif()

set(UCX_INCLUDE_DIRS ${UCX_INCLUDE_DIR})
set(UCX_LIBRARIES ${UCP_LIBRARY} ${UCT_LIBRARY} ${UCS_LIBRARY})

include(FindPackageHandleStandardArgs)
# handle the QUIETLY and REQUIRED arguments and set UCX_FOUND to TRUE
# if all listed variables are TRUE
find_package_handle_standard_args(UCX REQUIRED_VARS UCX_INCLUDE_DIR UCP_LIBRARY UCT_LIBRARY UCS_LIBRARY
                                      VERSION_VAR UCX_VERSION_STRING)

mark_as_advanced(UCX_INCLUDE_DIR UCP_LIBRARY UCT_LIBRARY UCS_LIBRARY)

