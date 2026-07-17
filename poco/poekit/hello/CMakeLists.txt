cmake_minimum_required(VERSION 3.25 FATAL_ERROR)

# This example intentionally uses the generic module helper.  It has no
# Animator headers, direct function tables, or legacy Pocorex entry point.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/poco/cmake")
include(PocoModule)

# Build hello.poe using the host-neutral Poco module ABI.
poco_add_module(hello
    SOURCES hello.c
    INSTALL_DIR ${CMAKE_INSTALL_PREFIX}/tests
)
install(FILES hello.poc DESTINATION ${CMAKE_INSTALL_PREFIX}/tests)
add_test(
    NAME poco_generic_hello
    COMMAND ${CMAKE_INSTALL_PREFIX}/poco ${CMAKE_INSTALL_PREFIX}/tests/hello.poc
    WORKING_DIRECTORY ${CMAKE_INSTALL_PREFIX}
)
set_tests_properties(poco_generic_hello PROPERTIES
    PASS_REGULAR_EXPRESSION "Test completed successfully!"
    TIMEOUT 10
)
