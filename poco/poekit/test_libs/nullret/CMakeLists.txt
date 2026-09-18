cmake_minimum_required(VERSION 3.25 FATAL_ERROR)

include("${CMAKE_SOURCE_DIR}/poco/cmake/PocoPoeLibrary.cmake")

add_poe_library(test_nullret
    SOURCES nullret.c
    INCLUDES "${CMAKE_SOURCE_DIR}/src/inc"
    OUTPUT_NAME nullret
    NO_INSTALL
)

if(POCO_BUILD_TESTS)
    add_test(
        NAME "poco_neg_lib03_null_ret"
        COMMAND $<TARGET_FILE:poco>
            "${CMAKE_SOURCE_DIR}/poco/test/neg/nlib03.poc"
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:test_nullret>"
    )
    set_tests_properties("poco_neg_lib03_null_ret" PROPERTIES
        PASS_REGULAR_EXPRESSION "'nullret' is a legacy native-POE module"
        TIMEOUT 10
    )
endif()
