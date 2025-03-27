# - Try to find OFI
# Once done this will define
#  OFI_FOUND - System has OFI
#  OFI_INCLUDE_DIRS - The OFI include directories
#  OFI_LIBRARIES - The libraries needed to use OFI

find_package(PkgConfig)
pkg_check_modules(PC_OFI libfabric)

find_path(OFI_INCLUDE_DIR rdma/fabric.h
  HINTS ${PC_OFI_INCLUDEDIR} ${PC_OFI_INCLUDE_DIRS})

find_library(OFI_LIBRARY NAMES fabric
  HINTS ${PC_OFI_LIBDIR} ${PC_OFI_LIBRARY_DIRS})

if(OFI_INCLUDE_DIR AND EXISTS "${OFI_INCLUDE_DIR}/rdma/fabric.h")
    file(STRINGS "${OFI_INCLUDE_DIR}/rdma/fabric.h" FABRIC_H REGEX "^#define FI_MAJOR_VERSION [0-9]+$")
    string(REGEX MATCH "[0-9]+$" OFI_VERSION_MAJOR "${FABRIC_H}")

    file(STRINGS "${OFI_INCLUDE_DIR}/rdma/fabric.h" FABRIC_H REGEX "^#define FI_MINOR_VERSION [0-9]+$")
    string(REGEX MATCH "[0-9]+$" OFI_VERSION_MINOR "${FABRIC_H}")
    set(OFI_VERSION_STRING "${OFI_VERSION_MAJOR}.${OFI_VERSION_MINOR}")

    set(OFI_MAJOR_VERSION "${OFI_VERSION_MAJOR}")
    set(OFI_MINOR_VERSION "${OFI_VERSION_MINOR}")
endif()


set(OFI_INCLUDE_DIRS ${OFI_INCLUDE_DIR})
if(WIN32)
  set(OFI_INCLUDE_DIRS ${OFI_INCLUDE_DIR}/windows ${OFI_INCLUDE_DIRS})
endif()
set(OFI_LIBRARIES ${OFI_LIBRARY})

include(FindPackageHandleStandardArgs)
# handle the QUIETLY and REQUIRED arguments and set OFI_FOUND to TRUE
# if all listed variables are TRUE
find_package_handle_standard_args(OFI REQUIRED_VARS OFI_INCLUDE_DIR OFI_LIBRARY
                                      VERSION_VAR OFI_VERSION_STRING)

mark_as_advanced(OFI_INCLUDE_DIR OFI_LIBRARY)
