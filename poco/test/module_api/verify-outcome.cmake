if(NOT DEFINED POCO_EXECUTABLE OR NOT DEFINED POCO_SCRIPT OR NOT DEFINED EXPECTED_TEXT)
    message(FATAL_ERROR "POCO_EXECUTABLE, POCO_SCRIPT, and EXPECTED_TEXT are required")
endif()

execute_process(
    COMMAND "${POCO_EXECUTABLE}" "${POCO_SCRIPT}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)
set(combined_output "${output}${error}")

if(result EQUAL 0)
    message(FATAL_ERROR "Expected ${POCO_SCRIPT} to fail, but it succeeded:\n${combined_output}")
endif()

string(FIND "${combined_output}" "${EXPECTED_TEXT}" expected_text_offset)
if(expected_text_offset EQUAL -1)
    message(FATAL_ERROR
        "Expected loader outcome '${EXPECTED_TEXT}' was not reported for ${POCO_SCRIPT}:\n${combined_output}")
endif()
