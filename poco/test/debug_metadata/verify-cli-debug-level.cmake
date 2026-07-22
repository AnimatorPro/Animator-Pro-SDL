file(MAKE_DIRECTORY "${POCO_FIXTURE_DIR}")
set(minimal "${POCO_FIXTURE_DIR}/minimal.pex")
set(extended "${POCO_FIXTURE_DIR}/extended.pex")

execute_process(
    COMMAND "${POCO_EXECUTABLE}" -c -o "${minimal}" "${POCO_SOURCE}"
    RESULT_VARIABLE minimal_status
    OUTPUT_VARIABLE minimal_stdout
    ERROR_VARIABLE minimal_stderr
)
if(NOT minimal_status EQUAL 0)
    message(FATAL_ERROR "minimal compile failed: ${minimal_stdout}${minimal_stderr}")
endif()

execute_process(
    COMMAND "${POCO_EXECUTABLE}" -g -c -o "${extended}" "${POCO_SOURCE}"
    RESULT_VARIABLE extended_status
    OUTPUT_VARIABLE extended_stdout
    ERROR_VARIABLE extended_stderr
)
if(NOT extended_status EQUAL 0)
    message(FATAL_ERROR "extended compile failed: ${extended_stdout}${extended_stderr}")
endif()

file(READ "${minimal}" minimal_header OFFSET 20 LIMIT 4 HEX)
file(READ "${extended}" extended_header OFFSET 20 LIMIT 4 HEX)
if(NOT minimal_header STREQUAL "00000000")
    message(FATAL_ERROR "minimal CLI binary debug header was ${minimal_header}")
endif()
if(NOT extended_header STREQUAL "01000000")
    message(FATAL_ERROR "extended CLI binary debug header was ${extended_header}")
endif()
