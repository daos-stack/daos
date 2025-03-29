# Common Dashboard Script
#
# This script contains basic dashboard driver code common to all
# clients and projects. It is a combination of the universal.cmake script in
# the Kitware DashboardScriptsNG repo and cmake_common.cmake used by CMake
# dashboards.
#
# Create a file next to this script, say 'my_dashboard.cmake', with code of the
# following form:
#
#   # Client maintainer: me@mydomain.net
#   set(CTEST_SITE "machine.site")
#   set(CTEST_BUILD_NAME "Platform-Compiler")
#   set(CTEST_BUILD_CONFIGURATION Debug)
#   set(CTEST_CMAKE_GENERATOR "Unix Makefiles")
#   include(${CTEST_SCRIPT_DIRECTORY}/mercury_common.cmake)
#
# Then run a scheduled task (cron job) with a command line such as
#
#   ctest -S my_dashboard.cmake -V
#
# By default the source and build trees will be placed in the path
# "../My Tests/" relative to your script location.
#
# The following variables may be set before including this script
# to configure it:
#
#   dashboard_model          = Nightly | Experimental | Continuous
#   dashboard_root_name      = Change name of "My Tests" directory
#   dashboard_source_name    = Name of source directory (Mercury)
#   dashboard_binary_name    = Name of binary directory (Mercury-build)
#   dashboard_cache          = Initial CMakeCache.txt file content
#   dashboard_track          = The name of the CDash "Track" to submit to
#
#   dashboard_do_checkout    = True to enable source checkout via git
#   dashboard_do_update      = True to enable the Update step
#   dashboard_do_configure   = True to enable the Configure step
#   dashboard_do_build       = True to enable the Build step
#   dashboard_do_test        = True to enable the Test step
#   dashboard_do_done        = True to enable the Done step
#   dashboard_do_coverage    = True to enable coverage (ex: gcov)
#   dashboard_do_memcheck    = True to enable memcheck (ex: valgrind)
#   dashboard_do_submit      = Submit each step (ON)
#   dashboard_do_submit_only = Only submit step results, do nto run them (OFF)
#   dashboard_allow_errors   = Do not return errors when tests fail (OFF)
#
#   CTEST_GIT_COMMAND        = path to git command-line client
#   CTEST_BUILD_FLAGS        = build tool arguments (ex: -j2)
#   CTEST_DASHBOARD_ROOT     = Where to put source and build trees
#   CTEST_TEST_CTEST         = Whether to run long CTestTest* tests
#   CTEST_TEST_TIMEOUT       = Per-test timeout length
#   CTEST_TEST_ARGS          = ctest_test args (ex: PARALLEL_LEVEL 4)
#   CMAKE_MAKE_PROGRAM       = Path to "make" tool to use
#
# Options to configure Git:
#   dashboard_git_url        = Custom git clone url
#   dashboard_git_branch     = Custom remote branch to track
#   dashboard_git_crlf       = Value of core.autocrlf for repository
#
# For Makefile generators the script may be executed from an
# environment already configured to use the desired compilers.
# Alternatively the environment may be set at the top of the script:
#
#   set(ENV{CC}  /path/to/cc)   # C compiler
#   set(ENV{CXX} /path/to/cxx)  # C++ compiler
#   set(ENV{FC}  /path/to/fc)   # Fortran compiler (optional)
#   set(ENV{LD_LIBRARY_PATH} /path/to/vendor/lib) # (if necessary)

cmake_minimum_required(VERSION 2.8.12.2...3.15 FATAL_ERROR)

if(NOT DEFINED dashboard_full)
  set(dashboard_full TRUE)
endif()

# Initialize all build steps to "ON"
if(NOT DEFINED dashboard_do_update)
  set(dashboard_do_update ${dashboard_full})
endif()

if(NOT DEFINED dashboard_do_checkout)
  set(dashboard_do_checkout ${dashboard_full})
endif()

if(NOT DEFINED dashboard_do_configure)
  set(dashboard_do_configure ${dashboard_full})
endif()

if(NOT DEFINED dashboard_do_build)
  set(dashboard_do_build ${dashboard_full})
endif()

if(NOT DEFINED dashboard_do_test)
  set(dashboard_do_test ${dashboard_full})
endif()

if(NOT DEFINED dashboard_do_done)
  set(dashboard_do_done ${dashboard_full})
