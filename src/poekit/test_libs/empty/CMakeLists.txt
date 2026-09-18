cmake_minimum_required(VERSION 3.25 FATAL_ERROR)

include("${CMAKE_SOURCE_DIR}/poco/cmake/PocoPoeLibrary.cmake")

add_poe_library(test_empty
    SOURCES empty.c
    INCLUDES "${CMAKE_SOURCE_DIR}/src/inc"
    OUTPUT_NAME empty
    NO_INSTALL
)

if(POCO_BUILD_TESTS)
    add_test(
        NAME "poco_neg_lib05_empty"
        COMMAND $<TARGET_FILE:poco_cli>
            "${CMAKE_SOURCE_DIR}/poco/test/neg/nlib05.poc"
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:test_empty>"
    )
    set_tests_properties("poco_neg_lib05_empty" PROPERTIES
        PASS_REGULAR_EXPRESSION "'empty' is a legacy native-POE module"
        TIMEOUT 10
    )
endif()
