cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

project(poco_external_consumer LANGUAGES C)

if(NOT DEFINED POCO_SOURCE_DIR OR "${POCO_SOURCE_DIR}" STREQUAL "")
    message(FATAL_ERROR "POCO_SOURCE_DIR must name the copied Poco tree")
endif()

# The consumer-side Poco contract is deliberately limited to these three
# touchpoints: add_subdirectory, Poco::poco, and <poco/poco.h>.
add_subdirectory("${POCO_SOURCE_DIR}" poco)

add_executable(poco_external_consumer main.c)
target_link_libraries(poco_external_consumer PRIVATE Poco::poco)
target_compile_definitions(poco_external_consumer PRIVATE
    POCO_CONSUMER_SCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/consumer.poc"
)