endif()

# Default code coverage and memtesting to off
if(NOT DEFINED dashboard_do_coverage)
  set(dashboard_do_coverage FALSE)
endif()

if(NOT DEFINED dashboard_do_memcheck)
  set(dashboard_do_memcheck FALSE)
endif()

if(NOT DEFINED dashboard_fresh)
  if(dashboard_full OR dashboard_do_update)
    set(dashboard_fresh TRUE)
  else()
    set(dashboard_fresh FALSE)
  endif()
endif()

if(NOT DEFINED dashboard_do_submit_only)
  set(dashboard_do_submit_only FALSE)
endif()

if(NOT DEFINED dashboard_allow_errors)
  set(dashboard_allow_errors FALSE)
endif()

if(NOT DEFINED dashboard_do_submit)
  set(dashboard_do_submit TRUE)
endif()

if(NOT DEFINED CTEST_PROJECT_NAME)
  message(FATAL_ERROR "project-specific script including '***_common.cmake' should set CTEST_PROJECT_NAME")
endif()

# Select the top dashboard directory.
if(NOT DEFINED dashboard_root_name)
  set(dashboard_root_name "My Tests")
endif()
if(NOT DEFINED CTEST_DASHBOARD_ROOT)
  get_filename_component(CTEST_DASHBOARD_ROOT "${CTEST_SCRIPT_DIRECTORY}/../${dashboard_root_name}" ABSOLUTE)
endif()

# Select the model (Nightly, Experimental, Continuous).
if(NOT DEFINED dashboard_model)
  set(dashboard_model Nightly)
endif()
if(NOT "${dashboard_model}" MATCHES "^(Nightly|Experimental|Continuous)$")
  message(FATAL_ERROR "dashboard_model must be Nightly, Experimental, or Continuous")
endif()

# Default to a Debug build.
if(NOT DEFINED CTEST_BUILD_CONFIGURATION)
  set(CTEST_BUILD_CONFIGURATION Debug)
endif()

if(NOT DEFINED CTEST_CONFIGURATION_TYPE)
  set(CTEST_CONFIGURATION_TYPE ${CTEST_BUILD_CONFIGURATION})
endif()

# Choose CTest reporting mode.
if(NOT "${CTEST_CMAKE_GENERATOR}" MATCHES "Make|Ninja")
  # Launchers work only with Makefile generators.
  set(CTEST_USE_LAUNCHERS 0)
elseif(NOT DEFINED CTEST_USE_LAUNCHERS)
  # The setting is ignored by CTest < 2.8 so we need no version test.
  set(CTEST_USE_LAUNCHERS 1)
endif()

# Configure testing.
if(NOT DEFINED CTEST_TEST_CTEST)
  set(CTEST_TEST_CTEST 1)
endif()
if(NOT CTEST_TEST_TIMEOUT)
  set(CTEST_TEST_TIMEOUT 1500)
endif()

# Select Git source to use.
if(NOT DEFINED dashboard_git_url)
  message(FATAL_ERROR "project-specific script including '***_common.cmake' should set dashboard_git_url")
endif()
if(NOT DEFINED dashboard_git_branch)
  set(dashboard_git_branch master)
endif()
if(NOT DEFINED dashboard_git_crlf)
  if(UNIX)
    set(dashboard_git_crlf false)
  else()
    set(dashboard_git_crlf true)
  endif()
endif()

# Look for a GIT command-line client.
if(NOT DEFINED CTEST_GIT_COMMAND)
  set(git_names git git.cmd)
  # First search the PATH.
  find_program(CTEST_GIT_COMMAND NAMES ${git_names})
  if(CMAKE_HOST_WIN32)
    # Now look for installations in Git/ directories under typical installation
    # prefixes on Windows.  Exclude PATH from this search because VS 2017's
    # command prompt happens to have a PATH entry with a Git/ subdirectory
    # containing a minimal git not meant for general use.
    find_program(CTEST_GIT_COMMAND
      NAMES ${git_names}
      PATH_SUFFIXES Git/cmd Git/bin
      NO_SYSTEM_ENVIRONMENT_PATH
    )
  endif()
endif()
if(NOT CTEST_GIT_COMMAND)
  message(FATAL_ERROR "CTEST_GIT_COMMAND not available!")
endif()

