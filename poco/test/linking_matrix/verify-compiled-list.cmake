if(NOT DEFINED POCO_EXECUTABLE OR NOT DEFINED POCO_MAIN OR
   NOT DEFINED POCO_LIBRARY OR NOT DEFINED POCO_FIXTURE_DIR)
    message(FATAL_ERROR "compiled-list test is missing required paths")
endif()

file(MAKE_DIRECTORY "${POCO_FIXTURE_DIR}")
set(binary "${POCO_FIXTURE_DIR}/listed-app.pex")

execute_process(
    COMMAND "${POCO_EXECUTABLE}" -c "${POCO_MAIN}" "${POCO_LIBRARY}"
        -o "${binary}"
    RESULT_VARIABLE compile_result
    OUTPUT_VARIABLE compile_output
    ERROR_VARIABLE compile_error
)
if(NOT compile_result EQUAL 0 OR NOT EXISTS "${binary}")
    message(FATAL_ERROR
        "multi-file binary compile failed (${compile_result})\n"
        "stdout:\n${compile_output}\nstderr:\n${compile_error}")
endif()

execute_process(
    COMMAND "${POCO_EXECUTABLE}" "${POCO_MAIN}" "${POCO_LIBRARY}"
    RESULT_VARIABLE direct_result
    OUTPUT_VARIABLE direct_output
    ERROR_VARIABLE direct_error
)
if(NOT direct_result EQUAL 0)
    message(FATAL_ERROR
        "direct multi-file run failed (${direct_result})\n"
        "stdout:\n${direct_output}\nstderr:\n${direct_error}")
endif()

execute_process(
    COMMAND "${POCO_EXECUTABLE}" "${binary}"
    RESULT_VARIABLE run_result
    OUTPUT_VARIABLE run_output
    ERROR_VARIABLE run_error
)
if(NOT run_result EQUAL 0)
    message(FATAL_ERROR
        "compiled multi-file binary failed (${run_result})\n"
        "stdout:\n${run_output}\nstderr:\n${run_error}")
endif()
set(direct_combined "${direct_output}\n${direct_error}")
set(binary_combined "${run_output}\n${run_error}")
string(FIND "${direct_combined}" "Return value: 42" direct_result_found)
string(FIND "${binary_combined}" "Return value: 42" binary_result_found)
if(direct_result_found EQUAL -1 OR binary_result_found EQUAL -1)
    message(FATAL_ERROR
        "source or binary run returned unexpected output\n"
        "source stdout:\n${direct_output}\nsource stderr:\n${direct_error}\n"
        "binary stdout:\n${run_output}\nbinary stderr:\n${run_error}")
endif()
if(NOT direct_output STREQUAL run_output OR NOT direct_error STREQUAL run_error)
    message(FATAL_ERROR
        "direct and compiled multi-file output differed\n"
        "source stdout:\n${direct_output}\nsource stderr:\n${direct_error}\n"
        "binary stdout:\n${run_output}\nbinary stderr:\n${run_error}")
endif()
