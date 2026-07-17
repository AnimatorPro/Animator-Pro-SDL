cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

project(poco_public_header_consumer LANGUAGES C CXX)

if(NOT DEFINED POCO_INCLUDE_DIR OR "${POCO_INCLUDE_DIR}" STREQUAL "")
    message(FATAL_ERROR "POCO_INCLUDE_DIR must name Poco's installed include directory")
endif()

add_executable(poco_public_header_c c_consumer.c)
target_include_directories(poco_public_header_c PRIVATE "${POCO_INCLUDE_DIR}")

add_executable(poco_public_header_cxx cpp_consumer.cpp)
target_include_directories(poco_public_header_cxx PRIVATE "${POCO_INCLUDE_DIR}")
