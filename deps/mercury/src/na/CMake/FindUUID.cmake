# - Try to find UUID
# Once done this will define
#  UUID_FOUND - System has UUID
#  UUID_INCLUDE_DIRS - The UUID include directories
#  UUID_LIBRARIES - The libraries needed to use UUID

find_package(PkgConfig)
pkg_check_modules(PC_UUID uuid)

find_path(UUID_INCLUDE_DIR uuid/uuid.h
  HINTS ${PC_UUID_INCLUDEDIR} ${PC_UUID_INCLUDE_DIRS}
  PATHS /usr/local/include /usr/include)

find_library(UUID_LIBRARY NAMES uuid
  HINTS ${PC_DRC_LIBDIR} ${PC_DRC_LIBRARY_DIRS}
  PATHS /usr/local/lib64 /usr/local/lib /usr/lib64 /usr/lib)

set(UUID_INCLUDE_DIRS ${UUID_INCLUDE_DIR})
set(UUID_LIBRARIES ${UUID_LIBRARY})

include(FindPackageHandleStandardArgs)
# handle the QUIETLY and REQUIRED arguments and set UUID_FOUND to TRUE
# if all listed variables are TRUE
find_package_handle_standard_args(UUID DEFAULT_MSG
                                  UUID_INCLUDE_DIR UUID_LIBRARY)

mark_as_advanced(UUID_INCLUDE_DIR UUID_LIBRARY)

