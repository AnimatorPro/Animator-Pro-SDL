cmake_minimum_required(VERSION 3.25 FATAL_ERROR)

include("${CMAKE_SOURCE_DIR}/poco/cmake/PocoPoeLibrary.cmake")

# Test-only legacy loader fixture.  Its Animator compatibility header is an
# explicit caller include, never a transitive property of the generic helper.
add_poe_library(test_badver
    SOURCES badver.c
    INCLUDES "${CMAKE_SOURCE_DIR}/src/inc"
    OUTPUT_NAME badver
    NO_INSTALL
)

if(POCO_BUILD_TESTS)
    add_test(
        NAME "poco_neg_lib04_bad_ver"
        COMMAND $<TARGET_FILE:poco>
            "${CMAKE_SOURCE_DIR}/poco/test/neg/nlib04.poc"
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:test_badver>"
    )
    set_tests_properties("poco_neg_lib04_bad_ver" PROPERTIES
        PASS_REGULAR_EXPRESSION "'badver' is a legacy native-POE module"
        TIMEOUT 10
    )
endif()
