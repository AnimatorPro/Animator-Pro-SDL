foreach(required_variable POCO_EXECUTABLE POCO_SOURCE POCO_OUTPUT_DIR)
    if(NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
        message(FATAL_ERROR "${required_variable} must be set")
    endif()
endforeach()

# Separate processes, not one: ASLR moves the stack and heap between runs, so
# any host address or uninitialised slot that leaks into the image shows up as
# a byte difference here even when two compiles in one process would agree.
file(MAKE_DIRECTORY "${POCO_OUTPUT_DIR}")
set(images "")
foreach(run 1 2 3)
    set(image "${POCO_OUTPUT_DIR}/reproducible_${run}.pex")
    file(REMOVE "${image}")
    execute_process(
        COMMAND "${POCO_EXECUTABLE}" -c -o "${image}" "${POCO_SOURCE}"
        RESULT_VARIABLE compile_result
        OUTPUT_VARIABLE compile_stdout
        ERROR_VARIABLE compile_stderr
    )
    if(NOT compile_result EQUAL 0 OR NOT EXISTS "${image}")
        message(FATAL_ERROR
            "poco -c -o failed on run ${run} (${compile_result})\n"
            "stdout:\n${compile_stdout}\nstderr:\n${compile_stderr}")
    endif()
    list(APPEND images "${image}")
endforeach()

list(GET images 0 reference)
file(SHA256 "${reference}" reference_hash)
foreach(image IN LISTS images)
    file(SHA256 "${image}" image_hash)
    if(NOT image_hash STREQUAL reference_hash)
        message(FATAL_ERROR
            "program images differ between runs of the same compile:\n"
            "  ${reference} ${reference_hash}\n  ${image} ${image_hash}")
    endif()
endforeach()

# The image must still be the program: sum_points gives 36, scale gives 4.
execute_process(
    COMMAND "${POCO_EXECUTABLE}" "${reference}"
    RESULT_VARIABLE run_result
    OUTPUT_VARIABLE run_stdout
    ERROR_VARIABLE run_stderr
)
if(NOT run_result EQUAL 0 OR NOT run_stderr MATCHES "Return value: 40")
    message(FATAL_ERROR
        "running the compiled image did not return 40 (${run_result})\n"
        "stdout:\n${run_stdout}\nstderr:\n${run_stderr}")
endif()
