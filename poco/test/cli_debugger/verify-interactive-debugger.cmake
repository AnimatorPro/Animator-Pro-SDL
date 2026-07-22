if(NOT DEFINED POCO_EXECUTABLE OR NOT DEFINED POCO_SOURCE OR
   NOT DEFINED POCO_COMMANDS OR NOT DEFINED POCO_FIXTURE_DIR)
    message(FATAL_ERROR "interactive debugger test is missing required paths")
endif()

file(MAKE_DIRECTORY "${POCO_FIXTURE_DIR}")
set(source_copy "${POCO_FIXTURE_DIR}/interactive_debugger.poc")
set(binary "${POCO_FIXTURE_DIR}/interactive_debugger.pex")
file(COPY_FILE "${POCO_SOURCE}" "${source_copy}" ONLY_IF_DIFFERENT)

function(run_debugger input label)
    execute_process(
        COMMAND "${POCO_EXECUTABLE}" --debug "${input}"
        INPUT_FILE "${POCO_COMMANDS}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "${label} debugger failed (${result})\nstdout:\n${output}\nstderr:\n${error}")
    endif()
    set(combined "${output}\n${error}")
    foreach(expected IN ITEMS
            "Breakpoint set at line 19"
            "Breakpoint cleared at line 19"
            "Stopped at ${source_copy}:19"
            "answer = 4"
            "Stopped at ${source_copy}:11"
            "Stopped at ${source_copy}:12"
            "local = 7"
            "Stopped at ${source_copy}:13"
            "#0 helper at line 13"
            "#1 main at line 19"
            "   14  }"
            "Program exited."
            "Return value: 15")
        string(FIND "${combined}" "${expected}" found)
        if(found EQUAL -1)
            message(FATAL_ERROR
                "${label} debugger output omitted '${expected}'\nstdout:\n${output}\nstderr:\n${error}")
        endif()
    endforeach()
endfunction()

function(run_initial_step input label)
    set(step_commands "${POCO_FIXTURE_DIR}/${label}-initial-step.txt")
    file(WRITE "${step_commands}" "step\nquit\n")
    execute_process(
        COMMAND "${POCO_EXECUTABLE}" --debug "${input}"
        INPUT_FILE "${step_commands}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR
            "${label} initial debugger step failed (${result})\nstdout:\n${output}\nstderr:\n${error}")
    endif()
    set(combined "${output}\n${error}")
    string(FIND "${combined}" "Stopped at ${source_copy}:16" stop_found)
    if(stop_found EQUAL -1)
        message(FATAL_ERROR
            "${label} initial step did not stop at the first mapped line\n"
            "stdout:\n${output}\nstderr:\n${error}")
    endif()
    string(FIND "${combined}" "requires a stopped program" rejection_found)
    if(NOT rejection_found EQUAL -1)
        message(FATAL_ERROR
            "${label} initial step was rejected\nstdout:\n${output}\nstderr:\n${error}")
    endif()
endfunction()

run_initial_step("${source_copy}" "source")
run_debugger("${source_copy}" "source")

# A source invocation may use a relative read path even though the compiled
# unit retains its canonical identity. The debugger must associate the two
# after verification instead of treating the relative path as the identity.
set(relative_step_commands "${POCO_FIXTURE_DIR}/relative-initial-step.txt")
file(WRITE "${relative_step_commands}" "step\nquit\n")
execute_process(
    COMMAND "${POCO_EXECUTABLE}" --debug "interactive_debugger.poc"
    WORKING_DIRECTORY "${POCO_FIXTURE_DIR}"
    INPUT_FILE "${relative_step_commands}"
    RESULT_VARIABLE relative_result
    OUTPUT_VARIABLE relative_output
    ERROR_VARIABLE relative_error
)
if(NOT relative_result EQUAL 0)
    message(FATAL_ERROR
        "relative-path debugger failed (${relative_result})\n"
        "stdout:\n${relative_output}\nstderr:\n${relative_error}")
endif()
set(relative_combined "${relative_output}\n${relative_error}")
string(FIND "${relative_combined}" "Stopped at ${source_copy}:16" relative_stop_found)
if(relative_stop_found EQUAL -1)
    message(FATAL_ERROR
        "relative-path debugger did not resolve its canonical source identity\n"
        "stdout:\n${relative_output}\nstderr:\n${relative_error}")
endif()

# Quitting from a synchronous pause must unwind through cancellation without
# presenting the interrupted activation as a normally completed program.
set(quit_commands "${POCO_FIXTURE_DIR}/quit-commands.txt")
file(WRITE "${quit_commands}" "break 19\ncontinue\nquit\n")
execute_process(
    COMMAND "${POCO_EXECUTABLE}" --debug "${source_copy}"
    INPUT_FILE "${quit_commands}"
    RESULT_VARIABLE quit_result
    OUTPUT_VARIABLE quit_output
    ERROR_VARIABLE quit_error
)
if(NOT quit_result EQUAL 0)
    message(FATAL_ERROR
        "quit-at-breakpoint debugger failed (${quit_result})\nstdout:\n${quit_output}\nstderr:\n${quit_error}")
endif()
set(quit_combined "${quit_output}\n${quit_error}")
string(FIND "${quit_combined}" "Stopped at ${source_copy}:19" quit_stop_found)
if(quit_stop_found EQUAL -1)
    message(FATAL_ERROR
        "quit-at-breakpoint debugger did not reach the breakpoint\nstdout:\n${quit_output}\nstderr:\n${quit_error}")
endif()
foreach(unexpected IN ITEMS "Program exited." "Return value:")
    string(FIND "${quit_combined}" "${unexpected}" quit_completion_found)
    if(NOT quit_completion_found EQUAL -1)
        message(FATAL_ERROR
            "quit-at-breakpoint debugger reported '${unexpected}'\nstdout:\n${quit_output}\nstderr:\n${quit_error}")
    endif()
endforeach()

execute_process(
    COMMAND "${POCO_EXECUTABLE}" -g -c -o "${binary}" "${source_copy}"
    RESULT_VARIABLE compile_result
    OUTPUT_VARIABLE compile_output
    ERROR_VARIABLE compile_error
)
if(NOT compile_result EQUAL 0)
    message(FATAL_ERROR
        "extended binary compile failed (${compile_result})\nstdout:\n${compile_output}\nstderr:\n${compile_error}")
endif()
run_debugger("${binary}" "extended binary")
run_initial_step("${binary}" "extended-binary")

file(APPEND "${source_copy}" "\n/* deliberate source hash mismatch */\n")
execute_process(
    COMMAND "${POCO_EXECUTABLE}" --debug "${binary}"
    RESULT_VARIABLE mismatch_result
    OUTPUT_VARIABLE mismatch_output
    ERROR_VARIABLE mismatch_error
)
if(mismatch_result EQUAL 0)
    message(FATAL_ERROR "source hash mismatch was accepted")
endif()
string(FIND "${mismatch_error}" "source hash mismatch" mismatch_found)
if(mismatch_found EQUAL -1)
    message(FATAL_ERROR
        "source hash mismatch diagnostic missing\nstdout:\n${mismatch_output}\nstderr:\n${mismatch_error}")
endif()
