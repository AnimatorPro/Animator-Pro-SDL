cmake_minimum_required(VERSION 3.25 FATAL_ERROR)

# The canonical host-neutral native module: no Animator headers, no direct
# function tables, no legacy Pocorex entry point.  It is built with the same
# installed helper an external module author uses.
include(PocoModule)

poco_add_module(hello SOURCES "${CMAKE_CURRENT_LIST_DIR}/hello.c")

# Keep hello.poc beside the module so the example runs from any working
# directory, matching the layout of examples/snake.
add_custom_command(TARGET hello POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_LIST_DIR}/hello.poc"
        "$<TARGET_FILE_DIR:hello>/hello.poc"
    VERBATIM
)

# Installed whenever it is built.  This directory is only added under the same
# condition (see poco/CMakeLists.txt), so a module that exists in the build
# tree always has an installed location, and a host asserting on that location
# does not have to reason about which of the two knobs is on.
install(TARGETS hello LIBRARY DESTINATION examples/hello)
install(FILES "${CMAKE_CURRENT_LIST_DIR}/hello.poc" DESTINATION examples/hello)

# The test runs against the build tree so it does not require `cmake --install`
# to have been run, and so it cannot silently exercise a stale or foreign
# module left in the install prefix.
if(POCO_BUILD_TESTS)
    add_test(
        NAME poco_generic_hello
        COMMAND $<TARGET_FILE:poco_cli> "${CMAKE_CURRENT_LIST_DIR}/hello.poc"
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:hello>"
    )
    set_tests_properties(poco_generic_hello PROPERTIES
        PASS_REGULAR_EXPRESSION "Test completed successfully!"
        TIMEOUT 10
    )
endif()
