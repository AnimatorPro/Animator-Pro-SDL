cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

find_package(Threads REQUIRED)

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

# Sanitizer builds on macOS share a fixture that explicitly builds every
# registered concurrency target before any of them run.  The aggregate target
# begins empty so this scaffold is usable before the first characterization
# executable lands.
if(APPLE AND (USE_ASAN OR USE_TSAN))
    if(USE_ASAN)
        set(POCO_CONCURRENCY_SANITIZER "asan")
    else()
        set(POCO_CONCURRENCY_SANITIZER "tsan")
    endif()
    set(POCO_CONCURRENCY_FIXTURE
        "poco_concurrency_${POCO_CONCURRENCY_SANITIZER}")

    add_custom_target(poco_concurrency_tests)
    add_test(
        NAME "${POCO_CONCURRENCY_FIXTURE}_build"
        COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}"
            --target poco_concurrency_tests
    )
    set_tests_properties("${POCO_CONCURRENCY_FIXTURE}_build" PROPERTIES
        FIXTURES_SETUP "${POCO_CONCURRENCY_FIXTURE}"
        LABELS "poco_concurrency;${POCO_CONCURRENCY_SANITIZER}"
        TIMEOUT 120
    )
endif()

# Register a concurrency executable with a stable CTest label.  Configuring
# once with USE_ASAN and once with USE_TSAN exercises the same binaries under
# both runtimes without changing how the rest of the ASAN suite is registered.
function(add_poco_concurrency_test TEST_NAME TARGET_NAME)
    if(NOT TARGET "${TARGET_NAME}")
        message(FATAL_ERROR
            "Concurrency test ${TEST_NAME} references missing target ${TARGET_NAME}")
    endif()

    add_test(NAME "${TEST_NAME}" COMMAND $<TARGET_FILE:${TARGET_NAME}>)
    set_tests_properties("${TEST_NAME}" PROPERTIES
        LABELS "poco_concurrency"
        TIMEOUT 20
    )

    if(APPLE AND (USE_ASAN OR USE_TSAN))
        add_dependencies(poco_concurrency_tests "${TARGET_NAME}")
        set_tests_properties("${TEST_NAME}" PROPERTIES
            FIXTURES_REQUIRED "${POCO_CONCURRENCY_FIXTURE}"
        )
    endif()
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

# The standalone compiler writes the canonical source-less program image.
# Exercise explicit compile/output, output-implies-compile, and syntax-check
# modes through the installed CLI surface.
add_test(
    NAME poco_cli_compile_to_binary
    COMMAND "${CMAKE_COMMAND}"
        "-DPOCO_EXECUTABLE=$<TARGET_FILE:poco>"
        "-DPOCO_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/cli_compile/compile_to_binary.poc"
        "-DPOCO_INVALID_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/cli_compile/invalid.poc"
        "-DPOCO_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/cli_compile/compile_to_binary.pex"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cli_compile/verify-compile-to-binary.cmake"
)
set_tests_properties(poco_cli_compile_to_binary PROPERTIES TIMEOUT 10)

# Input type is determined from the full eight-byte container magic rather
# than the filename.  Exercise source/binary output parity, renamed inputs, and
# each public image-validation failure through the installed CLI surface.
add_test(
    NAME poco_cli_detect_and_run_binary
    COMMAND "${CMAKE_COMMAND}"
        "-DPOCO_EXECUTABLE=$<TARGET_FILE:poco>"
        "-DPOCO_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/cli_binary/detect_and_run.poc"
        "-DPOCO_FIXTURE_DIR=${CMAKE_CURRENT_BINARY_DIR}/cli_binary"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cli_binary/verify-detect-and-run.cmake"
)
set_tests_properties(poco_cli_detect_and_run_binary PROPERTIES TIMEOUT 10)

# The internal serialization hash helper is the single entry point for source
# addresses and image integrity. Check published BLAKE3 vectors, including a
# multi-chunk input that exercises Apple ARM's NEON hash_many path.
add_executable(poco_blake3_vectors blake3_vectors/blake3_vectors.c)
target_include_directories(poco_blake3_vectors PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)
target_link_libraries(poco_blake3_vectors PRIVATE Poco::poco)
add_test(NAME poco_blake3_vectors COMMAND $<TARGET_FILE:poco_blake3_vectors>)
set_tests_properties(poco_blake3_vectors PROPERTIES TIMEOUT 10)

