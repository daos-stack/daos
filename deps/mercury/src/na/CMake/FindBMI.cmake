# - Try to find BMI
# Once done this will define
#  BMI_FOUND - System has BMI
#  BMI_INCLUDE_DIRS - The BMI include directories
#  BMI_LIBRARIES - The libraries needed to use BMI

find_path(BMI_INCLUDE_DIR bmi.h
  HINTS /usr/local/include /usr/include)

find_library(BMI_LIBRARY NAMES bmi
  PATHS /usr/local/lib /usr/lib)

set(BMI_INCLUDE_DIRS ${BMI_INCLUDE_DIR})
set(BMI_LIBRARIES ${BMI_LIBRARY})

include(FindPackageHandleStandardArgs)
# handle the QUIETLY and REQUIRED arguments and set BMI_FOUND to TRUE
# if all listed variables are TRUE
find_package_handle_standard_args(BMI DEFAULT_MSG
                                  BMI_INCLUDE_DIR BMI_LIBRARY)

mark_as_advanced(BMI_INCLUDE_DIR BMI_LIBRARY)
