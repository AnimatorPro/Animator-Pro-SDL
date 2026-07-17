cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

# Poco can be configured as its own top-level source tree.  Keep Animator
# compatibility audits in the parent build, where the Animator source exists,
# so the copied-tree test suite remains independently runnable.
set(POCO_TEST_HAS_ANIMATOR_SOURCE FALSE)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/CMakeLists.txt")
    set(POCO_TEST_HAS_ANIMATOR_SOURCE TRUE)
endif()

# Function to add a positive test (expects successful execution)
# Usage: add_poco_pos_test(test_name relative_path_to_poc_file)
function(add_poco_pos_test TEST_NAME POC_FILE)
    set(TEST_FILE "${CMAKE_CURRENT_SOURCE_DIR}/${POC_FILE}")
    add_test(
        NAME "poco_pos_${TEST_NAME}"
        COMMAND $<TARGET_FILE:poco> "${TEST_FILE}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    # Positive tests should succeed (return 0)
    set_tests_properties("poco_pos_${TEST_NAME}" PROPERTIES
        PASS_REGULAR_EXPRESSION "Success|success|hi|Hello"
        TIMEOUT 10
    )
endfunction()

# Function to add a negative test (expects compilation/runtime error)
# Usage: add_poco_neg_test(test_name relative_path_to_poc_file expected_error_pattern)
function(add_poco_neg_test TEST_NAME POC_FILE)
    set(TEST_FILE "${CMAKE_CURRENT_SOURCE_DIR}/${POC_FILE}")
    add_test(
        NAME "poco_neg_${TEST_NAME}"
        COMMAND $<TARGET_FILE:poco> "${TEST_FILE}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    # Negative tests should fail (non-zero return code or error message)
    set_tests_properties("poco_neg_${TEST_NAME}" PROPERTIES
        WILL_FAIL TRUE
        TIMEOUT 10
    )
endfunction()

# Function to add a general poco test with custom properties
# Usage: add_poco_test(test_name relative_path_to_poc_file)
function(add_poco_test TEST_NAME POC_FILE)
    set(TEST_FILE "${CMAKE_CURRENT_SOURCE_DIR}/${POC_FILE}")
    add_test(
        NAME "poco_${TEST_NAME}"
        COMMAND $<TARGET_FILE:poco> "${TEST_FILE}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    set_tests_properties("poco_${TEST_NAME}" PROPERTIES
        TIMEOUT 10
    )
endfunction()


###############################################################################
# Positive Tests - These should compile and run successfully
###############################################################################

add_poco_pos_test(array_1d "pos/array_1d.poc")
add_poco_pos_test(array_2d "pos/array_2d.poc")
add_poco_pos_test(array_max_dims "pos/array_max_dims.poc")
add_poco_pos_test(char_constants "pos/char_constants.poc")
add_poco_pos_test(fnsplit_roundtrip "pos/fnsplit_roundtrip.poc")
add_poco_pos_test(for_nested "pos/for_nested.poc")
add_poco_pos_test(if_nested "pos/if_nested.poc")
add_poco_pos_test(lone_semicolons "pos/lone_semicolons.poc")
add_poco_pos_test(pointer_return "pos/pointer_return.poc")
add_poco_pos_test(sprintf_variadic "pos/sprintf_variadic.poc")
add_poco_pos_test(struct_nested "pos/struct_nested.poc")
add_poco_pos_test(struct_semicolons "pos/struct_semicolons.poc")
add_poco_pos_test(struct_union_enum "pos/struct_union_enum.poc")
add_poco_pos_test(token_long_idents "pos/token_long_idents.poc")


###############################################################################
# Negative Tests - These should fail to compile or produce errors
###############################################################################

add_poco_neg_test(addr_dot_ampersand "neg/addr_dot_ampersand.poc")
add_poco_neg_test(addr_dot_deref "neg/addr_dot_deref.poc")
add_poco_neg_test(addr_in_decl "neg/addr_in_decl.poc")
add_poco_neg_test(addr_of_addr "neg/addr_of_addr.poc")
add_poco_neg_test(array_as_number "neg/array_as_number.poc")
add_poco_neg_test(call_array_as_func "neg/call_array_as_func.poc")
add_poco_neg_test(call_int_as_func "neg/call_int_as_func.poc")
add_poco_neg_test(cast_ptr_to_func "neg/cast_ptr_to_func.poc")
add_poco_neg_test(char_in_param_list "neg/char_in_param_list.poc")
add_poco_neg_test(char_to_ptr "neg/char_to_ptr.poc")
add_poco_neg_test(dollar_in_name_01 "neg/dollar_in_name_01.poc")
add_poco_neg_test(dollar_in_name_02 "neg/dollar_in_name_02.poc")
add_poco_neg_test(dollar_in_name_03 "neg/dollar_in_name_03.poc")
add_poco_neg_test(dollar_in_name_04 "neg/dollar_in_name_04.poc")
add_poco_neg_test(else_no_body "neg/else_no_body.poc")
add_poco_neg_test(expect_bool_expr "neg/expect_bool_expr.poc")
add_poco_neg_test(func_as_number "neg/func_as_number.poc")
add_poco_neg_test(if_no_body "neg/if_no_body.poc")
add_poco_neg_test(param_count_0_of_1 "neg/param_count_0_of_1.poc")
add_poco_neg_test(param_count_1_of_2 "neg/param_count_1_of_2.poc")
add_poco_neg_test(param_count_2_of_3 "neg/param_count_2_of_3.poc")
add_poco_neg_test(param_count_3_of_4 "neg/param_count_3_of_4.poc")
add_poco_neg_test(param_count_4_of_5 "neg/param_count_4_of_5.poc")
add_poco_neg_test(stack_overflow "neg/stack_overflow.poc")
add_poco_neg_test(string_multiline_02 "neg/string_multiline_02.poc")
add_poco_neg_test(string_multiline_03 "neg/string_multiline_03.poc")
add_poco_neg_test(string_unterminated "neg/string_unterminated.poc")
add_poco_neg_test(struct_arrow_on_val "neg/struct_arrow_on_val.poc")
add_poco_neg_test(struct_bad_indirect "neg/struct_bad_indirect.poc")
add_poco_neg_test(struct_bad_member "neg/struct_bad_member.poc")
add_poco_neg_test(struct_as_var_name "neg/struct_as_var_name.poc")
add_poco_neg_test(struct_comma_semi "neg/struct_comma_semi.poc")
add_poco_neg_test(struct_dot_on_ptr "neg/struct_dot_on_ptr.poc")
add_poco_neg_test(struct_in_for_loop "neg/struct_in_for_loop.poc")
add_poco_neg_test(struct_in_parens "neg/struct_in_parens.poc")
add_poco_neg_test(struct_keyword_dup "neg/struct_keyword_dup.poc")
add_poco_neg_test(struct_keyword_name "neg/struct_keyword_name.poc")
add_poco_neg_test(struct_member_keyword "neg/struct_member_keyword.poc")
add_poco_neg_test(struct_missing_brace "neg/struct_missing_brace.poc")
add_poco_neg_test(struct_missing_keyword "neg/struct_missing_keyword.poc")
add_poco_neg_test(struct_missing_semi "neg/struct_missing_semi.poc")
add_poco_neg_test(token_empty_file "neg/token_empty_file.poc")
add_poco_neg_test(token_main_only "neg/token_main_only.poc")
add_poco_neg_test(token_no_main "neg/token_no_main.poc")
add_poco_neg_test(token_only_comment "neg/token_only_comment.poc")
add_poco_neg_test(type_funcptr_mismatch "neg/type_funcptr_mismatch.poc")
add_poco_neg_test(type_funcptr_params "neg/type_funcptr_params.poc")
add_poco_neg_test(undefined_var "neg/undefined_var.poc")
add_poco_neg_test(void_return_value "neg/void_return_value.poc")

# Library loading error tests - install test files first
INSTALL(FILES 
    ${CMAKE_CURRENT_SOURCE_DIR}/neg/nlib01.poc
    ${CMAKE_CURRENT_SOURCE_DIR}/neg/nlib02.poc
    ${CMAKE_CURRENT_SOURCE_DIR}/neg/nlib03.poc
    ${CMAKE_CURRENT_SOURCE_DIR}/neg/nlib04.poc
    ${CMAKE_CURRENT_SOURCE_DIR}/neg/nlib05.poc
    DESTINATION tests
)

# Define tests using installed files
add_test(
    NAME "poco_neg_lib01_not_found"
    COMMAND ${CMAKE_INSTALL_PREFIX}/poco ${CMAKE_INSTALL_PREFIX}/tests/nlib01.poc
    WORKING_DIRECTORY "${CMAKE_INSTALL_PREFIX}"
)
set_tests_properties("poco_neg_lib01_not_found" PROPERTIES
    WILL_FAIL TRUE
    TIMEOUT 10
)

add_test(
    NAME "poco_neg_lib02_no_entry"
    COMMAND ${CMAKE_INSTALL_PREFIX}/poco ${CMAKE_INSTALL_PREFIX}/tests/nlib02.poc
    WORKING_DIRECTORY "${CMAKE_INSTALL_PREFIX}"
)
set_tests_properties("poco_neg_lib02_no_entry" PROPERTIES
    WILL_FAIL TRUE
    TIMEOUT 10
    DEPENDS test_noentry
)

add_test(
    NAME "poco_neg_lib03_null_ret"
    COMMAND ${CMAKE_INSTALL_PREFIX}/poco ${CMAKE_INSTALL_PREFIX}/tests/nlib03.poc
    WORKING_DIRECTORY "${CMAKE_INSTALL_PREFIX}"
)
set_tests_properties("poco_neg_lib03_null_ret" PROPERTIES
    WILL_FAIL TRUE
    TIMEOUT 10
    DEPENDS test_nullret
)

add_test(
    NAME "poco_neg_lib04_bad_ver"
    COMMAND ${CMAKE_INSTALL_PREFIX}/poco ${CMAKE_INSTALL_PREFIX}/tests/nlib04.poc
    WORKING_DIRECTORY "${CMAKE_INSTALL_PREFIX}"
)
set_tests_properties("poco_neg_lib04_bad_ver" PROPERTIES
    WILL_FAIL TRUE
    TIMEOUT 10
    DEPENDS test_badver
)

add_test(
    NAME "poco_neg_lib05_empty"
    COMMAND ${CMAKE_INSTALL_PREFIX}/poco ${CMAKE_INSTALL_PREFIX}/tests/nlib05.poc
    WORKING_DIRECTORY "${CMAKE_INSTALL_PREFIX}"
)
set_tests_properties("poco_neg_lib05_empty" PROPERTIES
    WILL_FAIL TRUE
    TIMEOUT 10
    DEPENDS test_empty
)


###############################################################################
# General Tests - Various poco scripts in the root test directory
###############################################################################

add_poco_test(hello "hello.poc")
add_poco_test(array "array.poc")
add_poco_test(ffi_01 "po_ffi_01.poc")

# The libffi descriptor builder must reject malformed compiler frames without
# partially populating either lookup map, and the runtime must release those
# descriptors on every program teardown.
add_executable(poco_ffi_hardening ffi_hardening/ffi_hardening.c)
target_compile_definitions(poco_ffi_hardening PRIVATE
    POCO_FFI_HARDENING_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/ffi_hardening"
)
target_include_directories(poco_ffi_hardening PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)
target_link_libraries(poco_ffi_hardening PRIVATE Poco::poco poco_ffi)
add_test(NAME poco_ffi_hardening COMMAND $<TARGET_FILE:poco_ffi_hardening>)
set_tests_properties(poco_ffi_hardening PROPERTIES TIMEOUT 10)

# End-to-end ABI regression for the public extension boundary.  The fixture
# combines fixed return/call shapes with mixed dynamic varargs, then verifies
# descriptor rejection and cleanup through repeated VM lifecycles.
add_executable(poco_extension_boundary extension_boundary/extension_boundary.c)
target_compile_definitions(poco_extension_boundary PRIVATE
    POCO_EXTENSION_BOUNDARY_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/extension_boundary"
)
target_include_directories(poco_extension_boundary PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)
target_link_libraries(poco_extension_boundary PRIVATE Poco::poco poco_ffi)
add_test(NAME poco_extension_boundary COMMAND $<TARGET_FILE:poco_extension_boundary>)
set_tests_properties(poco_extension_boundary PROPERTIES TIMEOUT 10)

# Contracted native bindings retain Poco pointer spans until the final libffi
# conversion.  This host fixture exercises the public contract and borrowed
# span APIs without depending on compiler-private headers.
add_executable(poco_ffi_bounds ffi_bounds/ffi_bounds.c)
target_compile_definitions(poco_ffi_bounds PRIVATE
    POCO_FFI_BOUNDS_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/ffi_bounds"
)
target_link_libraries(poco_ffi_bounds PRIVATE Poco::poco)
add_test(NAME poco_ffi_bounds COMMAND $<TARGET_FILE:poco_ffi_bounds>)
set_tests_properties(poco_ffi_bounds PROPERTIES TIMEOUT 10)

# Native host callbacks enter the Poco interpreter with explicitly typed
# values.  This fixture covers every value shape used by the Animator callback
# sites and rejects malformed callback values before execution.
add_executable(poco_callback_invoke callback_invoke/callback_invoke.c)
target_compile_definitions(poco_callback_invoke PRIVATE
    POCO_CALLBACK_INVOKE_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/callback_invoke"
)
target_link_libraries(poco_callback_invoke PRIVATE Poco::poco)
add_test(NAME poco_callback_invoke COMMAND $<TARGET_FILE:poco_callback_invoke>)
set_tests_properties(poco_callback_invoke PROPERTIES TIMEOUT 10)

# The public embedding API must let a small C host supply libraries without
# including Poco compiler control-block headers.  The executable covers the
# lifecycle rather than spawning the CLI, so it exercises the API boundary
# directly.
add_executable(poco_public_api public_api/host_api.c)
target_compile_definitions(poco_public_api PRIVATE
    POCO_PUBLIC_API_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/public_api"
)
target_link_libraries(poco_public_api PRIVATE Poco::poco)
add_test(NAME poco_public_api COMMAND $<TARGET_FILE:poco_public_api>)
set_tests_properties(poco_public_api PROPERTIES TIMEOUT 10)

# The explicit standard catalog is an embedding-host feature, not the CLI's
# legacy table.  This fixture covers the real host-neutral console, string,
# memory, file, math, and path bindings, including cleanup of intentionally
# unclosed file handles after both a successful and failed program run.
add_executable(poco_standard_catalog standard_catalog/catalog.c)
target_compile_definitions(poco_standard_catalog PRIVATE
    POCO_STANDARD_CATALOG_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/standard_catalog"
)
target_link_libraries(poco_standard_catalog PRIVATE Poco::poco)
add_test(NAME poco_standard_catalog COMMAND $<TARGET_FILE:poco_standard_catalog>)
set_tests_properties(poco_standard_catalog PROPERTIES
    TIMEOUT 10
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)

# Poco owns its portability runtime.  This host intentionally exports
# incompatible Animator-style pj_* helpers; the embedded VM must still compile,
# run, and free a program without invoking them.
add_executable(poco_port_runtime port_runtime/host_collision.c)
target_compile_definitions(poco_port_runtime PRIVATE
    POCO_PORT_RUNTIME_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/port_runtime"
)
target_link_libraries(poco_port_runtime PRIVATE Poco::poco)
add_test(NAME poco_port_runtime COMMAND $<TARGET_FILE:poco_port_runtime>)
set_tests_properties(poco_port_runtime PROPERTIES TIMEOUT 10)

# Path operations are owned by Poco and must preserve the legacy script-facing
# split/merge behavior without relying on Animator file libraries.
add_executable(poco_path_operations path_operations.c)
target_include_directories(poco_path_operations PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)
target_link_libraries(poco_path_operations PRIVATE Poco::poco)
add_test(NAME poco_path_operations COMMAND $<TARGET_FILE:poco_path_operations>)
set_tests_properties(poco_path_operations PROPERTIES TIMEOUT 10)

if(POCO_TEST_HAS_ANIMATOR_SOURCE)
    # Animator still consumes the legacy compile/run and POE declarations while
    # it migrates to the embedding API.  Keep that compatibility boundary
    # compiling against the canonical Poco ABI instead of maintaining duplicate
    # layouts.
    add_executable(poco_legacy_animator_headers legacy_animator/headers.c)
    target_include_directories(poco_legacy_animator_headers BEFORE PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/inc"
    )
    target_link_libraries(poco_legacy_animator_headers PRIVATE Poco::poco)
    add_test(NAME poco_legacy_animator_headers COMMAND $<TARGET_FILE:poco_legacy_animator_headers>)
    set_tests_properties(poco_legacy_animator_headers PROPERTIES TIMEOUT 10)

    add_test(
        NAME poco_legacy_header_shims
        COMMAND "${CMAKE_COMMAND}"
            "-DPOCO_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/.."
            "-DANIMATOR_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/../.."
            -P "${CMAKE_CURRENT_SOURCE_DIR}/verify-legacy-header-shims.cmake"
    )
    set_tests_properties(poco_legacy_header_shims PROPERTIES TIMEOUT 10)
endif()

# Generic modules use only the canonical public header and Poco::poco.  The
# fixture validates the successful load path and the module-loader outcomes
# that a host can diagnose without the legacy Animator POE ABI.
add_subdirectory(module_api)

# Module lifecycle policy is configured by an embedding host, never inferred
# from a module's dependencies.  These fixtures cover success, host rejection,
# and cleanup after a module fails validation.
add_subdirectory(module_hooks)

# Legacy POE modules use Animator's native function-table ABI.  This fixture
# proves the generic loader rejects that ABI by default and accepts it only
# after an embedding host explicitly opts into the compatibility policy.
add_subdirectory(legacy_poe_fence)

# The canonical embedding header must compile for C and C++ consumers using
# only Poco's installed include directory.  This deliberately builds no Poco
# source or targets, so it catches an accidental internal-header dependency.
add_test(
    NAME "poco_public_header"
    COMMAND "${CMAKE_COMMAND}"
        "-DPOCO_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/.."
        "-DTEST_GENERATOR=${CMAKE_GENERATOR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/verify-public-header.cmake"
)
set_tests_properties("poco_public_header" PROPERTIES TIMEOUT 60)

if(POCO_TEST_HAS_ANIMATOR_SOURCE)
    # Characterization gate: keep the pre-refactor registration and callback
    # inventory reviewable as the binding ownership changes in later tasks.
    add_test(
        NAME "poco_binding_baseline"
        COMMAND "${CMAKE_COMMAND}"
            "-DPOCO_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/.."
            "-DANIMATOR_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/../.."
            "-DMANIFEST=${CMAKE_CURRENT_SOURCE_DIR}/binding-baseline.json"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/verify-binding-baseline.cmake"
    )
    set_tests_properties("poco_binding_baseline" PROPERTIES TIMEOUT 10)
endif()

# Standalone Poco must not compile the legacy dummy catalog or advertise
# Animator-only APIs.  The fixture verifies both the source graph and the
# diagnostic produced when a script asks for one of those unavailable APIs.
add_test(
    NAME "poco_binding_cleanup"
    COMMAND "${CMAKE_COMMAND}"
        "-DPOCO_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/.."
        "-DPOCO_EXECUTABLE=$<TARGET_FILE:poco>"
        "-DUNAVAILABLE_API_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/neg/animator_api_unavailable.poc"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/verify-binding-cleanup.cmake"
)
set_tests_properties("poco_binding_cleanup" PROPERTIES TIMEOUT 20)

if(POCO_TEST_HAS_ANIMATOR_SOURCE)
    # Every active C-varargs binding must use the result type declared to the
    # Poco compiler.  The script also runs a mixed integer/floating/null/string
    # /pointer call through the standalone registry so default promotions reach
    # libffi with their native C representation.
    add_test(
        NAME "poco_live_variadic_bindings"
        COMMAND "${CMAKE_COMMAND}"
            "-DPOCO_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/.."
            "-DANIMATOR_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/../.."
            "-DPOCO_EXECUTABLE=$<TARGET_FILE:poco>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/verify-live-variadic-bindings.cmake"
    )
    set_tests_properties("poco_live_variadic_bindings" PROPERTIES TIMEOUT 20)
endif()

if(POCO_TEST_HAS_ANIMATOR_SOURCE)
    # Current module builds use the generic and Animator-specific CMake helpers.
    # Keep the retired Rex assembly inputs and DOS-era Poekit packaging metadata
    # out of all supported build paths.
    add_test(
        NAME "poco_legacy_artifact_cleanup"
        COMMAND "${CMAKE_COMMAND}"
            "-DPOCO_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/.."
            -P "${CMAKE_CURRENT_SOURCE_DIR}/verify-legacy-artifact-cleanup.cmake"
    )
    set_tests_properties("poco_legacy_artifact_cleanup" PROPERTIES TIMEOUT 20)
endif()

# Extraction gate: the copied standalone tree configures, builds, installs,
# and runs its own tests, then verifies source and package consumers.  It is
# intentionally opt-in because that full independent build is expensive.
if(POCO_ENABLE_EXTRACTION_BOUNDARY_TEST)
    add_test(
        NAME "poco_extraction_boundary"
        COMMAND "${CMAKE_COMMAND}"
            "-DPOCO_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/.."
            "-DTEST_GENERATOR=${CMAKE_GENERATOR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/verify-extraction-boundary.cmake"
    )
    set_tests_properties("poco_extraction_boundary" PROPERTIES TIMEOUT 300)
endif()

# This is deliberately separate from the copied-tree gate: it confirms the
# current Animator-integrated target still runs a simple Poco script.
add_test(
    NAME "poco_animator_integration_control"
    COMMAND "${CMAKE_COMMAND}"
        "-DPOCO_EXECUTABLE=$<TARGET_FILE:poco>"
        "-DPOCO_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/hello.poc"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/verify-animator-integration.cmake"
)
set_tests_properties("poco_animator_integration_control" PROPERTIES TIMEOUT 30)

# The generic hello module test is registered in
# poco/poekit/hello/CMakeLists.txt as "poco_generic_hello".  It remains
# distinct from this directory's historical hello script smoke test.

# Add more general tests as needed:
# add_poco_test(atof "ATOF.POC")
# add_poco_test(comma "COMMA.POC")
# add_poco_test(float "FLOAT.POC")
# add_poco_test(forever "FOREVER.POC")
# add_poco_test(if "IF.POC")
# add_poco_test(short "SHORT.POC")
# add_poco_test(struct "STRUCT.POC")
# etc...
