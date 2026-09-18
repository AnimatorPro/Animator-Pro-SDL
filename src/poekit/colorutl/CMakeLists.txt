cmake_minimum_required(VERSION 3.25 FATAL_ERROR)

# This retained module deliberately uses Animator's direct POE table.  Keep
# that build/runtime policy in the Ani helper, never in the generic helper.
include("${CMAKE_SOURCE_DIR}/src/ani_poco/AniPocoPoeLibrary.cmake")

# Keep this policy-only smoke test: it proves a legacy POE cannot load in a
# generic VM and is admitted only after the Ani module policy is installed.
ani_add_poe_library(colorutl
    SOURCES colorutl.c rgbhls.c
    TEST_FILE ani_policy_coltest.poc
    RUNNER ani-policy
    LEGACY_POE
)

add_executable(colorutl_test_host colorutl_test_host.c)
target_link_libraries(colorutl_test_host PRIVATE ani_poco_adapter)
add_dependencies(colorutl_test_host colorutl)

# The full Animator-runtime fixture links the completed adapter registration
# path and exits as soon as the script finishes.  This pointer-heavy module
# regression therefore exercises its functions with Animator's actual library
# catalog, while the test above remains a focused module-policy smoke check.
install(FILES COLTEST.POC DESTINATION ${CMAKE_INSTALL_PREFIX}/tests)
add_dependencies(ani_poco_registration colorutl)
add_test(
    NAME poco_colorutl_functional
    COMMAND $<TARGET_FILE:ani_poco_registration>
        ${CMAKE_INSTALL_PREFIX}/tests/COLTEST.POC
    WORKING_DIRECTORY ${CMAKE_INSTALL_PREFIX}
)
set_tests_properties(poco_colorutl_functional PROPERTIES
    TIMEOUT 15
)
