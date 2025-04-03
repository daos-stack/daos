cmake_minimum_required(VERSION 2.8.12.2...3.15 FATAL_ERROR)

# Setup cmake policies.
foreach(p
    CMP0012
    CMP0013
    CMP0014
    CMP0022 # CMake 2.8.12
    CMP0025 # CMake 3.0
    CMP0053 # CMake 3.1
    CMP0054 # CMake 3.1
    CMP0074 # CMake 3.12
    CMP0075 # CMake 3.12
    CMP0083 # CMake 3.14
    CMP0093 # CMake 3.15
  )
  if(POLICY ${p})
    cmake_policy(SET ${p} NEW)
  endif()
endforeach()

# Set a consistent MACOSX_RPATH default across all CMake versions.
# When CMake 2.8.12 is required, change this default to 1.
# When CMake 3.0.0 is required, remove this block (see CMP0042).
if(NOT DEFINED CMAKE_MACOSX_RPATH)
  set(CMAKE_MACOSX_RPATH 0)
endif()

project(MCHECKSUM C)

#------------------------------------------------------------------------------
# Setup install and output Directories
#------------------------------------------------------------------------------
if(NOT MCHECKSUM_INSTALL_BIN_DIR)
  set(MCHECKSUM_INSTALL_BIN_DIR ${CMAKE_INSTALL_PREFIX}/bin)
endif()
if(NOT MCHECKSUM_INSTALL_LIB_DIR)
  set(MCHECKSUM_INSTALL_LIB_DIR ${CMAKE_INSTALL_PREFIX}/lib)
endif()
if(NOT MCHECKSUM_INSTALL_INCLUDE_DIR)
  # Interface include will default to prefix/include
  set(MCHECKSUM_INSTALL_INCLUDE_INTERFACE include)
  set(MCHECKSUM_INSTALL_INCLUDE_DIR ${CMAKE_INSTALL_PREFIX}/include)
else()
  set(MCHECKSUM_INSTALL_INCLUDE_INTERFACE ${MCHECKSUM_INSTALL_INCLUDE_DIR})
endif()
if(NOT MCHECKSUM_INSTALL_DATA_DIR)
  set(MCHECKSUM_INSTALL_DATA_DIR ${CMAKE_INSTALL_PREFIX}/share)
endif()

# Setting this ensures that "make install" will leave rpaths to external
# libraries intact on "make install". This ensures that one can install a
# version of mchecksum on the build machine without any issues. If this not
# desired, simply specify CMAKE_INSTALL_RPATH_USE_LINK_PATH when configuring
# mchecksum and "make install" will strip all rpaths, which is default behavior.
if(NOT CMAKE_INSTALL_RPATH_USE_LINK_PATH)
  set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
endif()

#------------------------------------------------------------------------------
# Set module path
#------------------------------------------------------------------------------
set(MCHECKSUM_CMAKE_MODULE_PATH "${MCHECKSUM_SOURCE_DIR}/CMake")
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${MCHECKSUM_CMAKE_MODULE_PATH})

#------------------------------------------------------------------------------
# Version information
#------------------------------------------------------------------------------
include(${MCHECKSUM_CMAKE_MODULE_PATH}/Git/Git.cmake)
include(MChecksumDetermineVersion)
# Hard-coded version variables are read-in from a separate file. This makes it
# easier to have a script to update version numbers automatically.
file(STRINGS version.txt version_txt)
extract_version_components("${version_txt}" "${PROJECT_NAME}")
determine_version(${MCHECKSUM_SOURCE_DIR} ${GIT_EXECUTABLE} "${PROJECT_NAME}")
set(MCHECKSUM_PACKAGE "mchecksum")
set(MCHECKSUM_PACKAGE_NAME "MChecksum")
set(MCHECKSUM_PACKAGE_DESCRIPTION "Mercury Checksum Library")
set(MCHECKSUM_PACKAGE_URL "https://github.com/mercury-hpc/mchecksum")
set(MCHECKSUM_PACKAGE_VENDOR "Argonne National Laboratory / The HDF Group")
message(STATUS "Configuring ${MCHECKSUM_PACKAGE} v${MCHECKSUM_VERSION_FULL}")

