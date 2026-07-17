# Poco-private hashmap build.  Do not import the upstream project's global
# options, package exports, C standard, or target names into a Poco consumer.
add_library(poco_hashmap STATIC
    src/hashmap.c
)
set_target_properties(poco_hashmap PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
    POSITION_INDEPENDENT_CODE YES
)
target_include_directories(poco_hashmap
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
