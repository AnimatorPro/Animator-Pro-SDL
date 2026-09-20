include(PocoModule)

# Fixtures run from the build tree, never from an install prefix.  Collecting
# the modules and their scripts in one directory lets the loader resolve each
# module through the ordinary script-directory search.
set(POCO_MODULE_API_DIR "${CMAKE_CURRENT_BINARY_DIR}/modules")
file(MAKE_DIRECTORY "${POCO_MODULE_API_DIR}")

function(add_module_api_fixture TARGET SOURCE)
    poco_add_module(${TARGET} SOURCES ${SOURCE})
    set_target_properties(${TARGET} PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${POCO_MODULE_API_DIR}"
        RUNTIME_OUTPUT_DIRECTORY "${POCO_MODULE_API_DIR}"
    )
endfunction()

add_module_api_fixture(poco_generic_module generic.c)
add_module_api_fixture(poco_generic_bad_version bad_version.c)
add_module_api_fixture(poco_generic_empty empty.c)

# This intentionally has neither the generic entry point nor the legacy
# compatibility entry point.
add_module_api_fixture(poco_generic_missing_entry missing_entry.c)

foreach(script IN ITEMS generic missing_entry bad_version empty)
    configure_file("${script}.poc" "${POCO_MODULE_API_DIR}/${script}.poc" COPYONLY)
endforeach()

add_test(
    NAME poco_generic_module_valid
    COMMAND $<TARGET_FILE:poco_cli> "${POCO_MODULE_API_DIR}/generic.poc"
    WORKING_DIRECTORY "${POCO_MODULE_API_DIR}"
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
            "-DPOCO_EXECUTABLE=$<TARGET_FILE:poco_cli>"
            "-DPOCO_SCRIPT=${POCO_MODULE_API_DIR}/${outcome}.poc"
            "-DEXPECTED_TEXT=${POCO_MODULE_OUTCOME_${outcome}}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/verify-outcome.cmake"
    )
    set_tests_properties("poco_generic_module_${outcome}" PROPERTIES
        TIMEOUT 10
    )
endforeach()
