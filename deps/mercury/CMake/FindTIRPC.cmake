# - Try to find TIRPC
# Once done this will define
#  TIRPC_FOUND - System has TIRPC
#  TIRPC_INCLUDE_DIRS - The TIRPC include directories
#  TIRPC_LIBRARIES - The libraries needed to use TIRPC

find_package(PkgConfig)
pkg_check_modules(PC_TIRPC libtirpc)

find_path(TIRPC_INCLUDE_DIR rpc/xdr.h
  HINTS ${PC_TIRPC_INCLUDEDIR} ${PC_TIRPC_INCLUDE_DIRS}
  PATHS /usr/local/include /usr/include /usr/include/tirpc)

find_library(TIRPC_LIBRARY NAMES tirpc
  HINTS ${PC_DRC_LIBDIR} ${PC_DRC_LIBRARY_DIRS}
  PATHS /usr/local/lib64 /usr/local/lib /usr/lib64 /usr/lib)

set(TIRPC_INCLUDE_DIRS ${TIRPC_INCLUDE_DIR})
set(TIRPC_LIBRARIES ${TIRPC_LIBRARY})

include(FindPackageHandleStandardArgs)
# handle the QUIETLY and REQUIRED arguments and set TIRPC_FOUND to TRUE
# if all listed variables are TRUE
find_package_handle_standard_args(TIRPC DEFAULT_MSG
                                  TIRPC_INCLUDE_DIR TIRPC_LIBRARY)

mark_as_advanced(TIRPC_INCLUDE_DIR TIRPC_LIBRARY)

