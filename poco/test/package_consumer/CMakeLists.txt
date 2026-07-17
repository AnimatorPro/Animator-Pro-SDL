cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

project(poco_package_consumer LANGUAGES C)

find_package(Poco 1 CONFIG REQUIRED)

if(NOT COMMAND poco_add_module)
    message(FATAL_ERROR "Poco's installed module helper was not loaded")
endif()

add_executable(poco_package_consumer main.c)
target_link_libraries(poco_package_consumer PRIVATE Poco::poco)
target_compile_definitions(poco_package_consumer PRIVATE
    POCO_CONSUMER_SCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/consumer.poc"
)
