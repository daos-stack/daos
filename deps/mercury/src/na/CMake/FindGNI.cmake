# - Try to find GNI
# Once done this will define
#  GNI_FOUND - System has GNI
#  GNI_INCLUDE_DIRS - The GNI include directories
#  GNI_LIBRARIES - The libraries needed to use GNI

find_package(PkgConfig)
pkg_check_modules(PC_GNI QUIET cray-ugni)

find_path(GNI_INCLUDE_DIR gni_pub.h
  HINTS ${PC_GNI_INCLUDEDIR} ${PC_GNI_INCLUDE_DIRS})

find_library(GNI_LIBRARY NAMES ugni libugni
  HINTS ${PC_GNI_LIBDIR} ${PC_GNI_LIBRARY_DIRS})

set(GNI_LIBRARIES ${GNI_LIBRARY})
set(GNI_INCLUDE_DIRS ${GNI_INCLUDE_DIR})

include(FindPackageHandleStandardArgs)
# handle the QUIETLY and REQUIRED arguments and set GNI_FOUND to TRUE
# if all listed variables are TRUE
find_package_handle_standard_args(GNI DEFAULT_MSG
                                  GNI_LIBRARY GNI_INCLUDE_DIR)

mark_as_advanced(GNI_INCLUDE_DIR GNI_LIBRARY)
