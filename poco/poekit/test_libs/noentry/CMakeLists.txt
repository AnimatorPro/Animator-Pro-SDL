cmake_minimum_required(VERSION 3.25 FATAL_ERROR)

include("${CMAKE_SOURCE_DIR}/poco/cmake/PocoPoeLibrary.cmake")

add_poe_library(test_noentry
    SOURCES noentry.c
    OUTPUT_NAME noentry
    INSTALL_DIR ${CMAKE_INSTALL_PREFIX}/tests
)
