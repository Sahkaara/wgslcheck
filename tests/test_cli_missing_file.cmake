set(missing_file "${CMAKE_CURRENT_BINARY_DIR}/wgslcheck-missing-input.wgsl")
if(EXISTS "${missing_file}")
    message(FATAL_ERROR "Test requires a nonexistent input: ${missing_file}")
endif()

foreach(command IN ITEMS check sim)
    execute_process(
        COMMAND "${EXECUTABLE}" "${command}" "${missing_file}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error)
    if(NOT "${result}" STREQUAL "1")
        message(FATAL_ERROR "${command}: expected exit 1, got ${result}: ${error}")
    endif()
    if(NOT error MATCHES "cannot open")
        message(FATAL_ERROR "${command}: missing file diagnostic absent: ${error}")
    endif()
endforeach()