#------------------------------------------------------------------------------
# Setup CMake Environment
#------------------------------------------------------------------------------
if(APPLE AND NOT MCHECKSUM_EXTERNALLY_CONFIGURED)
  # We are doing a unix-style install i.e. everything will be installed in
  # CMAKE_INSTALL_PREFIX/bin and CMAKE_INSTALL_PREFIX/lib etc. as on other unix
  # platforms. We still need to setup CMAKE_INSTALL_NAME_DIR correctly so that
  # the binaries point to appropriate location for the libraries.

  # 1. Make CMAKE_INSTALL_PREFIX publicly accessible, if it was hidden in
  #    previous pass
  get_property(is_internal CACHE CMAKE_INSTALL_PREFIX PROPERTY TYPE)
  if(is_internal STREQUAL "INTERNAL")
    set(CMAKE_INSTALL_PREFIX ${CACHED_CMAKE_INSTALL_PREFIX} CACHE PATH "Install prefix" FORCE)
  else()
    set(CMAKE_INSTALL_PREFIX ${CACHED_CMAKE_INSTALL_PREFIX} CACHE PATH "Install prefix")
  endif()
  unset(MACOSX_APP_INSTALL_PREFIX CACHE)

  set(CMAKE_INSTALL_NAME_DIR "@rpath")
  mark_as_advanced(
    CMAKE_OSX_ARCHITECTURES
    CMAKE_OSX_DEPLOYMENT_TARGET
    CMAKE_OSX_SYSROOT
    )
endif()

#------------------------------------------------------------------------------
if(NOT MCHECKSUM_EXTERNALLY_CONFIGURED)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY
      ${PROJECT_BINARY_DIR}/bin CACHE PATH "Single Directory for all Executables."
  )
  set(EXECUTABLE_OUTPUT_PATH ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY
      ${PROJECT_BINARY_DIR}/bin CACHE PATH "Single Directory for all Libraries"
  )
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY
      ${PROJECT_BINARY_DIR}/bin CACHE PATH "Single Directory for all static libraries."
  )
endif()

#------------------------------------------------------------------------------
# Disallow in-source build
#------------------------------------------------------------------------------
if("${MCHECKSUM_SOURCE_DIR}" STREQUAL "${MCHECKSUM_BINARY_DIR}")
  message(FATAL_ERROR
    "MChecksum requires an out of source Build. "
    "Please create a separate binary directory and run CMake there.")
endif()

#------------------------------------------------------------------------------
# Set a default build type if none was specified
#------------------------------------------------------------------------------
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "Setting build type to 'RelWithDebInfo' as none was specified.")
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Choose the type of build." FORCE)
  # Set the possible values of build type for cmake-gui
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release"
    "MinSizeRel" "RelWithDebInfo" "Asan" "Ubsan")
endif()

if(NOT CMAKE_C_FLAGS AND CMAKE_COMPILER_IS_GNUCC)
  message(STATUS "GCC detected, setting additional flags")
  set(CMAKE_C_FLAGS "-Wall -Wextra -Winline -Wcast-qual -std=gnu99 -Wshadow" CACHE STRING "Flags used by the compiler during all build types." FORCE)
endif()

# Detect Asan/Ubsan compiler flags
if(NOT MCHECKSUM_EXTERNALLY_CONFIGURED)
  include(CheckAsan)
  include(CheckUbsan)
endif()

#-----------------------------------------------------------------------------
# Targets built within this project are exported at Install time for use
# by other projects.
#-----------------------------------------------------------------------------
if(NOT MCHECKSUM_EXPORTED_TARGETS)
  set(MCHECKSUM_EXPORTED_TARGETS "${MCHECKSUM_PACKAGE}-targets")
endif()

#------------------------------------------------------------------------------
# Choose static or shared libraries.
#------------------------------------------------------------------------------
option(BUILD_SHARED_LIBS "Build with shared libraries." OFF)
if(BUILD_SHARED_LIBS)
  set(MCHECKSUM_BUILD_SHARED_LIBS 1)
  set(MCHECKSUM_LIBTYPE SHARED)
