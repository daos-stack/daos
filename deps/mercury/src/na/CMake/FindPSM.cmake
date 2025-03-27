# - Try to find PSM
# Once done this will define
#  PSM_FOUND - System has PSM
#  PSM_INCLUDE_DIRS - The PSM include directories
#  PSM_LIBRARIES - The libraries needed to use PSM

find_path(PSM_INCLUDE_DIR psm.h
  HINTS /usr/local/include /usr/include)

find_library(PSM_LIBRARY NAMES psm_infinipath
  PATHS /usr/local/lib /usr/lib)

set(PSM_INCLUDE_DIRS ${PSM_INCLUDE_DIR})
set(PSM_LIBRARIES ${PSM_LIBRARY})

include(FindPackageHandleStandardArgs)
# handle the QUIETLY and REQUIRED arguments and set PSM_FOUND to TRUE
# if all listed variables are TRUE
find_package_handle_standard_args(PSM DEFAULT_MSG
                                  PSM_INCLUDE_DIR PSM_LIBRARY)

mark_as_advanced(PSM_INCLUDE_DIR PSM_LIBRARY)