# On Apple ARM, run the identical vectors through a separately compiled
# portable-only reference target. Both tests asserting the same published
# digest is a direct dispatch-versus-portable agreement gate.
if(POCO_BLAKE3_HAS_NEON)
    add_executable(poco_blake3_vectors_portable
        blake3_vectors/blake3_vectors.c
        ../third_party/blake3/blake3.c
        ../third_party/blake3/blake3_dispatch.c
        ../third_party/blake3/blake3_portable.c
    )
    target_compile_definitions(poco_blake3_vectors_portable PRIVATE
        POCO_BLAKE3_PORTABLE_FIXTURE=1
        BLAKE3_USE_NEON=0
        BLAKE3_NO_SSE2
        BLAKE3_NO_SSE41
        BLAKE3_NO_AVX2
        BLAKE3_NO_AVX512
    )
    target_include_directories(poco_blake3_vectors_portable PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/blake3"
    )
    set_target_properties(poco_blake3_vectors_portable PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO
    )
    add_test(NAME poco_blake3_vectors_portable
        COMMAND $<TARGET_FILE:poco_blake3_vectors_portable>
    )
    set_tests_properties(poco_blake3_vectors_portable PROPERTIES TIMEOUT 10)
endif()

# The bytecode envelope is payload-agnostic framing around an opaque image.
# Gate its exact canonical bytes, strict format-version match, and both BLAKE3
# hash paths before the program-image codec begins consuming it.
add_executable(poco_bytecode_envelope
    bytecode_envelope/bytecode_envelope.c
)
target_include_directories(poco_bytecode_envelope PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)
target_link_libraries(poco_bytecode_envelope PRIVATE Poco::poco)
add_test(NAME poco_bytecode_envelope
    COMMAND $<TARGET_FILE:poco_bytecode_envelope>
)
set_tests_properties(poco_bytecode_envelope PROPERTIES TIMEOUT 10)

# The fixed envelope header exposes the selected debug tier without decoding
# the program image. Minimal archives retain name/hash/line mappings but omit
# the physical path; extended archives round-trip that path for the debugger.
add_executable(poco_debug_metadata
    debug_metadata/debug_metadata.c
)
target_compile_definitions(poco_debug_metadata PRIVATE
    POCO_DEBUG_METADATA_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/debug_metadata/debug_metadata.poc"
)
target_include_directories(poco_debug_metadata PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)
target_link_libraries(poco_debug_metadata PRIVATE Poco::poco poco_ffi)
add_test(NAME poco_debug_metadata COMMAND $<TARGET_FILE:poco_debug_metadata>)
set_tests_properties(poco_debug_metadata PROPERTIES TIMEOUT 10)

