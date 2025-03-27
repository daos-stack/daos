# - Try to find PSM2
# Once done this will define
#  PSM2_FOUND - System has PSM2
#  PSM2_INCLUDE_DIRS - The PSM2 include directories
#  PSM2_LIBRARIES - The libraries needed to use PSM2

find_path(PSM2_INCLUDE_DIR psm2.h
  HINTS /usr/local/include /usr/include)

find_library(PSM2_LIBRARY NAMES psm2
  PATHS /usr/local/lib /usr/lib)

set(PSM2_INCLUDE_DIRS ${PSM2_INCLUDE_DIR})
set(PSM2_LIBRARIES ${PSM2_LIBRARY})

include(FindPackageHandleStandardArgs)
# handle the QUIETLY and REQUIRED arguments and set PSM2_FOUND to TRUE
# if all listed variables are TRUE
find_package_handle_standard_args(PSM2 DEFAULT_MSG
                                  PSM2_INCLUDE_DIR PSM2_LIBRARY)

mark_as_advanced(PSM2_INCLUDE_DIR PSM2_LIBRARY)