else()
  set(MCHECKSUM_BUILD_SHARED_LIBS 0)
  set(MCHECKSUM_LIBTYPE STATIC)
endif()

#------------------------------------------------------------------------------
# Enable debug output.
#------------------------------------------------------------------------------
option(MCHECKSUM_ENABLE_DEBUG "Enable debug statements." OFF)
if(MCHECKSUM_ENABLE_DEBUG)
  set(MCHECKSUM_HAS_DEBUG 1)
else()
  set(MCHECKSUM_HAS_DEBUG 0)
endif()

#-------------------------------------------------------------------------------
function(mchecksum_set_lib_options libtarget libname libtype)
  if(${libtype} MATCHES "SHARED")
    if(WIN32 AND NOT MINGW)
      set(LIB_RELEASE_NAME "${libname}")
      set(LIB_DEBUG_NAME "${libname}_D")
    else()
      set(LIB_RELEASE_NAME "${libname}")
      set(LIB_DEBUG_NAME "${libname}_debug")
    endif()
    set(LIB_VERSION ${MCHECKSUM_VERSION}.${MCHECKSUM_VERSION_PATCH})
    set(API_VERSION ${MCHECKSUM_VERSION_MAJOR})
  else()
    if(WIN32 AND NOT MINGW)
      set(LIB_RELEASE_NAME "lib${libname}")
      set(LIB_DEBUG_NAME "lib${libname}_D")
    else()
      # if the generator supports configuration types or if the CMAKE_BUILD_TYPE has a value
      if(CMAKE_CONFIGURATION_TYPES OR CMAKE_BUILD_TYPE)
        set(LIB_RELEASE_NAME "${libname}")
        set(LIB_DEBUG_NAME "${libname}_debug")
      else()
        set(LIB_RELEASE_NAME "lib${libname}")
        set(LIB_DEBUG_NAME "lib${libname}_debug")
      endif()
    endif()
  endif()

  set_target_properties(${libtarget}
      PROPERTIES
      OUTPUT_NAME_DEBUG          ${LIB_DEBUG_NAME}
      OUTPUT_NAME_RELEASE        ${LIB_RELEASE_NAME}
      OUTPUT_NAME_MINSIZEREL     ${LIB_RELEASE_NAME}
      OUTPUT_NAME_RELWITHDEBINFO ${LIB_RELEASE_NAME}
      OUTPUT_NAME_ASAN           ${LIB_DEBUG_NAME}
      OUTPUT_NAME_TSAN           ${LIB_DEBUG_NAME}
      OUTPUT_NAME_UBSAN          ${LIB_DEBUG_NAME}
  )
  if(${libtype} MATCHES "SHARED")
    set_target_properties(${libtarget}
        PROPERTIES
        INSTALL_RPATH              ${MCHECKSUM_INSTALL_LIB_DIR}
        INSTALL_NAME_DIR           ${MCHECKSUM_INSTALL_LIB_DIR}
        VERSION                    ${MCHECKSUM_VERSION}.${MCHECKSUM_VERSION_PATCH}
        SOVERSION                  ${MCHECKSUM_VERSION_MAJOR}
    )
  endif()

  if(MSVC)
    target_compile_definitions(${libtarget} PRIVATE -D_CRT_SECURE_NO_WARNINGS)
  endif()

  #----- Use MSVC Naming conventions for Shared Libraries
  if(MINGW AND ${libtype} MATCHES "SHARED")
    set_target_properties(${libtarget}
        PROPERTIES
        IMPORT_SUFFIX ".lib"
        IMPORT_PREFIX ""
        PREFIX ""
    )
  endif()
endfunction()

#-----------------------------------------------------------------------------
function(mchecksum_get_pc_lib_name output_var lib_target)
  if(CMAKE_BUILD_TYPE)
    string(TOLOWER ${CMAKE_BUILD_TYPE} lower_cmake_build_type)
  endif()
  if(lower_cmake_build_type MATCHES "debug")
    get_target_property(${output_var} ${lib_target} OUTPUT_NAME_DEBUG)
  else()
    get_target_property(${output_var} ${lib_target} OUTPUT_NAME_RELEASE)
  endif()
  set(${output_var} "${${output_var}}" PARENT_SCOPE)
