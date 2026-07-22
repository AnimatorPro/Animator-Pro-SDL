foreach(required_variable POCO_EXECUTABLE POCO_SOURCE POCO_FIXTURE_DIR)
    if(NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
        message(FATAL_ERROR "${required_variable} must be set")
    endif()
endforeach()

file(MAKE_DIRECTORY "${POCO_FIXTURE_DIR}")
set(binary "${POCO_FIXTURE_DIR}/detect_and_run.pex")
set(renamed_binary "${POCO_FIXTURE_DIR}/detect_and_run.poc")
set(renamed_source "${POCO_FIXTURE_DIR}/detect_and_run_source.pex")
set(truncated "${POCO_FIXTURE_DIR}/truncated.pex")
set(corrupt "${POCO_FIXTURE_DIR}/corrupt.pex")
set(version_mismatch "${POCO_FIXTURE_DIR}/version_mismatch.pex")
file(REMOVE
    "${binary}"
    "${renamed_binary}"
    "${renamed_source}"
    "${truncated}"
    "${corrupt}"
    "${version_mismatch}"
)

execute_process(
    COMMAND "${POCO_EXECUTABLE}" "${POCO_SOURCE}"
    RESULT_VARIABLE source_result
    OUTPUT_VARIABLE source_stdout
    ERROR_VARIABLE source_stderr
)
if(NOT source_result EQUAL 0)
    message(FATAL_ERROR
        "source execution failed (${source_result})\nstdout:\n${source_stdout}\nstderr:\n${source_stderr}")
endif()

execute_process(
    COMMAND "${POCO_EXECUTABLE}" -c -o "${binary}" "${POCO_SOURCE}"
    RESULT_VARIABLE compile_result
    OUTPUT_VARIABLE compile_stdout
    ERROR_VARIABLE compile_stderr
)
if(NOT compile_result EQUAL 0 OR NOT EXISTS "${binary}")
    message(FATAL_ERROR
        "binary compilation failed (${compile_result})\n"
        "stdout:\n${compile_stdout}\nstderr:\n${compile_stderr}")
endif()

execute_process(
    COMMAND "${POCO_EXECUTABLE}" "${binary}"
    RESULT_VARIABLE binary_result
    OUTPUT_VARIABLE binary_stdout
    ERROR_VARIABLE binary_stderr
)
if(NOT binary_result EQUAL source_result OR
   NOT binary_stdout STREQUAL source_stdout OR
   NOT binary_stderr STREQUAL source_stderr)
    message(FATAL_ERROR
        "binary output differs from source\n"
        "source stdout:\n${source_stdout}\nbinary stdout:\n${binary_stdout}\n"
        "source stderr:\n${source_stderr}\nbinary stderr:\n${binary_stderr}")
endif()

# A binary renamed with a source extension remains a binary.
configure_file("${binary}" "${renamed_binary}" COPYONLY)
execute_process(
    COMMAND "${POCO_EXECUTABLE}" "${renamed_binary}"
    RESULT_VARIABLE renamed_binary_result
    OUTPUT_VARIABLE renamed_binary_stdout
    ERROR_VARIABLE renamed_binary_stderr
)
if(NOT renamed_binary_result EQUAL source_result OR
   NOT renamed_binary_stdout STREQUAL source_stdout OR
   NOT renamed_binary_stderr STREQUAL source_stderr)
    message(FATAL_ERROR "renamed binary did not preserve execution output")
endif()

# A source renamed with a binary extension remains source.
configure_file("${POCO_SOURCE}" "${renamed_source}" COPYONLY)
execute_process(
    COMMAND "${POCO_EXECUTABLE}" "${renamed_source}"
    RESULT_VARIABLE renamed_source_result
    OUTPUT_VARIABLE renamed_source_stdout
    ERROR_VARIABLE renamed_source_stderr
)
if(NOT renamed_source_result EQUAL source_result OR
   NOT renamed_source_stdout STREQUAL source_stdout OR
   NOT renamed_source_stderr STREQUAL source_stderr)
    message(FATAL_ERROR "renamed source did not preserve execution output")
endif()

# A complete magic match is authoritative even when the rest of the image is
# absent.  This must be diagnosed as a truncated binary, not compiled as text.
set(magic "POCOBC\r\n")
file(WRITE "${truncated}" "${magic}")
execute_process(
    COMMAND "${POCO_EXECUTABLE}" "${truncated}"
    RESULT_VARIABLE truncated_result
    OUTPUT_VARIABLE truncated_stdout
    ERROR_VARIABLE truncated_stderr
)
if(truncated_result EQUAL 0 OR
   NOT "${truncated_stdout}${truncated_stderr}" MATCHES "compiled binary is truncated")
    message(FATAL_ERROR
        "truncated binary was not distinctly rejected (${truncated_result})\n"
        "stdout:\n${truncated_stdout}\nstderr:\n${truncated_stderr}")
endif()

# Extra bytes violate canonical framing without disturbing the leading magic.
configure_file("${binary}" "${corrupt}" COPYONLY)
file(APPEND "${corrupt}" "X")
execute_process(
    COMMAND "${POCO_EXECUTABLE}" "${corrupt}"
    RESULT_VARIABLE corrupt_result
    OUTPUT_VARIABLE corrupt_stdout
    ERROR_VARIABLE corrupt_stderr
)
if(corrupt_result EQUAL 0 OR
   NOT "${corrupt_stdout}${corrupt_stderr}" MATCHES "compiled binary is corrupt")
    message(FATAL_ERROR
        "corrupt binary was not distinctly rejected (${corrupt_result})\n"
        "stdout:\n${corrupt_stdout}\nstderr:\n${corrupt_stderr}")
endif()

# Validation checks the strict format version immediately after the complete
# magic and fixed header extent, before interpreting the remaining layout.
string(REPEAT "X" 112 version_tail)
file(WRITE "${version_mismatch}" "${magic}${version_tail}")
execute_process(
    COMMAND "${POCO_EXECUTABLE}" "${version_mismatch}"
    RESULT_VARIABLE version_result
    OUTPUT_VARIABLE version_stdout
    ERROR_VARIABLE version_stderr
)
if(version_result EQUAL 0 OR
   NOT "${version_stdout}${version_stderr}" MATCHES "compiled binary version mismatch")
    message(FATAL_ERROR
        "version-mismatched binary was not distinctly rejected (${version_result})\n"
        "stdout:\n${version_stdout}\nstderr:\n${version_stderr}")
endif()
