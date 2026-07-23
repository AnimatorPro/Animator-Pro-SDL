if(NOT DEFINED POCO_EXECUTABLE OR NOT DEFINED POCO_MAIN OR
   NOT DEFINED POCO_LIBRARY OR NOT DEFINED POCO_HEADER OR
   NOT DEFINED POCO_COMMANDS OR NOT DEFINED POCO_FIXTURE_DIR)
    message(FATAL_ERROR "multi-file debugger test is missing required paths")
endif()

file(MAKE_DIRECTORY "${POCO_FIXTURE_DIR}")
set(main_copy "${POCO_FIXTURE_DIR}/debug_main.poc")
set(library_copy "${POCO_FIXTURE_DIR}/debug_library.poc")
set(header_copy "${POCO_FIXTURE_DIR}/debug_library.h")
set(binary "${POCO_FIXTURE_DIR}/debug-app.pex")
file(COPY_FILE "${POCO_MAIN}" "${main_copy}" ONLY_IF_DIFFERENT)
file(COPY_FILE "${POCO_LIBRARY}" "${library_copy}" ONLY_IF_DIFFERENT)
file(COPY_FILE "${POCO_HEADER}" "${header_copy}" ONLY_IF_DIFFERENT)

execute_process(
    COMMAND "${POCO_EXECUTABLE}" -g -c "${main_copy}" "${library_copy}"
        -o "${binary}"
    RESULT_VARIABLE compile_result
    OUTPUT_VARIABLE compile_output
    ERROR_VARIABLE compile_error
)
if(NOT compile_result EQUAL 0)
    message(FATAL_ERROR
        "multi-file debug binary compile failed (${compile_result})\n"
        "stdout:\n${compile_output}\nstderr:\n${compile_error}")
endif()

execute_process(
    COMMAND "${POCO_EXECUTABLE}" --debug "${binary}"
    INPUT_FILE "${POCO_COMMANDS}"
    RESULT_VARIABLE debug_result
    OUTPUT_VARIABLE debug_output
    ERROR_VARIABLE debug_error
)
if(NOT debug_result EQUAL 0)
    message(FATAL_ERROR
        "multi-file debugger failed (${debug_result})\n"
        "stdout:\n${debug_output}\nstderr:\n${debug_error}")
endif()
set(debug_combined "${debug_output}\n${debug_error}")
foreach(expected IN ITEMS
        "Stopped at ${main_copy}:"
        "Stopped at ${library_copy}:"
        "Return value: 42")
    string(FIND "${debug_combined}" "${expected}" expected_found)
    if(expected_found EQUAL -1)
        message(FATAL_ERROR
            "multi-file debugger output omitted '${expected}'\n"
            "stdout:\n${debug_output}\nstderr:\n${debug_error}")
    endif()
endforeach()

# The image retains a distinct hash for each source.  Changing only the second
# file must reject the image before execution instead of validating against the
# primary source's hash.
file(APPEND "${library_copy}" "\n/* deliberate second-file hash mismatch */\n")
execute_process(
    COMMAND "${POCO_EXECUTABLE}" --debug "${binary}"
    RESULT_VARIABLE mismatch_result
    OUTPUT_VARIABLE mismatch_output
    ERROR_VARIABLE mismatch_error
)
if(mismatch_result EQUAL 0)
    message(FATAL_ERROR "modified second source was accepted by the debugger")
endif()
set(mismatch_combined "${mismatch_output}\n${mismatch_error}")
string(FIND "${mismatch_combined}" "source hash mismatch" mismatch_found)
string(FIND "${mismatch_combined}" "${library_copy}" library_found)
if(mismatch_found EQUAL -1 OR library_found EQUAL -1)
    message(FATAL_ERROR
        "second-file hash mismatch diagnostic was incomplete\n"
        "stdout:\n${mismatch_output}\nstderr:\n${mismatch_error}")
endif()
