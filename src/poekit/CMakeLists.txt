cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

# Animator-owned native POE modules.  They need Animator headers, the Animator
# POE helper, or Animator's legacy Pocorex ABI, so they live in the consumer
# tree rather than inside poco/.  The one host-neutral descriptor module is
# poco/examples/hello, which belongs to Poco and builds without Animator.
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

# Retained Animator-native POE modules.  Their CMake files use the Ani helper
# and explicitly request the legacy direct-function-table policy.
if(WITH_ANI)
    add_subdirectory(colorutl)
    add_subdirectory(pstamp)
endif()

# Loader fixtures only; they deliberately exercise malformed legacy entries
# built against Animator's Pocorex headers.
add_subdirectory(test_libs/noentry)
add_subdirectory(test_libs/nullret)
add_subdirectory(test_libs/badver)
add_subdirectory(test_libs/empty)
