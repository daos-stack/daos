# - Try to find ISA-L
# Once done this will define
#  ISAL_FOUND - System has ISA-L
#  ISAL_INCLUDE_DIRS - The ISA-L include directories
#  ISAL_LIBRARIES - The libraries needed to use ISA-L

find_package(PkgConfig)
pkg_check_modules(PC_ISAL libisal)

find_path(ISAL_INCLUDE_DIR isa-l.h
  HINTS ${PC_ISAL_INCLUDEDIR} ${PC_ISAL_INCLUDE_DIRS})

find_library(ISAL_LIBRARY NAMES isal
  HINTS ${PC_ISAL_LIBDIR} ${PC_ISAL_LIBRARY_DIRS})

set(ISAL_INCLUDE_DIRS ${ISAL_INCLUDE_DIR})
set(ISAL_LIBRARIES ${ISAL_LIBRARY})

include(FindPackageHandleStandardArgs)
# handle the QUIETLY and REQUIRED arguments and set ISAL_FOUND to TRUE
# if all listed variables are TRUE
find_package_handle_standard_args(ISAL DEFAULT_MSG
                                  ISAL_LIBRARY ISAL_INCLUDE_DIR)

mark_as_advanced(ISAL_INCLUDE_DIR ISAL_LIBRARY)