endfunction()

#-----------------------------------------------------------------------------
function(mchecksum_get_pc_inc_deps output_var inc_deps)
  foreach(inc_dep ${inc_deps})
    set(${output_var} "${${output_var}} -I${inc_dep}")
  endforeach()
  set(${output_var} "${${output_var}}" PARENT_SCOPE)
endfunction()

#-----------------------------------------------------------------------------
function(mchecksum_get_pc_lib_deps output_var lib_deps)
  # Need to generate -llib if not already passed
  foreach(lib_dep ${lib_deps})
    # get library name
    get_filename_component(lib_name ${lib_dep} NAME_WE)
    if(lib_name MATCHES "^-l" OR lib_name MATCHES "-pthread")
      # lib_name found is -lxxx
      set(deps_list ${deps_list} ${lib_name})
    else()
      # lib_name is /path/to/lib so get library path and name
      get_filename_component(lib_path ${lib_dep} PATH)
      if(lib_path)
        set(deps_list ${deps_list} -L${lib_path})
      endif()
      string(REGEX REPLACE "^lib" "" lib_name ${lib_name})
      set(deps_list ${deps_list} "-l${lib_name}")
    endif()
  endforeach()
  if(deps_list)
    list(REMOVE_DUPLICATES deps_list)
    string(REPLACE ";" " " ${output_var} "${deps_list}")
  endif()
  set(${output_var} "${${output_var}}" PARENT_SCOPE)
endfunction()

#-----------------------------------------------------------------------------
# Avoid explicitly including system include paths
set(MCHECKSUM_SYSTEM_INCLUDE_PATH ${CMAKE_SYSTEM_INCLUDE_PATH}
  ${CMAKE_C_IMPLICIT_INCLUDE_DIRECTORIES})
function(mchecksum_clean_include_path path_list)
  if(${path_list})
    foreach(item ${MCHECKSUM_SYSTEM_INCLUDE_PATH})
      list(REMOVE_ITEM ${path_list} ${item})
    endforeach()
  endif()
  set(${path_list} "${${path_list}}" PARENT_SCOPE)
endfunction()

set(MCHECKSUM_SYSTEM_LIBRARY_PATH ${CMAKE_SYSTEM_LIBRARY_PATH}
  ${CMAKE_C_IMPLICIT_LINK_DIRECTORIES})
function(mchecksum_clean_library_path path_list)
  if(${path_list})
    foreach(item ${MCHECKSUM_SYSTEM_LIBRARY_PATH})
      list(REMOVE_ITEM ${path_list} ${item})
    endforeach()
  endif()
  set(${path_list} "${${path_list}}" PARENT_SCOPE)
endfunction()

#-----------------------------------------------------------------------------
# Coverage
#-----------------------------------------------------------------------------
if(NOT MCHECKSUM_EXTERNALLY_CONFIGURED)
  option(MCHECKSUM_ENABLE_COVERAGE "Enable coverage." OFF)
  if(MCHECKSUM_ENABLE_COVERAGE)
    set(COVERAGE_FLAGS "-fprofile-arcs -ftest-coverage" CACHE STRING
      "Flags to the coverage program that CTest uses to perform coverage inspection"
    )
    mark_as_advanced(COVERAGE_FLAGS)
  endif()

  macro(set_coverage_flags target)
    set_target_properties(${target}
      PROPERTIES
        COMPILE_FLAGS ${COVERAGE_FLAGS}
        LINK_FLAGS ${COVERAGE_FLAGS}
    )
  endmacro()
endif()

#-----------------------------------------------------------------------------
# Source
#-----------------------------------------------------------------------------
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/src)

#-----------------------------------------------------------------------------
# Testing
#-----------------------------------------------------------------------------
option(BUILD_TESTING "Build testing." OFF)
if(NOT MCHECKSUM_EXTERNALLY_CONFIGURED AND BUILD_TESTING)
  enable_testing()
