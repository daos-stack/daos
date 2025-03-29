#------------------------------------------------------------------------------
# Set up test options
#------------------------------------------------------------------------------

# mchecksum_prefix is added to executable
function(add_mchecksum_test test_name)
  add_executable(mchecksum_test_${test_name} test_${test_name}.c)
  target_link_libraries(mchecksum_test_${test_name} mchecksum)
  if(MCHECKSUM_ENABLE_COVERAGE)
    set_coverage_flags(mchecksum_test_${test_name})
  endif()
  foreach(hash_type ${MCHECKSUM_HASH_TYPES})
    add_test(NAME mchecksum_test_${test_name}_${hash_type}
      COMMAND $<TARGET_FILE:mchecksum_test_${test_name}> "${hash_type}")
  endforeach()
endfunction()

#------------------------------------------------------------------------------
# Dependencies
#------------------------------------------------------------------------------
include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}
)

#------------------------------------------------------------------------------
# Set up tests
#------------------------------------------------------------------------------
add_mchecksum_test(simple)
add_mchecksum_test(perf)