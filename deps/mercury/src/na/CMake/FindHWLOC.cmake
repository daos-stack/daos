# - Try to find HWLOC
# Once done this will define
#  HWLOC_FOUND - System has HWLOC
#  HWLOC_INCLUDE_DIRS - The HWLOC include directories
#  HWLOC_LIBRARIES - The libraries needed to use HWLOC

find_package(PkgConfig)
pkg_check_modules(PC_HWLOC hwloc)

find_path(HWLOC_INCLUDE_DIR hwloc.h
  HINTS ${PC_HWLOC_INCLUDEDIR} ${PC_HWLOC_INCLUDE_DIRS})

find_library(HWLOC_LIBRARY NAMES hwloc
  HINTS ${PC_HWLOC_LIBDIR} ${PC_HWLOC_LIBRARY_DIRS})

set(HWLOC_INCLUDE_DIRS ${HWLOC_INCLUDE_DIR})
set(HWLOC_LIBRARIES ${HWLOC_LIBRARY})

include(FindPackageHandleStandardArgs)
# handle the QUIETLY and REQUIRED arguments and set HWLOC_FOUND to TRUE
# if all listed variables are TRUE
find_package_handle_standard_args(HWLOC DEFAULT_MSG
                                  HWLOC_INCLUDE_DIR HWLOC_LIBRARY)

mark_as_advanced(HWLOC_INCLUDE_DIR HWLOC_LIBRARY)
