if(NOT DEFINED POCO_SOURCE_DIR OR "${POCO_SOURCE_DIR}" STREQUAL "")
    message(FATAL_ERROR "POCO_SOURCE_DIR must name the Poco source tree")
endif()

if(NOT DEFINED TEST_GENERATOR OR "${TEST_GENERATOR}" STREQUAL "")
    set(TEST_GENERATOR "Ninja")
endif()

set(POCO_INCLUDE_DIR "${POCO_SOURCE_DIR}/include")
set(CANONICAL_HEADER "${POCO_INCLUDE_DIR}/poco/poco.h")
set(CONSUMER_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/public_header")
set(CONSUMER_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/poco-public-header-consumer")

if(NOT EXISTS "${CANONICAL_HEADER}")
    message(FATAL_ERROR "Missing canonical public header: ${CANONICAL_HEADER}")
endif()

file(READ "${CANONICAL_HEADER}" CANONICAL_HEADER_CONTENTS)
string(REGEX MATCH "#[ \t]*include[ \t]*[<\"]\.\.?/|#[ \t]*include[ \t]*[<\"][^>\"]*src/" INTERNAL_INCLUDE
    "${CANONICAL_HEADER_CONTENTS}")
if(INTERNAL_INCLUDE)
    message(FATAL_ERROR "The canonical public header reaches into an internal source directory: ${INTERNAL_INCLUDE}")
endif()

string(REGEX MATCHALL "#[ \t]*include[ \t]*[^\n]+" CANONICAL_INCLUDES "${CANONICAL_HEADER_CONTENTS}")
foreach(CANONICAL_INCLUDE IN LISTS CANONICAL_INCLUDES)
    if(NOT CANONICAL_INCLUDE MATCHES "^[ \t]*#[ \t]*include[ \t]*<(stddef|stdint|stdbool)\\.h>[ \t]*(/\\*.*\\*/)?$")
        message(FATAL_ERROR
            "The canonical public header may include only standard C headers; found: ${CANONICAL_INCLUDE}")
    endif()
endforeach()

file(GLOB_RECURSE INSTALLED_HEADERS LIST_DIRECTORIES false "${POCO_INCLUDE_DIR}/*.h")
foreach(INSTALLED_HEADER IN LISTS INSTALLED_HEADERS)
    file(READ "${INSTALLED_HEADER}" INSTALLED_HEADER_CONTENTS)
    string(REGEX MATCH "#[ \t]*include[ \t]*[<\"]\.\.?/|#[ \t]*include[ \t]*[<\"][^>\"]*src/" INSTALLED_INTERNAL_INCLUDE
        "${INSTALLED_HEADER_CONTENTS}")
    if(INSTALLED_INTERNAL_INCLUDE)
        message(FATAL_ERROR
            "Installed header ${INSTALLED_HEADER} reaches into an internal source directory: ${INSTALLED_INTERNAL_INCLUDE}")
    endif()
endforeach()

file(REMOVE_RECURSE "${CONSUMER_BINARY_DIR}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${CONSUMER_SOURCE_DIR}" -B "${CONSUMER_BINARY_DIR}"
        -G "${TEST_GENERATOR}" "-DPOCO_INCLUDE_DIR=${POCO_INCLUDE_DIR}"
    RESULT_VARIABLE CONFIGURE_RESULT
    OUTPUT_VARIABLE CONFIGURE_OUTPUT
    ERROR_VARIABLE CONFIGURE_ERROR
)
if(NOT CONFIGURE_RESULT EQUAL 0)
    message(FATAL_ERROR
        "The public-header-only consumer did not configure.\n${CONFIGURE_OUTPUT}\n${CONFIGURE_ERROR}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${CONSUMER_BINARY_DIR}"
    RESULT_VARIABLE BUILD_RESULT
    OUTPUT_VARIABLE BUILD_OUTPUT
    ERROR_VARIABLE BUILD_ERROR
)
if(NOT BUILD_RESULT EQUAL 0)
    message(FATAL_ERROR
        "The public-header-only consumer did not compile.\n${BUILD_OUTPUT}\n${BUILD_ERROR}")
endif()
