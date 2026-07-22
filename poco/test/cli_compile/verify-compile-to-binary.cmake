foreach(required_variable POCO_EXECUTABLE POCO_SOURCE POCO_INVALID_SOURCE POCO_OUTPUT)
    if(NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
        message(FATAL_ERROR "${required_variable} must be set")
    endif()
endforeach()

set(SOURCE_MARKER_HEX
    "504841534531305f534f555243455f42595445535f4d5553545f4e4f545f415050454152")
string(TOLOWER "${SOURCE_MARKER_HEX}" SOURCE_MARKER_HEX)

get_filename_component(POCO_OUTPUT_DIRECTORY "${POCO_OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${POCO_OUTPUT_DIRECTORY}")
file(REMOVE "${POCO_OUTPUT}")
execute_process(
    COMMAND "${POCO_EXECUTABLE}" -c "${POCO_SOURCE}" -o "${POCO_OUTPUT}"
    RESULT_VARIABLE compile_result
    OUTPUT_VARIABLE compile_stdout
    ERROR_VARIABLE compile_stderr
)
if(NOT compile_result EQUAL 0)
    message(FATAL_ERROR
        "poco -c/-o failed (${compile_result})\nstdout:\n${compile_stdout}\nstderr:\n${compile_stderr}")
endif()
if(NOT EXISTS "${POCO_OUTPUT}")
    message(FATAL_ERROR "poco -c/-o did not create ${POCO_OUTPUT}")
endif()

file(READ "${POCO_OUTPUT}" compiled_hex HEX)
string(TOLOWER "${compiled_hex}" compiled_hex)
string(LENGTH "${compiled_hex}" compiled_hex_length)
if(compiled_hex_length LESS 8)
    message(FATAL_ERROR "compiled output is shorter than the four-byte magic")
endif()
string(SUBSTRING "${compiled_hex}" 0 8 compiled_magic)
if(NOT compiled_magic STREQUAL "504f434f")
    message(FATAL_ERROR "compiled output has '${compiled_magic}' instead of POCO magic")
endif()
string(FIND "${compiled_hex}" "${SOURCE_MARKER_HEX}" source_marker_offset)
if(NOT source_marker_offset EQUAL -1)
    message(FATAL_ERROR "compiled output retained source comment bytes")
endif()

# The script returns 23 when run.  A zero CLI result here proves that -o by
# itself implies compile-only while still producing the binary.
file(REMOVE "${POCO_OUTPUT}")
execute_process(
    COMMAND "${POCO_EXECUTABLE}" -o "${POCO_OUTPUT}" "${POCO_SOURCE}"
    RESULT_VARIABLE output_only_result
    OUTPUT_VARIABLE output_only_stdout
    ERROR_VARIABLE output_only_stderr
)
if(NOT output_only_result EQUAL 0 OR NOT EXISTS "${POCO_OUTPUT}")
    message(FATAL_ERROR
        "poco -o did not compile without running (${output_only_result})\n"
        "stdout:\n${output_only_stdout}\nstderr:\n${output_only_stderr}")
endif()

# -c without -o is only a compile/syntax check and must not choose an output
# path on the caller's behalf.
file(REMOVE "${POCO_OUTPUT}")
execute_process(
    COMMAND "${POCO_EXECUTABLE}" -c "${POCO_SOURCE}"
    RESULT_VARIABLE check_result
    OUTPUT_VARIABLE check_stdout
    ERROR_VARIABLE check_stderr
)
if(NOT check_result EQUAL 0)
    message(FATAL_ERROR
        "poco -c syntax check failed (${check_result})\nstdout:\n${check_stdout}\n"
        "stderr:\n${check_stderr}")
endif()
if(EXISTS "${POCO_OUTPUT}")
    message(FATAL_ERROR "poco -c unexpectedly created ${POCO_OUTPUT}")
endif()

execute_process(
    COMMAND "${POCO_EXECUTABLE}" -c "${POCO_INVALID_SOURCE}"
    RESULT_VARIABLE invalid_result
    OUTPUT_VARIABLE invalid_stdout
    ERROR_VARIABLE invalid_stderr
)
if(invalid_result EQUAL 0)
    message(FATAL_ERROR
        "poco -c reported success for invalid source\nstdout:\n${invalid_stdout}\n"
        "stderr:\n${invalid_stderr}")
endif()
if(EXISTS "${POCO_OUTPUT}")
    message(FATAL_ERROR "failed poco -c unexpectedly created ${POCO_OUTPUT}")
endif()