# Select a source directory name.
if(NOT DEFINED CTEST_SOURCE_DIRECTORY)
  if(DEFINED dashboard_source_name)
    set(CTEST_SOURCE_DIRECTORY ${CTEST_DASHBOARD_ROOT}/${dashboard_source_name})
  else()
    set(CTEST_SOURCE_DIRECTORY ${CTEST_DASHBOARD_ROOT}/${CTEST_PROJECT_NAME})
  endif()
endif()

# Select a build directory name.
if(NOT DEFINED CTEST_BINARY_DIRECTORY)
  if(DEFINED dashboard_binary_name)
    set(CTEST_BINARY_DIRECTORY ${CTEST_DASHBOARD_ROOT}/${dashboard_binary_name})
  else()
    set(CTEST_BINARY_DIRECTORY ${CTEST_SOURCE_DIRECTORY}-build)
  endif()
endif()

macro(dashboard_git)
  execute_process(
    COMMAND ${CTEST_GIT_COMMAND} ${ARGN}
    WORKING_DIRECTORY "${CTEST_SOURCE_DIRECTORY}"
    OUTPUT_VARIABLE dashboard_git_output
    ERROR_VARIABLE dashboard_git_output
    RESULT_VARIABLE dashboard_git_failed
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE
    )
endmacro()

