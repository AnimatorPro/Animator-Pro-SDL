cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

# Current POE inventory.  Source-only entries are retained as historical
# examples; no modern target consumes them.
set(POEKIT_GENERIC_MODULES
    hello
)
set(POEKIT_ANI_MODULES
    colorutl
    pstamp
)
set(POEKIT_TEST_ONLY_MODULES
    test_libs/noentry
    test_libs/nullret
    test_libs/badver
    test_libs/empty
)
set(POEKIT_LEGACY_DEAD_MODULES
    cycle
    eco
    flicplay
    lookup
    otdemo
    skeleton
    vscreen
)

# Host-neutral descriptor module.
add_subdirectory(hello)

# Retained Animator-native POE modules.  Their CMake files use the Ani helper
# and explicitly request the legacy direct-function-table policy.  Do not
# configure them for a Poco-only parent build.
if(WITH_ANI)
    add_subdirectory(colorutl)
    add_subdirectory(pstamp)
endif()

# Loader fixtures only; they deliberately exercise malformed legacy entries.
add_subdirectory(test_libs/noentry)
add_subdirectory(test_libs/nullret)
add_subdirectory(test_libs/badver)
add_subdirectory(test_libs/empty)
