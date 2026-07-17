add_library(poco_legacy_poe_fixture MODULE legacy_poe_fixture.c)
target_link_libraries(poco_legacy_poe_fixture PRIVATE Poco::poco)
set_target_properties(poco_legacy_poe_fixture PROPERTIES
    PREFIX ""
    SUFFIX ".poe"
    POSITION_INDEPENDENT_CODE ON
)

configure_file(legacy_poe_fixture.poc legacy_poe_fixture.poc COPYONLY)

add_executable(poco_legacy_poe_fence legacy_poe_fence.c)
target_compile_definitions(poco_legacy_poe_fence PRIVATE
    POCO_LEGACY_POE_FENCE_FIXTURE_DIR="${CMAKE_CURRENT_BINARY_DIR}"
)
target_link_libraries(poco_legacy_poe_fence PRIVATE Poco::poco)
add_dependencies(poco_legacy_poe_fence poco_legacy_poe_fixture)
add_test(NAME poco_legacy_poe_fence COMMAND $<TARGET_FILE:poco_legacy_poe_fence>)
set_tests_properties(poco_legacy_poe_fence PROPERTIES TIMEOUT 10)

if(POCO_TEST_HAS_ANIMATOR_SOURCE)
    add_test(
        NAME poco_legacy_poe_fence_source
        COMMAND "${CMAKE_COMMAND}"
            "-DPOCO_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/../.."
            "-DANIMATOR_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/../../.."
            -P "${CMAKE_CURRENT_SOURCE_DIR}/verify-legacy-poe-fence.cmake"
    )
    set_tests_properties(poco_legacy_poe_fence_source PROPERTIES TIMEOUT 10)
endif()