if(dashboard_do_checkout)
  # Delete source tree if it is incompatible with current VCS.
  if(EXISTS ${CTEST_SOURCE_DIRECTORY})
    if(NOT EXISTS "${CTEST_SOURCE_DIRECTORY}/.git")
      set(vcs_refresh "because it is not managed by git.")
    else()
      execute_process(
        COMMAND ${CTEST_GIT_COMMAND} reset --hard
        WORKING_DIRECTORY "${CTEST_SOURCE_DIRECTORY}"
        OUTPUT_VARIABLE output
        ERROR_VARIABLE output
        RESULT_VARIABLE failed
        )
      if(failed)
        set(vcs_refresh "because its .git may be corrupted.")
      endif()
    endif()
    if(vcs_refresh AND "${CTEST_SOURCE_DIRECTORY}" MATCHES "/CMake[^/]*")
      message("Deleting source tree\n")
      message("  ${CTEST_SOURCE_DIRECTORY}\n${vcs_refresh}")
      file(REMOVE_RECURSE "${CTEST_SOURCE_DIRECTORY}")
    endif()
  endif()

  # Support initial checkout if necessary.
  if(NOT EXISTS "${CTEST_SOURCE_DIRECTORY}"
      AND NOT DEFINED CTEST_CHECKOUT_COMMAND)
  # Generate an initial checkout script.
  get_filename_component(_name "${CTEST_SOURCE_DIRECTORY}" NAME)
  set(ctest_checkout_script ${CTEST_DASHBOARD_ROOT}/${_name}-init.cmake)
  file(WRITE ${ctest_checkout_script} "# git repo init script for ${_name}
execute_process(
  COMMAND \"${CTEST_GIT_COMMAND}\" clone -n -- \"${dashboard_git_url}\"
          \"${CTEST_SOURCE_DIRECTORY}\"
  )
if(EXISTS \"${CTEST_SOURCE_DIRECTORY}/.git\")
  execute_process(
    COMMAND \"${CTEST_GIT_COMMAND}\" config core.autocrlf ${dashboard_git_crlf}
    WORKING_DIRECTORY \"${CTEST_SOURCE_DIRECTORY}\"
    )
  execute_process(
    COMMAND \"${CTEST_GIT_COMMAND}\" fetch
    WORKING_DIRECTORY \"${CTEST_SOURCE_DIRECTORY}\"
    )
  execute_process(
    COMMAND \"${CTEST_GIT_COMMAND}\" checkout ${dashboard_git_branch}
    WORKING_DIRECTORY \"${CTEST_SOURCE_DIRECTORY}\"
    )
  execute_process(
    COMMAND \"${CTEST_GIT_COMMAND}\" submodule update --init
    WORKING_DIRECTORY \"${CTEST_SOURCE_DIRECTORY}\"
    )
endif()"
  )
  set(CTEST_CHECKOUT_COMMAND "\"${CMAKE_COMMAND}\" -P \"${ctest_checkout_script}\"")
  elseif(EXISTS "${CTEST_SOURCE_DIRECTORY}/.git")
    # Upstream URL.
    dashboard_git(config --get remote.origin.url)
    if(NOT dashboard_git_output STREQUAL "${dashboard_git_url}")
      dashboard_git(config remote.origin.url "${dashboard_git_url}")
    endif()

    # Local checkout.
    dashboard_git(symbolic-ref HEAD)
    if(NOT dashboard_git_output STREQUAL "${dashboard_git_branch}")
      dashboard_git(checkout --recurse-submodules ${dashboard_git_branch})
      if(dashboard_git_failed)
        message(FATAL_ERROR "Failed to checkout branch ${dashboard_git_branch}:\n${dashboard_git_output}")
      endif()
    endif()
  endif()
endif()

#-----------------------------------------------------------------------------

# Check for required variables.
foreach(req
    CTEST_CMAKE_GENERATOR
    CTEST_SITE
    CTEST_BUILD_NAME
    )
  if(NOT DEFINED ${req})
    message(FATAL_ERROR "The containing script must set ${req}")
  endif()
endforeach(req)

# Print summary information.
set(vars "")
foreach(v
    CTEST_SITE
    CTEST_BUILD_NAME
    CTEST_SOURCE_DIRECTORY
    CTEST_BINARY_DIRECTORY
    CTEST_CMAKE_GENERATOR
    CTEST_BUILD_CONFIGURATION
    CTEST_GIT_COMMAND
    CTEST_CHECKOUT_COMMAND
    CTEST_CONFIGURE_COMMAND
    CTEST_SCRIPT_DIRECTORY
    CTEST_USE_LAUNCHERS
    )
  set(vars "${vars}  ${v}=[${${v}}]\n")
endforeach(v)
message("Dashboard script configuration:\n${vars}\n")

# Avoid non-ascii characters in tool output.
set(ENV{LC_ALL} C)

# Helper macro to write the initial cache.
macro(write_cache)
  set(cache_build_type "")
  set(cache_make_program "")
  if(CTEST_CMAKE_GENERATOR MATCHES "Make|Ninja")
    set(cache_build_type CMAKE_BUILD_TYPE:STRING=${CTEST_BUILD_CONFIGURATION})
    if(CMAKE_MAKE_PROGRAM)
      set(cache_make_program CMAKE_MAKE_PROGRAM:FILEPATH=${CMAKE_MAKE_PROGRAM})
    endif()
  endif()
  file(WRITE ${CTEST_BINARY_DIRECTORY}/CMakeCache.txt "
SITE:STRING=${CTEST_SITE}
BUILDNAME:STRING=${CTEST_BUILD_NAME}
CTEST_TEST_CTEST:BOOL=${CTEST_TEST_CTEST}
CTEST_USE_LAUNCHERS:BOOL=${CTEST_USE_LAUNCHERS}
DART_TESTING_TIMEOUT:STRING=${CTEST_TEST_TIMEOUT}
GIT_EXECUTABLE:FILEPATH=${CTEST_GIT_COMMAND}
${cache_build_type}
${cache_make_program}
${dashboard_cache}
")
endmacro(write_cache)

if(COMMAND dashboard_hook_init)
  dashboard_hook_init()
endif()

if(dashboard_fresh)
  # Start with a fresh build tree.
  if(EXISTS "${CTEST_BINARY_DIRECTORY}" AND
     NOT "${CTEST_SOURCE_DIRECTORY}" STREQUAL "${CTEST_BINARY_DIRECTORY}")
     message("Clearing build tree...")
     ctest_empty_binary_directory(${CTEST_BINARY_DIRECTORY})
  endif()
  file(MAKE_DIRECTORY "${CTEST_BINARY_DIRECTORY}")
  message("Starting fresh build...")
  write_cache()
endif()

# Start a new submission.
if(dashboard_track)
  set(dashboard_track_arg TRACK "${dashboard_track}")
endif()
if(dashboard_fresh)
  message("Calling ctest_start (fresh)")
  if(COMMAND dashboard_hook_start)
    dashboard_hook_start()
  endif()
  ctest_start(${dashboard_model} ${dashboard_track_arg})
  if(dashboard_do_submit)
    ctest_submit(PARTS Start)
  endif()
  if(COMMAND dashboard_hook_started)
    dashboard_hook_started()
  endif()
else()
  message("Calling ctest_start (append)")
  ctest_start(${dashboard_model} ${dashboard_track_arg} APPEND)
endif()

# Look for updates.
if(dashboard_do_update)
  if(NOT dashboard_do_submit_only)
    if(COMMAND dashboard_hook_update)
      dashboard_hook_update()
    endif()
    message("Calling ctest_update")
    ctest_update(RETURN_VALUE count)
    set(CTEST_CHECKOUT_COMMAND) # checkout on first iteration only
    message("Found ${count} changed files")
  endif()

  if(dashboard_do_submit)
    if(CTEST_SUBMIT_NOTES)
      message("Submitting dashboard scripts as Notes")
      # Send the main script as a note while submitting the Update part
      set(CTEST_NOTES_FILES
        ${CTEST_UPDATE_NOTES_FILES}
        "${CMAKE_CURRENT_LIST_FILE}")
      ctest_submit(PARTS Update Notes)
      unset(CTEST_NOTES_FILES)
    else()
      message("Skipping notes submission for Update step")
      ctest_submit(PARTS Update)
    endif()
  endif()
endif()

if(dashboard_do_configure)
  if(NOT dashboard_do_submit_only)
    if(COMMAND dashboard_hook_configure)
      dashboard_hook_configure()
    endif()
    message("Calling ctest_configure")
    ctest_configure(${dashboard_configure_args})
  endif()
  if(dashboard_do_submit)
    if(CTEST_SUBMIT_NOTES)
      message("Submitting CMakeCache.txt as Notes")
      set(CTEST_NOTES_FILES "${CTEST_BINARY_DIRECTORY}/CMakeCache.txt")
      ctest_submit(PARTS Configure Notes)
      unset(CTEST_NOTES_FILES)
    else()
      message("Skipping notes submission for Configure step")
      ctest_submit(PARTS Configure)
    endif()
  endif()
endif()

ctest_read_custom_files(${CTEST_BINARY_DIRECTORY})

if(dashboard_do_build)
  if(NOT dashboard_do_submit_only)
    if(COMMAND dashboard_hook_build)
      dashboard_hook_build()
    endif()
    message("Calling ctest_build")
    ctest_build(
      NUMBER_WARNINGS ctest_build_num_warnings
      )
  endif()
  if(dashboard_do_submit)
    ctest_submit(PARTS Build)
  endif()
endif()

if(dashboard_do_test)
  if(NOT dashboard_do_submit_only)
    if(COMMAND dashboard_hook_test)
      dashboard_hook_test()
    endif()
    message("Calling ctest_test")
    if(NOT dashboard_allow_errors)
      ctest_test(${CTEST_TEST_ARGS} RETURN_VALUE TEST_RESULTS)
      if(${TEST_RESULTS} EQUAL 0)
        message("ctest test results return value: ${TEST_RESULTS}")
      else()
        message(SEND_ERROR "Some tests failed")
      endif()
    else()
      ctest_test(${CTEST_TEST_ARGS})
    endif()
  endif()
  if(dashboard_do_submit)
    ctest_submit(PARTS Test)
  endif()
endif()

if(dashboard_do_coverage)
  if(NOT dashboard_do_submit_only)
    if(COMMAND dashboard_hook_coverage)
      dashboard_hook_coverage()
    endif()
    message("Calling ctest_coverage")
    ctest_coverage()
  endif()
  if(dashboard_do_submit)
    ctest_submit(PARTS Coverage)
  endif()
endif()

if(dashboard_do_memcheck)
  if(NOT dashboard_do_submit_only)
    if(COMMAND dashboard_hook_memcheck)
      dashboard_hook_memcheck()
    endif()
    message("Calling ctest_memcheck")
    if(NOT dashboard_allow_errors)
      ctest_memcheck(RETURN_VALUE MEMCHECK_RETURN DEFECT_COUNT MEMCHECK_DEFECTS)
      if(NOT (MEMCHECK_DEFECTS EQUAL 0))
        message(SEND_ERROR "ctest memcheck defects found: ${MEMCHECK_DEFECTS}")
      endif()
    else()
      ctest_memcheck()
    endif()
  endif()
  if(dashboard_do_submit)
    ctest_submit(PARTS MemCheck)
  endif()
endif()

if(dashboard_do_done)
  message("Calling ctest_done")
  if(dashboard_do_submit)
    ctest_submit(PARTS Done)
  endif()
endif()

if(COMMAND dashboard_hook_end)
  dashboard_hook_end()
endif()