add_executable(poco_multi_unit_compile
    multi_unit_compile/multi_unit_compile.c
)
target_compile_definitions(poco_multi_unit_compile PRIVATE
    POCO_MULTI_UNIT_0="${CMAKE_CURRENT_SOURCE_DIR}/multi_unit_compile/unit0.poc"
    POCO_MULTI_UNIT_1="${CMAKE_CURRENT_SOURCE_DIR}/multi_unit_compile/unit1.poc"
    POCO_MULTI_UNIT_INVALID="${CMAKE_CURRENT_SOURCE_DIR}/multi_unit_compile/invalid.poc"
    POCO_MULTI_UNIT_LINK_0="${CMAKE_CURRENT_SOURCE_DIR}/multi_unit_compile/link0.poc"
    POCO_MULTI_UNIT_LINK_1="${CMAKE_CURRENT_SOURCE_DIR}/multi_unit_compile/link1.poc"
    POCO_MULTI_UNIT_DUP_FUNCTION_0="${CMAKE_CURRENT_SOURCE_DIR}/multi_unit_compile/duplicate_function0.poc"
    POCO_MULTI_UNIT_DUP_FUNCTION_1="${CMAKE_CURRENT_SOURCE_DIR}/multi_unit_compile/duplicate_function1.poc"
    POCO_MULTI_UNIT_DUP_GLOBAL_0="${CMAKE_CURRENT_SOURCE_DIR}/multi_unit_compile/duplicate_global0.poc"
    POCO_MULTI_UNIT_DUP_GLOBAL_1="${CMAKE_CURRENT_SOURCE_DIR}/multi_unit_compile/duplicate_global1.poc"
    POCO_MULTI_UNIT_DUP_KIND_0="${CMAKE_CURRENT_SOURCE_DIR}/multi_unit_compile/duplicate_kind0.poc"
    POCO_MULTI_UNIT_DUP_KIND_1="${CMAKE_CURRENT_SOURCE_DIR}/multi_unit_compile/duplicate_kind1.poc"
    POCO_MULTI_UNIT_MAIN_0="${CMAKE_CURRENT_SOURCE_DIR}/multi_unit_compile/multiple_main0.poc"
    POCO_MULTI_UNIT_MAIN_1="${CMAKE_CURRENT_SOURCE_DIR}/multi_unit_compile/multiple_main1.poc"
    POCO_MULTI_UNIT_MISSING_PROTO_0="${CMAKE_CURRENT_SOURCE_DIR}/multi_unit_compile/missing_prototype0.poc"
    POCO_MULTI_UNIT_MISSING_PROTO_1="${CMAKE_CURRENT_SOURCE_DIR}/multi_unit_compile/missing_prototype1.poc"
    POCO_MULTI_UNIT_USE_LEAF="${CMAKE_CURRENT_SOURCE_DIR}/multi_unit_compile/use_leaf.poc"
    POCO_MULTI_UNIT_USE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/multi_unit_compile/use_root.poc"
    POCO_MULTI_UNIT_USE_MATCH="${CMAKE_CURRENT_SOURCE_DIR}/multi_unit_compile/use_matching_root.poc"
    POCO_MULTI_UNIT_USE_MISMATCH="${CMAKE_CURRENT_SOURCE_DIR}/multi_unit_compile/use_mismatch_root.poc"
    POCO_MULTI_UNIT_USE_HIDDEN="${CMAKE_CURRENT_SOURCE_DIR}/multi_unit_compile/use_hidden_root.poc"
    POCO_MULTI_UNIT_USE_CYCLE="${CMAKE_CURRENT_SOURCE_DIR}/multi_unit_compile/use_cycle_a.poc"
    POCO_MULTI_UNIT_USE_HOST_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/multi_unit_compile/use_host_root.poc"
    POCO_MULTI_UNIT_USE_HOST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/multi_unit_compile/use_include/"
)
target_include_directories(poco_multi_unit_compile PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_link_libraries(poco_multi_unit_compile PRIVATE Poco::poco poco_ffi)
add_test(NAME poco_multi_unit_compile COMMAND $<TARGET_FILE:poco_multi_unit_compile>)
set_tests_properties(poco_multi_unit_compile PROPERTIES
    LABELS "poco_multi_source"
    TIMEOUT 10
)

add_test(
    NAME poco_pragma_use_cli
    COMMAND $<TARGET_FILE:poco>
        "${CMAKE_CURRENT_SOURCE_DIR}/multi_unit_compile/use_root.poc"
)
set_tests_properties(poco_pragma_use_cli PROPERTIES
    LABELS "poco_multi_source"
    PASS_REGULAR_EXPRESSION "Return value: 15"
    TIMEOUT 10
)

# End-to-end CLI coverage for the complete multi-source linking contract.  The
# API-level test above checks the linker internals; this matrix keeps the user
# surfaces (listed sources, pragma use, persisted images, and the debugger) in
# lockstep with those semantics.
set(POCO_LINKING_FIXTURES "${CMAKE_CURRENT_SOURCE_DIR}/linking_matrix")

add_test(
    NAME poco_linking_compile_header
    COMMAND "${CMAKE_COMMAND}"
        "-DPOCO_EXECUTABLE=$<TARGET_FILE:poco>"
        "-DPOCO_MAIN=${POCO_LINKING_FIXTURES}/listed_main.poc"
        "-DPOCO_LIBRARY=${POCO_LINKING_FIXTURES}/library.poc"
        "-DPOCO_FIXTURE_DIR=${CMAKE_CURRENT_BINARY_DIR}/linking_matrix/compile"
        -P "${POCO_LINKING_FIXTURES}/verify-compiled-list.cmake"
)

add_test(
    NAME poco_linking_direct_list
    COMMAND $<TARGET_FILE:poco>
        "${POCO_LINKING_FIXTURES}/listed_main.poc"
        "${POCO_LINKING_FIXTURES}/library.poc"
)
set_tests_properties(poco_linking_direct_list PROPERTIES
    PASS_REGULAR_EXPRESSION "Return value: 42"
)

add_test(
    NAME poco_linking_use_no_header
    COMMAND $<TARGET_FILE:poco>
        "${POCO_LINKING_FIXTURES}/use_no_header.poc"
)
set_tests_properties(poco_linking_use_no_header PROPERTIES
    PASS_REGULAR_EXPRESSION "Return value: 42"
)

add_test(
    NAME poco_linking_use_matching_header
    COMMAND $<TARGET_FILE:poco>
        "${POCO_LINKING_FIXTURES}/use_matching_header.poc"
)
set_tests_properties(poco_linking_use_matching_header PROPERTIES
    PASS_REGULAR_EXPRESSION "Return value: 42"
)

add_test(
    NAME poco_linking_use_mismatched_header
    COMMAND $<TARGET_FILE:poco>
        "${POCO_LINKING_FIXTURES}/use_mismatched_header.poc"
)
set_tests_properties(poco_linking_use_mismatched_header PROPERTIES WILL_FAIL TRUE)

add_test(
    NAME poco_linking_duplicate_external
    COMMAND $<TARGET_FILE:poco>
        "${POCO_LINKING_FIXTURES}/duplicate_a.poc"
        "${POCO_LINKING_FIXTURES}/duplicate_b.poc"
)
set_tests_properties(poco_linking_duplicate_external PROPERTIES WILL_FAIL TRUE)

add_test(
    NAME poco_linking_same_named_static
    COMMAND $<TARGET_FILE:poco>
        "${POCO_LINKING_FIXTURES}/static_main.poc"
        "${POCO_LINKING_FIXTURES}/static_library.poc"
)
set_tests_properties(poco_linking_same_named_static PROPERTIES
    PASS_REGULAR_EXPRESSION "Return value: 42"
)

add_test(
    NAME poco_linking_zero_main_run
    COMMAND $<TARGET_FILE:poco>
        "${POCO_LINKING_FIXTURES}/library_only_a.poc"
        "${POCO_LINKING_FIXTURES}/library_only_b.poc"
)
set_tests_properties(poco_linking_zero_main_run PROPERTIES WILL_FAIL TRUE)

add_test(
    NAME poco_linking_library_check
    COMMAND $<TARGET_FILE:poco> -c
        "${POCO_LINKING_FIXTURES}/library_only_a.poc"
        "${POCO_LINKING_FIXTURES}/library_only_b.poc"
)

add_test(
    NAME poco_linking_multiple_main
    COMMAND $<TARGET_FILE:poco>
        "${POCO_LINKING_FIXTURES}/main_a.poc"
        "${POCO_LINKING_FIXTURES}/main_b.poc"
)
set_tests_properties(poco_linking_multiple_main PROPERTIES WILL_FAIL TRUE)

add_test(
    NAME poco_linking_multifile_debugger
    COMMAND "${CMAKE_COMMAND}"
        "-DPOCO_EXECUTABLE=$<TARGET_FILE:poco>"
        "-DPOCO_MAIN=${POCO_LINKING_FIXTURES}/debug_main.poc"
        "-DPOCO_LIBRARY=${POCO_LINKING_FIXTURES}/debug_library.poc"
        "-DPOCO_HEADER=${POCO_LINKING_FIXTURES}/debug_library.h"
        "-DPOCO_COMMANDS=${POCO_LINKING_FIXTURES}/debug-commands.txt"
        "-DPOCO_FIXTURE_DIR=${CMAKE_CURRENT_BINARY_DIR}/linking_matrix/debugger"
        -P "${POCO_LINKING_FIXTURES}/verify-multifile-debugger.cmake"
)

add_test(
    NAME poco_linking_use_cycle
    COMMAND $<TARGET_FILE:poco>
        "${POCO_LINKING_FIXTURES}/cycle_a.poc"
)
set_tests_properties(poco_linking_use_cycle PROPERTIES WILL_FAIL TRUE)

add_test(
    NAME poco_linking_use_list_dedup
    COMMAND $<TARGET_FILE:poco>
        "${POCO_LINKING_FIXTURES}/dedup_root.poc"
        "${POCO_LINKING_FIXTURES}/dedup_library.poc"
)
set_tests_properties(poco_linking_use_list_dedup PROPERTIES
    PASS_REGULAR_EXPRESSION "Return value: 42"
)

set_tests_properties(
    poco_linking_compile_header
    poco_linking_direct_list
    poco_linking_use_no_header
    poco_linking_use_matching_header
    poco_linking_use_mismatched_header
    poco_linking_duplicate_external
    poco_linking_same_named_static
    poco_linking_zero_main_run
    poco_linking_library_check
    poco_linking_multiple_main
    poco_linking_multifile_debugger
    poco_linking_use_cycle
    poco_linking_use_list_dedup
    PROPERTIES
        LABELS "poco_linking;poco_multi_source"
        TIMEOUT 15
)

# Debug sessions stop synchronously before a mapped line's first opcode while
# detached and breakpoint-free runs retain the ordinary result/output stream.
add_executable(poco_debug_breakpoints
    debug_breakpoints/debug_breakpoints.c
)
target_compile_definitions(poco_debug_breakpoints PRIVATE
    POCO_DEBUG_BREAKPOINT_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/debug_breakpoints/debug_breakpoints.poc"
)
target_link_libraries(poco_debug_breakpoints PRIVATE Poco::poco)
set_target_properties(poco_debug_breakpoints PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED YES
)
add_test(NAME poco_debug_breakpoints COMMAND $<TARGET_FILE:poco_debug_breakpoints>)
set_tests_properties(poco_debug_breakpoints PROPERTIES TIMEOUT 10)

# Step-into follows nested Poco calls, next-over waits for the starting call
# depth, and continue resumes ordinary execution until a breakpoint or exit.
add_executable(poco_debug_stepping
    debug_stepping/debug_stepping.c
)
target_compile_definitions(poco_debug_stepping PRIVATE
    POCO_DEBUG_STEPPING_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/debug_stepping/debug_stepping.poc"
)
target_link_libraries(poco_debug_stepping PRIVATE Poco::poco)
set_target_properties(poco_debug_stepping PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED YES
)
add_test(NAME poco_debug_stepping COMMAND $<TARGET_FILE:poco_debug_stepping>)
set_tests_properties(poco_debug_stepping PROPERTIES TIMEOUT 10)

# Extended images expose typed live locals while minimal images do not; both
# retain source-line call stacks for backtraces.
add_executable(poco_debug_inspection
    debug_inspection/debug_inspection.c
)
target_compile_definitions(poco_debug_inspection PRIVATE
    POCO_DEBUG_INSPECTION_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/debug_inspection/debug_inspection.poc"
)
target_link_libraries(poco_debug_inspection PRIVATE Poco::poco)
set_target_properties(poco_debug_inspection PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED YES
)
add_test(NAME poco_debug_inspection COMMAND $<TARGET_FILE:poco_debug_inspection>)
set_tests_properties(poco_debug_inspection PROPERTIES TIMEOUT 10)

# The standalone debugger is driven entirely through stdin. Exercise source
# compilation, extended-image loading, break/step/next/inspection commands,
# and refusal when the external source no longer matches its stored hash.
add_test(
    NAME poco_cli_interactive_debugger
    COMMAND "${CMAKE_COMMAND}"
        "-DPOCO_EXECUTABLE=$<TARGET_FILE:poco>"
        "-DPOCO_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/cli_debugger/interactive_debugger.poc"
        "-DPOCO_COMMANDS=${CMAKE_CURRENT_SOURCE_DIR}/cli_debugger/commands.txt"
        "-DPOCO_FIXTURE_DIR=${CMAKE_CURRENT_BINARY_DIR}/cli_debugger"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cli_debugger/verify-interactive-debugger.cmake"
)
set_tests_properties(poco_cli_interactive_debugger PROPERTIES TIMEOUT 15)

add_test(
    NAME poco_cli_debug_level
    COMMAND "${CMAKE_COMMAND}"
        "-DPOCO_EXECUTABLE=$<TARGET_FILE:poco>"
        "-DPOCO_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/debug_metadata/debug_metadata.poc"
        "-DPOCO_FIXTURE_DIR=${CMAKE_CURRENT_BINARY_DIR}/debug_metadata"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/debug_metadata/verify-cli-debug-level.cmake"
)
set_tests_properties(poco_cli_debug_level PROPERTIES TIMEOUT 10)

add_executable(poco_program_image_roundtrip
    program_image_roundtrip/program_image_roundtrip.c
)
target_include_directories(poco_program_image_roundtrip PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
    "${CMAKE_CURRENT_SOURCE_DIR}/support"
)
target_link_libraries(poco_program_image_roundtrip PRIVATE Poco::poco)
add_test(NAME poco_program_image_roundtrip
    COMMAND $<TARGET_FILE:poco_program_image_roundtrip>
)
set_tests_properties(poco_program_image_roundtrip PROPERTIES TIMEOUT 10)

add_executable(poco_program_image_portable_structs
    program_image_portable_structs/program_image_portable_structs.c
)
target_include_directories(poco_program_image_portable_structs PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
    "${CMAKE_CURRENT_SOURCE_DIR}/support"
)
target_link_libraries(poco_program_image_portable_structs PRIVATE Poco::poco poco_ffi)
add_test(NAME poco_program_image_portable_structs
    COMMAND $<TARGET_FILE:poco_program_image_portable_structs>
)
set_tests_properties(poco_program_image_portable_structs PROPERTIES TIMEOUT 10)

# Extended debug metadata uses the same endian-explicit image and abstract
# struct schema as the portable program image.  Compare every decoded field and
# reject unknown types specifically inside the debug-locals section.
add_executable(poco_program_image_portable_debug
    program_image_portable_debug/program_image_portable_debug.c
)
target_compile_definitions(poco_program_image_portable_debug PRIVATE
    POCO_PORTABLE_DEBUG_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/debug_metadata/debug_metadata.poc"
)
target_include_directories(poco_program_image_portable_debug PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
    "${CMAKE_CURRENT_SOURCE_DIR}/support"
)
target_link_libraries(poco_program_image_portable_debug PRIVATE Poco::poco poco_ffi)
add_test(NAME poco_program_image_portable_debug
    COMMAND $<TARGET_FILE:poco_program_image_portable_debug>
)
set_tests_properties(poco_program_image_portable_debug PROPERTIES TIMEOUT 10)

add_executable(poco_serialization_api
	serialization_api/serialization_api.c
)
target_include_directories(poco_serialization_api PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/../src"
	"${CMAKE_CURRENT_SOURCE_DIR}/support"
)
target_link_libraries(poco_serialization_api PRIVATE Poco::poco)
add_test(NAME poco_serialization_api
	COMMAND $<TARGET_FILE:poco_serialization_api>
)
set_tests_properties(poco_serialization_api PROPERTIES TIMEOUT 10)

add_executable(poco_baked_variant
	baked_variant/baked_variant.c
)
target_include_directories(poco_baked_variant PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/support"
)
target_link_libraries(poco_baked_variant PRIVATE Poco::poco)
add_test(NAME poco_baked_variant
	COMMAND $<TARGET_FILE:poco_baked_variant>
)
set_tests_properties(poco_baked_variant PROPERTIES TIMEOUT 10)

add_executable(poco_load_validation
	load_validation/load_validation.c
)
target_include_directories(poco_load_validation PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/../src"
	"${CMAKE_CURRENT_SOURCE_DIR}/support"
)
target_link_libraries(poco_load_validation PRIVATE Poco::poco)
add_test(NAME poco_load_validation
	COMMAND $<TARGET_FILE:poco_load_validation>
)
set_tests_properties(poco_load_validation PROPERTIES TIMEOUT 10)

# Compile-time ownership check for the program/activation split. This private
# test deliberately includes the internal type definition: the public
# PocoActivation handle is only exposed through the activation-pool primitive.
add_executable(poco_activation_types activation_types/activation_types.c)
target_include_directories(poco_activation_types PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")
target_link_libraries(poco_activation_types PRIVATE Poco::poco poco_ffi)
set_target_properties(poco_activation_types PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS YES
)
add_test(NAME poco_activation_types COMMAND poco_activation_types)

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

# Struct descriptors recursively mirror compiler Struct_info metadata.  Keep
# their elements arrays alive with the binding and verify libffi's computed
# layout against the native ABI before struct call marshaling consumes them.
add_executable(poco_ffi_struct_type ffi_struct_type/ffi_struct_type.c)
target_include_directories(poco_ffi_struct_type PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)
target_link_libraries(poco_ffi_struct_type PRIVATE Poco::poco poco_ffi)
add_test(NAME poco_ffi_struct_type COMMAND $<TARGET_FILE:poco_ffi_struct_type>)
set_tests_properties(poco_ffi_struct_type PROPERTIES TIMEOUT 10)

# End-to-end aggregate argument gate: compiler classification, VM transport,
# libffi by-value delivery, nested layout, and rejection of truncated bounds.
add_executable(poco_ffi_struct_args ffi_struct_args/ffi_struct_args.c)
target_include_directories(poco_ffi_struct_args PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)
target_link_libraries(poco_ffi_struct_args PRIVATE Poco::poco poco_ffi)
add_test(NAME poco_ffi_struct_args COMMAND $<TARGET_FILE:poco_ffi_struct_args>)
set_tests_properties(poco_ffi_struct_args PROPERTIES TIMEOUT 10)

# A native aggregate result lands in an activation-local aligned temp, then
# copies through a bounded lvalue.  Array elements exercise the computed-lvalue
# path while the same binding also consumes aggregate arguments by value.
add_executable(poco_ffi_struct_return ffi_struct_return/ffi_struct_return.c)
target_include_directories(poco_ffi_struct_return PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)
target_link_libraries(poco_ffi_struct_return PRIVATE Poco::poco poco_ffi)
add_test(NAME poco_ffi_struct_return COMMAND $<TARGET_FILE:poco_ffi_struct_return>)
set_tests_properties(poco_ffi_struct_return PROPERTIES TIMEOUT 10)

# Host aggregate bindings use real by-value vec/quat/matrix signatures.  Their
# results can be delivered through contracted writable borrowed spans, and an
# interior pointer with too little remaining capacity must fail before native
# code runs.
add_executable(poco_ffi_aggregate_bindings
    ffi_aggregate_bindings/ffi_aggregate_bindings.c
)
target_link_libraries(poco_ffi_aggregate_bindings PRIVATE Poco::poco)
add_test(NAME poco_ffi_aggregate_bindings
    COMMAND $<TARGET_FILE:poco_ffi_aggregate_bindings>
)
set_tests_properties(poco_ffi_aggregate_bindings PROPERTIES TIMEOUT 10)

# Fixed bindings prepare their immutable cif at compile time and reuse
# activation-owned argument storage.  The counter fixture proves the repeated
# fixed path is allocation/prep free while variadic calls retain both steps.
add_executable(poco_ffi_cif_cache ffi_cif_cache/ffi_cif_cache.c)
target_include_directories(poco_ffi_cif_cache PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)
target_link_libraries(poco_ffi_cif_cache PRIVATE Poco::poco poco_ffi)
add_test(NAME poco_ffi_cif_cache COMMAND $<TARGET_FILE:poco_ffi_cif_cache>)
set_tests_properties(poco_ffi_cif_cache PROPERTIES TIMEOUT 10)

# Source float values share Poco's double compute class, but fixed native float
# parameters and results still require 4-byte libffi slots at the ABI boundary.
add_executable(poco_ffi_float_marshaling
    ffi_float_marshaling/ffi_float_marshaling.c
)
target_link_libraries(poco_ffi_float_marshaling PRIVATE Poco::poco)
add_test(NAME poco_ffi_float_marshaling
    COMMAND $<TARGET_FILE:poco_ffi_float_marshaling>
)
set_tests_properties(poco_ffi_float_marshaling PROPERTIES TIMEOUT 10)

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
target_include_directories(poco_callback_invoke PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
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

# The public memory entry point compiles without opening its diagnostic name.
# File compilation delegates through the same path, so successful results and
# syntax-error name/line/message details must remain identical.
add_executable(poco_compile_buffer compile_buffer/compile_buffer.c)
target_compile_definitions(poco_compile_buffer PRIVATE
    POCO_COMPILE_BUFFER_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/compile_buffer"
)
target_link_libraries(poco_compile_buffer PRIVATE Poco::poco)
add_test(NAME poco_compile_buffer COMMAND $<TARGET_FILE:poco_compile_buffer>)
set_tests_properties(poco_compile_buffer PROPERTIES
    TIMEOUT 10
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/compile_buffer"
)

# Capture deterministic single-threaded results before the runtime and compiler
# state are made concurrent.  Later phase gates include the same reference data
# header so their threaded results are checked against this exact oracle.
add_executable(poco_reference_fixtures
    reference_fixtures/reference_fixtures.c
)
target_compile_definitions(poco_reference_fixtures PRIVATE
    POCO_REFERENCE_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/reference_fixtures"
)
target_include_directories(poco_reference_fixtures PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/reference_fixtures"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)
target_link_libraries(poco_reference_fixtures PRIVATE Poco::poco Threads::Threads)
add_poco_concurrency_test(poco_reference_fixtures poco_reference_fixtures)

# Hosts own activation pooling policy.  This gate compiles one immutable
# program, concurrently acquires one activation per host thread, and repeatedly
# runs/reset/reuses those activations against the Phase-1 reference oracle.
add_executable(poco_activation_pool activation_pool/activation_pool.c)
target_compile_definitions(poco_activation_pool PRIVATE
    POCO_ACTIVATION_POOL_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/reference_fixtures"
)
target_include_directories(poco_activation_pool PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/reference_fixtures"
)
target_link_libraries(poco_activation_pool PRIVATE Poco::poco Threads::Threads)
set_target_properties(poco_activation_pool PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED YES
)
add_poco_concurrency_test(poco_activation_pool poco_activation_pool)

# The host-driven lifecycle separates global initialization from main while
# retaining the bundled activation/VM entry points as compatibility helpers.
add_executable(poco_init_run_split init_run_split/init_run_split.c)
target_compile_definitions(poco_init_run_split PRIVATE
    POCO_INIT_RUN_SPLIT_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/init_run_split"
)
target_include_directories(poco_init_run_split PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)
target_link_libraries(poco_init_run_split PRIVATE Poco::poco poco_ffi)
set_target_properties(poco_init_run_split PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED YES
)
add_test(NAME poco_init_run_split COMMAND $<TARGET_FILE:poco_init_run_split>)
set_tests_properties(poco_init_run_split PROPERTIES TIMEOUT 10)

# Host global access uses retained root-scope symbol metadata to seed and read
# persistent activation data for both compiled and deserialized programs.
add_executable(poco_global_access global_access/global_access.c)
target_compile_definitions(poco_global_access PRIVATE
    POCO_GLOBAL_ACCESS_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/global_access"
)
target_include_directories(poco_global_access PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)
target_link_libraries(poco_global_access PRIVATE Poco::poco poco_ffi)
set_target_properties(poco_global_access PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED YES
)
add_test(NAME poco_global_access COMMAND $<TARGET_FILE:poco_global_access>)
set_tests_properties(poco_global_access PROPERTIES TIMEOUT 10)

# Main-only host invocation accepts either no parameters or a copied,
# bounds-carrying argc/argv pair and never reruns global initialization.
add_executable(poco_run_main_argv run_main_argv/run_main_argv.c)
target_compile_definitions(poco_run_main_argv PRIVATE
    POCO_RUN_MAIN_ARGV_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/run_main_argv"
)
target_include_directories(poco_run_main_argv PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)
target_link_libraries(poco_run_main_argv PRIVATE Poco::poco poco_ffi)
set_target_properties(poco_run_main_argv PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED YES
)
add_test(NAME poco_run_main_argv COMMAND $<TARGET_FILE:poco_run_main_argv>)
set_tests_properties(poco_run_main_argv PROPERTIES TIMEOUT 10)

# One-shot named calls reuse the interpreter's typed callback argument layout,
# with invoke-time scalar coercion and typed return marshaling.
add_executable(poco_call_builder call_builder/call_builder.c)
target_compile_definitions(poco_call_builder PRIVATE
    POCO_CALL_BUILDER_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/call_builder"
)
target_link_libraries(poco_call_builder PRIVATE Poco::poco)
set_target_properties(poco_call_builder PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED YES
)
add_test(NAME poco_call_builder COMMAND $<TARGET_FILE:poco_call_builder>)
set_tests_properties(poco_call_builder PROPERTIES TIMEOUT 10)

# Call-builder pointer arguments borrow exact host spans. Direct interpreter
# reads and writes consult the same permission registry as native contracts.
add_executable(poco_call_pointer_args call_pointer_args/call_pointer_args.c)
target_compile_definitions(poco_call_pointer_args PRIVATE
    POCO_CALL_POINTER_ARGS_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/call_pointer_args"
)
target_link_libraries(poco_call_pointer_args PRIVATE Poco::poco)
set_target_properties(poco_call_pointer_args PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED YES
)
add_test(NAME poco_call_pointer_args COMMAND $<TARGET_FILE:poco_call_pointer_args>)
set_tests_properties(poco_call_pointer_args PROPERTIES TIMEOUT 10)

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

# The trusted graph capability table is intentionally just scalar ANSI math.
# Logic and bitwise operators remain language built-ins, while side-effecting
# standard-library categories and host-specific aggregate bindings stay out.
add_executable(poco_trusted_graph_table trusted_graph_table/trusted_graph_table.c)
target_link_libraries(poco_trusted_graph_table PRIVATE Poco::poco)
add_test(NAME poco_trusted_graph_table COMMAND $<TARGET_FILE:poco_trusted_graph_table>)
set_tests_properties(poco_trusted_graph_table PROPERTIES TIMEOUT 10)

# The trusted graph path must not inherit a host's numeric locale, time, or
# random state.  Compile and repeatedly run all curated scalar math bindings
# under both dot- and comma-decimal locales; unavailable optional locales are
# skipped by the executable while the mandatory C locale always runs.
add_executable(poco_deterministic_bindings
	deterministic_bindings/deterministic_bindings.c)
target_link_libraries(poco_deterministic_bindings PRIVATE Poco::poco)
add_test(NAME poco_deterministic_bindings
	COMMAND $<TARGET_FILE:poco_deterministic_bindings>)
set_tests_properties(poco_deterministic_bindings PROPERTIES TIMEOUT 20)

# The untrusted expression table retains scalar math/logic while excluding
# every script-visible FFI pointer path, including native pointer returns that
# would otherwise execute OP_CPT_TO_PPT with permissive bounds.
add_executable(poco_untrusted_expression_tier
	untrusted_expression_tier/untrusted_expression_tier.c)
target_link_libraries(poco_untrusted_expression_tier PRIVATE Poco::poco)
add_test(NAME poco_untrusted_expression_tier
	COMMAND $<TARGET_FILE:poco_untrusted_expression_tier>)
set_tests_properties(poco_untrusted_expression_tier PROPERTIES TIMEOUT 10)

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

# Native-module lookup has fixed default precedence plus an ordered VM-owned
# extension list, including pathless in-memory compile behavior.
add_subdirectory(poe_search_path)
add_subdirectory(external_library_image)

# The final external-library CLI gate compiles the canonical poekit hello
# script to a source-less image, reloads its module by unresolved name, and
# substitutes invalid modules under that same name for load-time diagnostics.
add_subdirectory(cli_external_library)

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
