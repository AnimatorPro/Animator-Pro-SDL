foreach(required_variable
        POCO_EXECUTABLE
        POCO_SOURCE
        POCO_GOOD_LIBRARY
        POCO_BAD_ABI_LIBRARY
        POCO_MISSING_SYMBOL_LIBRARY
        POCO_PUBLIC_HEADER
        POCO_FIXTURE_DIR)
    if(NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
        message(FATAL_ERROR "${required_variable} must be set")
    endif()
endforeach()

foreach(required_file
        "${POCO_EXECUTABLE}"
        "${POCO_SOURCE}"
        "${POCO_GOOD_LIBRARY}"
        "${POCO_BAD_ABI_LIBRARY}"
        "${POCO_MISSING_SYMBOL_LIBRARY}"
        "${POCO_PUBLIC_HEADER}")
    if(NOT EXISTS "${required_file}")
        message(FATAL_ERROR "required fixture does not exist: ${required_file}")
    endif()
endforeach()

# External modules continue to use the public 2.0.0 API contract.
file(READ "${POCO_PUBLIC_HEADER}" public_header)
foreach(version_component IN ITEMS MAJOR MINOR PATCH)
    if(version_component STREQUAL "MAJOR")
        set(expected_component 2)
    else()
        set(expected_component 0)
    endif()
    if(NOT public_header MATCHES
            "#define[ \t]+POCO_API_VERSION_${version_component}[ \t]+${expected_component}u")
        message(FATAL_ERROR
            "POCO_API_VERSION_${version_component} is not pinned to ${expected_component}")
    endif()
endforeach()

set(good_dir "${POCO_FIXTURE_DIR}/good")
set(missing_dir "${POCO_FIXTURE_DIR}/missing")
set(bad_abi_dir "${POCO_FIXTURE_DIR}/bad-abi")
set(missing_symbol_dir "${POCO_FIXTURE_DIR}/missing-symbol")
file(REMOVE_RECURSE "${POCO_FIXTURE_DIR}")
file(MAKE_DIRECTORY
    "${good_dir}"
    "${missing_dir}"
    "${bad_abi_dir}"
    "${missing_symbol_dir}")

configure_file("${POCO_SOURCE}" "${good_dir}/hello.poc" COPYONLY)
configure_file("${POCO_GOOD_LIBRARY}" "${good_dir}/hello.poe" COPYONLY)

execute_process(
    COMMAND "${POCO_EXECUTABLE}" hello.poc
    WORKING_DIRECTORY "${good_dir}"
    RESULT_VARIABLE source_result
    OUTPUT_VARIABLE source_stdout
    ERROR_VARIABLE source_stderr
)
if(NOT source_result EQUAL 0)
    message(FATAL_ERROR
        "external-library source execution failed (${source_result})\n"
        "stdout:\n${source_stdout}\nstderr:\n${source_stderr}")
endif()

execute_process(
    COMMAND "${POCO_EXECUTABLE}" -c hello.poc -o hello.pex
    WORKING_DIRECTORY "${good_dir}"
    RESULT_VARIABLE compile_result
    OUTPUT_VARIABLE compile_stdout
    ERROR_VARIABLE compile_stderr
)
set(binary "${good_dir}/hello.pex")
if(NOT compile_result EQUAL 0 OR NOT EXISTS "${binary}")
    message(FATAL_ERROR
        "external-library binary compilation failed (${compile_result})\n"
        "stdout:\n${compile_stdout}\nstderr:\n${compile_stderr}")
endif()

# The image carries compiled data and the unresolved module name, never the
# source body or a resolved absolute path.
file(READ "${POCO_SOURCE}" source_hex HEX)
file(READ "${binary}" binary_hex HEX)
string(TOLOWER "${source_hex}" source_hex)
string(TOLOWER "${binary_hex}" binary_hex)
string(FIND "${binary_hex}" "${source_hex}" source_offset)
if(NOT source_offset EQUAL -1)
    message(FATAL_ERROR "compiled external-library image retained source bytes")
endif()

file(STRINGS "${binary}" binary_strings LENGTH_MINIMUM 4)
set(found_library_name FALSE)
foreach(binary_string IN LISTS binary_strings)
    if(binary_string STREQUAL "hello.poe")
        set(found_library_name TRUE)
    endif()
    if(binary_string MATCHES "^/" OR
       binary_string MATCHES "^[A-Za-z]:[/\\\\]")
        message(FATAL_ERROR
            "compiled external-library image retained absolute path '${binary_string}'")
    endif()
endforeach()
if(NOT found_library_name)
    message(FATAL_ERROR "compiled image did not retain the requested library name hello.poe")
endif()

execute_process(
    COMMAND "${POCO_EXECUTABLE}" hello.pex
    WORKING_DIRECTORY "${good_dir}"
    RESULT_VARIABLE binary_result
    OUTPUT_VARIABLE binary_stdout
    ERROR_VARIABLE binary_stderr
)
if(NOT binary_result EQUAL source_result OR
   NOT binary_stdout STREQUAL source_stdout OR
   NOT binary_stderr STREQUAL source_stderr)
    message(FATAL_ERROR
        "external-library binary output differs from source\n"
        "source stdout:\n${source_stdout}\nbinary stdout:\n${binary_stdout}\n"
        "source stderr:\n${source_stderr}\nbinary stderr:\n${binary_stderr}")
endif()

# Exercise the composed debugger path as well: load an extended binary, reload
# and rebind its external module, start from the pre-run prompt with step, step
# across the native call, and complete the Poco program.
set(debug_binary "${good_dir}/hello-debug.pex")
execute_process(
    COMMAND "${POCO_EXECUTABLE}" -g -c hello.poc -o hello-debug.pex
    WORKING_DIRECTORY "${good_dir}"
    RESULT_VARIABLE debug_compile_result
    OUTPUT_VARIABLE debug_compile_stdout
    ERROR_VARIABLE debug_compile_stderr
)
if(NOT debug_compile_result EQUAL 0 OR NOT EXISTS "${debug_binary}")
    message(FATAL_ERROR
        "external-library debug binary compilation failed (${debug_compile_result})\n"
        "stdout:\n${debug_compile_stdout}\nstderr:\n${debug_compile_stderr}")
endif()

set(debug_commands "${good_dir}/debug-commands.txt")
file(WRITE "${debug_commands}" "step\nstep\nstep\ncontinue\n")
execute_process(
    COMMAND "${POCO_EXECUTABLE}" --debug hello-debug.pex
    WORKING_DIRECTORY "${good_dir}"
    INPUT_FILE "${debug_commands}"
    RESULT_VARIABLE debug_result
    OUTPUT_VARIABLE debug_stdout
    ERROR_VARIABLE debug_stderr
)
if(NOT debug_result EQUAL 0)
    message(FATAL_ERROR
        "external-library binary debugger failed (${debug_result})\n"
        "stdout:\n${debug_stdout}\nstderr:\n${debug_stderr}")
endif()
set(debug_output "${debug_stdout}\n${debug_stderr}")
foreach(expected IN ITEMS
		"Poco debugger: ${good_dir}/hello.poc"
		"Stopped at ${good_dir}/hello.poc:3"
		"Stopped at ${good_dir}/hello.poc:5"
        "Hello from POE!"
		"Stopped at ${good_dir}/hello.poc:6"
        "Test completed successfully!"
        "Program exited.")
    string(FIND "${debug_output}" "${expected}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR
            "external-library binary debugger omitted '${expected}'\n"
            "stdout:\n${debug_stdout}\nstderr:\n${debug_stderr}")
    endif()
endforeach()
string(FIND "${debug_output}" "Stopped at ${good_dir}/hello.poc:5" native_call_stop)
string(FIND "${debug_output}" "Hello from POE!" native_call_output)
string(FIND "${debug_output}" "Stopped at ${good_dir}/hello.poc:6" after_native_stop)
if(NOT native_call_stop LESS native_call_output OR
   NOT native_call_output LESS after_native_stop)
    message(FATAL_ERROR
        "external-library debugger did not execute the native call between Poco stops\n"
        "stdout:\n${debug_stdout}\nstderr:\n${debug_stderr}")
endif()

function(expect_binary_rejection fixture_dir library_file expected_message label)
    configure_file("${binary}" "${fixture_dir}/hello.pex" COPYONLY)
    if(NOT "${library_file}" STREQUAL "")
        configure_file("${library_file}" "${fixture_dir}/hello.poe" COPYONLY)
    endif()

    execute_process(
        COMMAND "${POCO_EXECUTABLE}" hello.pex
        WORKING_DIRECTORY "${fixture_dir}"
        RESULT_VARIABLE rejection_result
        OUTPUT_VARIABLE rejection_stdout
        ERROR_VARIABLE rejection_stderr
    )
    if(rejection_result EQUAL 0)
        message(FATAL_ERROR "${label} external library was accepted")
    endif()
    set(rejection_output "${rejection_stdout}${rejection_stderr}")
    if(rejection_output MATCHES "Hello from POE!" OR
       rejection_output MATCHES "Test completed successfully!")
        message(FATAL_ERROR
            "${label} external library produced program output before rejection:\n"
            "${rejection_output}")
    endif()
    if(NOT rejection_stderr MATCHES "${expected_message}")
        message(FATAL_ERROR
            "${label} external library did not report '${expected_message}'\n"
            "stderr:\n${rejection_stderr}")
    endif()
endfunction()

expect_binary_rejection(
    "${missing_dir}" "" "required external library was not found" "missing")
expect_binary_rejection(
    "${bad_abi_dir}" "${POCO_BAD_ABI_LIBRARY}"
    "external library ABI version mismatch" "ABI-mismatched")
expect_binary_rejection(
    "${missing_symbol_dir}" "${POCO_MISSING_SYMBOL_LIBRARY}"
    "unknown external library binding" "missing-symbol")
