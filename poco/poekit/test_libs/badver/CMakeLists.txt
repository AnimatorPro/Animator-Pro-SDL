cmake_minimum_required(VERSION 3.25 FATAL_ERROR)

include("${CMAKE_SOURCE_DIR}/poco/cmake/PocoPoeLibrary.cmake")

# Test-only legacy loader fixture.  Its Animator compatibility header is an
# explicit caller include, never a transitive property of the generic helper.
add_poe_library(test_badver
    SOURCES badver.c
    INCLUDES "${CMAKE_SOURCE_DIR}/src/inc"
    OUTPUT_NAME badver
    INSTALL_DIR ${CMAKE_INSTALL_PREFIX}/tests
)
