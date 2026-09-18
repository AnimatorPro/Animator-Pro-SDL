cmake_minimum_required(VERSION 3.25 FATAL_ERROR)

include("${CMAKE_SOURCE_DIR}/poco/cmake/PocoPoeLibrary.cmake")

add_poe_library(test_noentry
    SOURCES noentry.c
    OUTPUT_NAME noentry
    NO_INSTALL
)

# The loader diagnostic is registered beside the fixture that produces it: the
# script resolves the module out of the build tree, so the test cannot pass by
# failing to find anything at all.
if(POCO_BUILD_TESTS)
    add_test(
        NAME "poco_neg_lib02_no_entry"
        COMMAND $<TARGET_FILE:poco>
            "${CMAKE_SOURCE_DIR}/poco/test/neg/nlib02.poc"
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:test_noentry>"
    )
    set_tests_properties("poco_neg_lib02_no_entry" PROPERTIES
        PASS_REGULAR_EXPRESSION "'noentry' is missing entry point 'poco_module_get'"
        TIMEOUT 10
    )
endif()
