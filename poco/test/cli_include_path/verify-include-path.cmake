foreach(required_variable POCO_EXECUTABLE POCO_FIXTURE_DIR POCO_OUTPUT)
    if(NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
        message(FATAL_ERROR "${required_variable} must be set")
    endif()
endforeach()

set(FIRST_INCLUDE "${POCO_FIXTURE_DIR}/first")
set(SECOND_INCLUDE "${POCO_FIXTURE_DIR}/second")
set(MAIN_SOURCE "${POCO_FIXTURE_DIR}/main.poc")
set(TWO_INCLUDE_SOURCE "${POCO_FIXTURE_DIR}/two_includes.poc")
set(SIBLING_SOURCE "${POCO_FIXTURE_DIR}/sibling/sibling.poc")

get_filename_component(POCO_OUTPUT_DIRECTORY "${POCO_OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${POCO_OUTPUT_DIRECTORY}")

# Run the CLI and require the given exit code.  Output is returned so a caller
# can assert on the "Return value: N" line the run path prints to stderr.
function(run_poco expectation description)
    execute_process(
        COMMAND ${ARGN}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout_text
        ERROR_VARIABLE stderr_text
    )
    if(expectation STREQUAL "success")
        if(NOT result EQUAL 0)
            message(FATAL_ERROR
                "${description} failed (${result})\nstdout:\n${stdout_text}\nstderr:\n${stderr_text}")
        endif()
    elseif(result EQUAL 0)
        message(FATAL_ERROR
            "${description} unexpectedly succeeded\nstdout:\n${stdout_text}\nstderr:\n${stderr_text}")
    endif()
    set(poco_stdout "${stdout_text}" PARENT_SCOPE)
    set(poco_stderr "${stderr_text}" PARENT_SCOPE)
endfunction()

function(expect_return_value description expected)
    if(NOT poco_stderr MATCHES "Return value: ${expected}\n")
        message(FATAL_ERROR
            "${description} did not return ${expected}\nstderr:\n${poco_stderr}")
    endif()
endfunction()

# Without -I the include is unresolvable: the fixture's directory holds no
# shared.h.  This is the gap the flag closes, so prove it is still a failure.
run_poco(failure "poco with no include directory"
    "${POCO_EXECUTABLE}" -c "${MAIN_SOURCE}")
if(NOT poco_stdout MATCHES "shared.h" AND NOT poco_stderr MATCHES "shared.h")
    message(FATAL_ERROR
        "the missing include was not diagnosed by name\nstdout:\n${poco_stdout}\n"
        "stderr:\n${poco_stderr}")
endif()

# -I makes the header reachable and the program runs.
run_poco(success "poco -I" "${POCO_EXECUTABLE}" -I "${FIRST_INCLUDE}" "${MAIN_SOURCE}")
expect_return_value("poco -I" 11)

# --include and --include=<dir> are the same option.
run_poco(success "poco --include" "${POCO_EXECUTABLE}" --include "${FIRST_INCLUDE}" "${MAIN_SOURCE}")
expect_return_value("poco --include" 11)

run_poco(success "poco --include=" "${POCO_EXECUTABLE}" "--include=${FIRST_INCLUDE}" "${MAIN_SOURCE}")
expect_return_value("poco --include=" 11)

# Repeated flags accumulate in command-line order, and a header present in both
# directories resolves to the first one named.  second_only.h proves the second
# directory is searched rather than ignored.
run_poco(success "poco -I first -I second"
    "${POCO_EXECUTABLE}" -I "${FIRST_INCLUDE}" -I "${SECOND_INCLUDE}" "${TWO_INCLUDE_SOURCE}")
expect_return_value("poco -I first -I second" 16)

run_poco(success "poco -I second -I first"
    "${POCO_EXECUTABLE}" -I "${SECOND_INCLUDE}" -I "${FIRST_INCLUDE}" "${TWO_INCLUDE_SOURCE}")
expect_return_value("poco -I second -I first" 27)

# The source file's own directory still wins over any -I directory.
run_poco(success "poco -I beside a sibling header"
    "${POCO_EXECUTABLE}" -I "${FIRST_INCLUDE}" "${SIBLING_SOURCE}")
expect_return_value("poco -I beside a sibling header" 33)

# -I composes with compile-to-binary rather than being a run-only convenience.
file(REMOVE "${POCO_OUTPUT}")
run_poco(success "poco -c -I -o"
    "${POCO_EXECUTABLE}" -c -I "${FIRST_INCLUDE}" "${MAIN_SOURCE}" -o "${POCO_OUTPUT}")
if(NOT EXISTS "${POCO_OUTPUT}")
    message(FATAL_ERROR "poco -c -I did not create ${POCO_OUTPUT}")
endif()
run_poco(success "running the -I compiled image" "${POCO_EXECUTABLE}" "${POCO_OUTPUT}")
expect_return_value("running the -I compiled image" 11)
file(REMOVE "${POCO_OUTPUT}")

# A missing argument is a usage error, not a crash, for each spelling.
foreach(missing_argument_case "-I" "--include" "--include=")
    run_poco(failure "poco ${missing_argument_case} with no argument"
        "${POCO_EXECUTABLE}" "${missing_argument_case}")
    if(NOT poco_stderr MATCHES "requires an argument")
        message(FATAL_ERROR
            "poco ${missing_argument_case} with no argument lost its usage message\n"
            "stdout:\n${poco_stdout}\nstderr:\n${poco_stderr}")
    endif()
endforeach()

# usage() is the only place this flag can be discovered.
run_poco(success "poco with no input" "${POCO_EXECUTABLE}")
if(NOT poco_stdout MATCHES "-I, --include")
    message(FATAL_ERROR "usage() does not list the include flag\nstdout:\n${poco_stdout}")
endif()
