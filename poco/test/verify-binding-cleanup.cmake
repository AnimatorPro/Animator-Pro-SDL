cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

foreach(required_variable POCO_SOURCE_DIR POCO_EXECUTABLE UNAVAILABLE_API_SCRIPT)
    if(NOT DEFINED ${required_variable})
        message(FATAL_ERROR "${required_variable} is required")
    endif()
endforeach()

function(require_absent_path path)
    if(EXISTS "${path}")
        message(FATAL_ERROR "Obsolete dummy binding catalog remains: ${path}")
    endif()
endfunction()

function(require_absent_literal path literal)
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "Required source evidence is missing: ${path}")
    endif()
    file(READ "${path}" content)
    string(FIND "${content}" "${literal}" found_at)
    if(NOT found_at EQUAL -1)
        message(FATAL_ERROR "Obsolete dummy binding evidence '${literal}' remains in ${path}")
    endif()
endfunction()

require_absent_path("${POCO_SOURCE_DIR}/src/dummylib.c")
foreach(source
    "${POCO_SOURCE_DIR}/CMakeLists.txt"
    "${POCO_SOURCE_DIR}/src/main.c"
    "${POCO_SOURCE_DIR}/src/POCO.LNK"
    "${POCO_SOURCE_DIR}/src/POCFILES.INC")
    require_absent_literal("${source}" "dummylib")
    require_absent_literal("${source}" "po_dummy_lib")
endforeach()

execute_process(
    COMMAND "${POCO_EXECUTABLE}" "${UNAVAILABLE_API_SCRIPT}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
)
if(result EQUAL 0)
    message(FATAL_ERROR "Standalone Poco accepted the Animator-only Qchoice API")
endif()

set(output "${stdout}\n${stderr}")
string(FIND "${output}" "Qchoice undefined" undefined_at)
if(undefined_at EQUAL -1)
    message(FATAL_ERROR
        "Standalone Poco rejected Qchoice without a clear unavailable-API diagnostic:\n${output}")
endif()
