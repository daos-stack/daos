#
# Build the documentation
#
find_package(Doxygen REQUIRED)
find_package(Perl REQUIRED)
find_package(HTMLHelp)

find_program(GUNZIPCOMMAND gunzip DOC "Path to gunzip executable")
mark_as_advanced(GUNZIPCOMMAND)

set(DOXYGEN_SHORT_NAMES NO)
#
# Configure the script and the doxyfile, then add target
#
set(HAVE_DOT_YESNO NO)
if(DOT)
  set(HAVE_DOT_YESNO YES)
  if(NOT DOT_PATH)
    get_filename_component(DOT_PATH ${DOT} PATH)
  endif()
endif()

# Build up a list of all module source directories. Note that this should be
# all source directories and so does not use the normal variables.
unset(HG_MODULE_DIRS_DOXYGEN)
unset(HG_MODULE_DIRS_DOXYGEN_ESCAPED)
set(HG_MODULE_DIRS_DOXYGEN
  "${HG_MODULE_DIRS_DOXYGEN}  \"${MERCURY_SOURCE_DIR}/src\"\n")
set(HG_MODULE_DIRS_DOXYGEN_ESCAPED
  "${HG_MODULE_DIRS_DOXYGEN_ESCAPED} \"${MERCURY_SOURCE_DIR}/src\"\\\n")

configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/doc_mercury_version.dox.in
  ${CMAKE_CURRENT_BINARY_DIR}/doc_mercury_version.dox)

configure_file(
  ${MERCURY_SOURCE_DIR}/Documentation/Doxygen/doxyfile.in
  ${MERCURY_BINARY_DIR}/Documentation/Doxygen/doxyfile)

configure_file(
  ${MERCURY_SOURCE_DIR}/Documentation/Doxygen/doc_makeall.cmake.in
  ${MERCURY_BINARY_DIR}/Documentation/Doxygen/doc_makeall.cmake
  @ONLY)

add_custom_target(doc
  ${CMAKE_COMMAND}
  -P ${MERCURY_BINARY_DIR}/Documentation/Doxygen/doc_makeall.cmake
  DEPENDS ${MERCURY_BINARY_DIR}/Documentation/Doxygen/doc_makeall.cmake)
