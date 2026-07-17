cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

foreach(_required_var IN ITEMS POCO_EXECUTABLE POCO_SCRIPT)
    if(NOT DEFINED ${_required_var} OR "${${_required_var}}" STREQUAL "")
        message(FATAL_ERROR "${_required_var} must be set")
    endif()
endforeach()

if(NOT EXISTS "${POCO_EXECUTABLE}")
    message(FATAL_ERROR "Animator integration control cannot find Poco executable: ${POCO_EXECUTABLE}")
endif()
if(NOT EXISTS "${POCO_SCRIPT}")
    message(FATAL_ERROR "Animator integration control cannot find script: ${POCO_SCRIPT}")
endif()

execute_process(
    COMMAND "${POCO_EXECUTABLE}" "${POCO_SCRIPT}"
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _stdout
    ERROR_VARIABLE _stderr
)
if(NOT "${_result}" STREQUAL "0")
    message(FATAL_ERROR
        "Animator-integrated Poco failed to run ${POCO_SCRIPT} (${_result}):\n${_stdout}\n${_stderr}")
endif()

string(FIND "${_stdout}" "hi" _greeting_offset)
if(_greeting_offset EQUAL -1)
    message(FATAL_ERROR
        "Animator-integrated Poco ran but did not produce the expected greeting:\n${_stdout}\n${_stderr}")
endif()

message(STATUS "Animator integration control passed: ${POCO_EXECUTABLE} ran ${POCO_SCRIPT}.")
