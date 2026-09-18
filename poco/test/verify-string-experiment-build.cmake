cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

# The String type (POSTRING.C) is an experiment that is retained but not
# shipped: nothing in the default build compiles it, so it rotted silently for
# thirty years behind an unconditional `#undef`.  This gate configures a second
# build with POCO_STRING_EXPERIMENT=ON and requires the library and the CLI --
# main.c and trace.c carry guarded code too -- to compile.
# Compile-clean is the bar; the experiment has no behavioural tests.

foreach(_required_var IN ITEMS POCO_SOURCE_DIR POCO_BUILD_DIR)
    if(NOT DEFINED ${_required_var} OR "${${_required_var}}" STREQUAL "")
        message(FATAL_ERROR "${_required_var} must be set")
    endif()
endforeach()

if(NOT EXISTS "${POCO_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "POCO_SOURCE_DIR is not a Poco source directory: ${POCO_SOURCE_DIR}")
endif()

set(_generator_args)
if(DEFINED TEST_GENERATOR AND NOT "${TEST_GENERATOR}" STREQUAL "")
    list(APPEND _generator_args -G "${TEST_GENERATOR}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        -S "${POCO_SOURCE_DIR}"
        -B "${POCO_BUILD_DIR}"
        ${_generator_args}
        -DCMAKE_BUILD_TYPE=Debug
        -DPOCO_STRING_EXPERIMENT=ON
        -DPOCO_BUILD_TESTS=OFF
        -DPOCO_BUILD_EXAMPLES=OFF
    RESULT_VARIABLE _configure_result
    OUTPUT_VARIABLE _configure_stdout
    ERROR_VARIABLE _configure_stderr
)
if(NOT "${_configure_result}" STREQUAL "0")
    message(FATAL_ERROR
        "POCO_STRING_EXPERIMENT=ON configure failed (${_configure_result}):\n"
        "${_configure_stdout}\n${_configure_stderr}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${POCO_BUILD_DIR}"
    RESULT_VARIABLE _build_result
    OUTPUT_VARIABLE _build_stdout
    ERROR_VARIABLE _build_stderr
)
if(NOT "${_build_result}" STREQUAL "0")
    message(FATAL_ERROR
        "POCO_STRING_EXPERIMENT=ON build failed (${_build_result}):\n"
        "${_build_stdout}\n${_build_stderr}")
endif()

message(STATUS "Poco compiles with POCO_STRING_EXPERIMENT=ON")
