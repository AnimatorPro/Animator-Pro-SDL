include(PocoModule)

set(POCO_MODULE_TEST_INSTALL_DIR "tests")

poco_add_module(poco_generic_module
    SOURCES generic.c
    INSTALL_DIR "${POCO_MODULE_TEST_INSTALL_DIR}"
)

poco_add_module(poco_generic_bad_version
    SOURCES bad_version.c
    INSTALL_DIR "${POCO_MODULE_TEST_INSTALL_DIR}"
)

poco_add_module(poco_generic_empty
    SOURCES empty.c
    INSTALL_DIR "${POCO_MODULE_TEST_INSTALL_DIR}"
)

# This intentionally has neither the generic entry point nor the legacy
# compatibility entry point.
poco_add_module(poco_generic_missing_entry
    SOURCES missing_entry.c
    INSTALL_DIR "${POCO_MODULE_TEST_INSTALL_DIR}"
)

install(FILES
    generic.poc
    missing_entry.poc
    bad_version.poc
    empty.poc
    DESTINATION tests
)

add_test(
    NAME poco_generic_module_valid
    COMMAND "${CMAKE_INSTALL_PREFIX}/poco" "${CMAKE_INSTALL_PREFIX}/tests/generic.poc"
    WORKING_DIRECTORY "${CMAKE_INSTALL_PREFIX}"
)
set_tests_properties(poco_generic_module_valid PROPERTIES
    PASS_REGULAR_EXPRESSION "generic module loaded"
    TIMEOUT 10
)

set(POCO_MODULE_OUTCOME_missing_entry "missing entry point 'poco_module_get'")
set(POCO_MODULE_OUTCOME_bad_version "version mismatch")
set(POCO_MODULE_OUTCOME_empty "contains no functions")

foreach(outcome IN ITEMS missing_entry bad_version empty)
    add_test(
        NAME "poco_generic_module_${outcome}"
        COMMAND "${CMAKE_COMMAND}"
            "-DPOCO_EXECUTABLE=${CMAKE_INSTALL_PREFIX}/poco"
            "-DPOCO_SCRIPT=${CMAKE_INSTALL_PREFIX}/tests/${outcome}.poc"
            "-DEXPECTED_TEXT=${POCO_MODULE_OUTCOME_${outcome}}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/verify-outcome.cmake"
    )
    set_tests_properties("poco_generic_module_${outcome}" PROPERTIES
        TIMEOUT 10
    )
endforeach()
