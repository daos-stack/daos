# - Try to find CXI
# Once done this will define
#  CXI_FOUND - System has CXI
#  CXI_INCLUDE_DIRS - The CXI include directories
#  CXI_LIBRARIES - The libraries needed to use CXI

find_package(PkgConfig)
pkg_check_modules(PC_CXI libcxi)

find_path(CXI_INCLUDE_DIR libcxi/libcxi.h
  HINTS ${PC_CXI_INCLUDEDIR} ${PC_CXI_INCLUDE_DIRS})

find_library(CXI_LIBRARY NAMES cxi
  HINTS ${PC_CXI_LIBDIR} ${PC_CXI_LIBRARY_DIRS})

set(CXI_INCLUDE_DIRS ${CXI_INCLUDE_DIR})
set(CXI_LIBRARIES ${CXI_LIBRARY})

include(FindPackageHandleStandardArgs)
# handle the QUIETLY and REQUIRED arguments and set CXI_FOUND to TRUE
# if all listed variables are TRUE
find_package_handle_standard_args(CXI DEFAULT_MSG
                                  CXI_INCLUDE_DIR CXI_LIBRARY)

mark_as_advanced(CXI_INCLUDE_DIR CXI_LIBRARY)
