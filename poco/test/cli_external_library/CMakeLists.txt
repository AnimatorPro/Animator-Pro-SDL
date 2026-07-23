include(PocoModule)

# Reuse the canonical poekit hello implementation without making the
# standalone Poco test tree depend on the Animator-owned poekit build target.
poco_add_module(poco_cli_external_hello
    SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/../../poekit/hello/hello.c")
set_target_properties(poco_cli_external_hello PROPERTIES OUTPUT_NAME hello)

add_test(
    NAME poco_cli_external_library_roundtrip
    COMMAND "${CMAKE_COMMAND}"
        "-DPOCO_EXECUTABLE=$<TARGET_FILE:poco>"
        "-DPOCO_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/../../poekit/hello/hello.poc"
        "-DPOCO_GOOD_LIBRARY=$<TARGET_FILE:poco_cli_external_hello>"
        "-DPOCO_BAD_ABI_LIBRARY=$<TARGET_FILE:badabi____fixture>"
        "-DPOCO_MISSING_SYMBOL_LIBRARY=$<TARGET_FILE:misssym___fixture>"
        "-DPOCO_PUBLIC_HEADER=${CMAKE_CURRENT_SOURCE_DIR}/../../include/poco/poco.h"
        "-DPOCO_FIXTURE_DIR=${CMAKE_CURRENT_BINARY_DIR}/roundtrip"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/verify-external-library-roundtrip.cmake"
)
set_tests_properties(poco_cli_external_library_roundtrip PROPERTIES TIMEOUT 20)