#  include(CTest)
  add_subdirectory(Testing)
endif()

#-----------------------------------------------------------------------------
# Configure the config.cmake file for the build directory
#-----------------------------------------------------------------------------
set(MCHECKSUM_CONFIG_INSTALLED FALSE)
configure_file(
  ${MCHECKSUM_SOURCE_DIR}/CMake/${MCHECKSUM_PACKAGE}-config.cmake.in
  ${MCHECKSUM_BINARY_DIR}/${MCHECKSUM_PACKAGE}-config.cmake @ONLY
)

#-----------------------------------------------------------------------------
# Configure the config.cmake file for the install directory
#-----------------------------------------------------------------------------
set(MCHECKSUM_CONFIG_INSTALLED TRUE)
configure_file(
  ${MCHECKSUM_SOURCE_DIR}/CMake/${MCHECKSUM_PACKAGE}-config.cmake.in
  ${MCHECKSUM_BINARY_DIR}/CMakeFiles/mchecksum-config.cmake @ONLY
)

install(
  FILES
    ${MCHECKSUM_BINARY_DIR}/CMakeFiles/${MCHECKSUM_PACKAGE}-config.cmake
  DESTINATION
    ${MCHECKSUM_INSTALL_DATA_DIR}/cmake/${MCHECKSUM_PACKAGE}
)

#-----------------------------------------------------------------------------
# Configure the mchecksum-config-version .cmake file for the install directory
#-----------------------------------------------------------------------------
configure_file(
  ${MCHECKSUM_SOURCE_DIR}/CMake/${MCHECKSUM_PACKAGE}-config-version.cmake.in
  ${MCHECKSUM_BINARY_DIR}/CMakeFiles/${MCHECKSUM_PACKAGE}-config-version.cmake @ONLY
)

install(
  FILES
    ${MCHECKSUM_BINARY_DIR}/CMakeFiles/${MCHECKSUM_PACKAGE}-config-version.cmake
  DESTINATION
    ${MCHECKSUM_INSTALL_DATA_DIR}/cmake/${MCHECKSUM_PACKAGE}
)

#-----------------------------------------------------------------------------
# CPack
#-----------------------------------------------------------------------------
if(NOT MCHECKSUM_EXTERNALLY_CONFIGURED)
  set(CPACK_GENERATOR "TBZ2")
  set(CPACK_PACKAGE_DESCRIPTION_FILE ${MCHECKSUM_SOURCE_DIR}/README.md)
  set(CPACK_RESOURCE_FILE_LICENSE ${MCHECKSUM_SOURCE_DIR}/LICENSE.txt)
  set(CPACK_PACKAGE_DESCRIPTION_SUMMARY ${MCHECKSUM_PACKAGE_DESCRIPTION})
  set(CPACK_PACKAGE_NAME ${MCHECKSUM_PACKAGE_NAME})
  set(CPACK_PACKAGE_VENDOR ${MCHECKSUM_PACKAGE_VENDOR})
  set(CPACK_PACKAGE_VERSION_MAJOR ${MCHECKSUM_VERSION_MAJOR})
  set(CPACK_PACKAGE_VERSION_MINOR ${MCHECKSUM_VERSION_MINOR})
  set(CPACK_PACKAGE_VERSION_PATCH ${MCHECKSUM_VERSION_PATCH})
  set(CPACK_SOURCE_GENERATOR "TBZ2")
  set(CPACK_SOURCE_PACKAGE_FILE_NAME ${MCHECKSUM_PACKAGE}-${MCHECKSUM_VERSION_FULL})
  set(CPACK_SOURCE_IGNORE_FILES
    # Files specific to version control
    "/\\\\.git/"
    "/\\\\.git$"
    "/\\\\.gitattributes$"
    "/\\\\.github/"
    "/\\\\.gitignore$"

    # IDE files
    "/\\\\.vscode/"

    # Build
    "/build/"

    # Temporary files
    "\\\\.swp$"
    "\\\\.#"
    "/#"
    "~$"
  )
  set(CPACK_SOURCE_STRIP_FILES "")
  include(CPack)
endif()
