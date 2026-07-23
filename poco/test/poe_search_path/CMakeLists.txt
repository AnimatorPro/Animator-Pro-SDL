include(PocoModule)

set(POCO_SEARCH_SCRIPT_DIR "${CMAKE_CURRENT_BINARY_DIR}/script")
set(POCO_SEARCH_CWD_DIR "${CMAKE_CURRENT_BINARY_DIR}/cwd")
set(POCO_SEARCH_EXE_DIR "${CMAKE_CURRENT_BINARY_DIR}/exe")
set(POCO_SEARCH_HOST_FIRST_DIR "${CMAKE_CURRENT_BINARY_DIR}/host-first")
set(POCO_SEARCH_HOST_SECOND_DIR "${CMAKE_CURRENT_BINARY_DIR}/host-second")
set(POCO_SEARCH_VIRTUAL_DIR "${POCO_SEARCH_CWD_DIR}/virtual/looks")

file(MAKE_DIRECTORY
    "${POCO_SEARCH_SCRIPT_DIR}"
    "${POCO_SEARCH_CWD_DIR}"
    "${POCO_SEARCH_EXE_DIR}"
    "${POCO_SEARCH_HOST_FIRST_DIR}"
    "${POCO_SEARCH_HOST_SECOND_DIR}"
    "${POCO_SEARCH_VIRTUAL_DIR}"
)
configure_file(ordered.poc "${POCO_SEARCH_SCRIPT_DIR}/ordered.poc" COPYONLY)

function(add_poe_search_fixture TARGET OUTPUT_NAME OUTPUT_DIR VALUE)
    poco_add_module(${TARGET} SOURCES module.c)
    target_compile_definitions(${TARGET} PRIVATE POCO_SEARCH_PATH_VALUE=${VALUE})
    set_target_properties(${TARGET} PROPERTIES
        OUTPUT_NAME "${OUTPUT_NAME}"
        LIBRARY_OUTPUT_DIRECTORY "${OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY "${OUTPUT_DIR}"
    )
endfunction()

# The same requested module in every location proves the complete default
# precedence chain starts with a physical file's script directory.
add_poe_search_fixture(poe_search_ordered_script ordered "${POCO_SEARCH_SCRIPT_DIR}" 11)
add_poe_search_fixture(poe_search_ordered_cwd ordered "${POCO_SEARCH_CWD_DIR}" 22)
add_poe_search_fixture(poe_search_ordered_exe ordered "${POCO_SEARCH_EXE_DIR}" 33)
add_poe_search_fixture(poe_search_ordered_host ordered "${POCO_SEARCH_HOST_FIRST_DIR}" 44)

# The virtual-name copy must be skipped; CWD must beat executable and host.
add_poe_search_fixture(poe_search_buffer_virtual buffer_cwd "${POCO_SEARCH_VIRTUAL_DIR}" 99)
add_poe_search_fixture(poe_search_buffer_cwd buffer_cwd "${POCO_SEARCH_CWD_DIR}" 22)
add_poe_search_fixture(poe_search_buffer_exe buffer_cwd "${POCO_SEARCH_EXE_DIR}" 33)
add_poe_search_fixture(poe_search_buffer_host buffer_cwd "${POCO_SEARCH_HOST_FIRST_DIR}" 44)

# Executable-directory resolution remains active for pathless buffers and
# precedes a host-added directory containing the same name.
add_poe_search_fixture(poe_search_executable executable_over_host "${POCO_SEARCH_EXE_DIR}" 33)
add_poe_search_fixture(poe_search_executable_host executable_over_host "${POCO_SEARCH_HOST_FIRST_DIR}" 44)

# Host additions preserve append order and accept paths without trailing '/'.
add_poe_search_fixture(poe_search_host_first host_order "${POCO_SEARCH_HOST_FIRST_DIR}" 44)
add_poe_search_fixture(poe_search_host_second host_order "${POCO_SEARCH_HOST_SECOND_DIR}" 55)

add_executable(poco_poe_search_path host.c)
set_target_properties(poco_poe_search_path PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${POCO_SEARCH_EXE_DIR}"
)
target_compile_definitions(poco_poe_search_path PRIVATE
    POCO_SEARCH_SCRIPT_PATH="${POCO_SEARCH_SCRIPT_DIR}/ordered.poc"
    POCO_SEARCH_HOST_FIRST_DIR="${POCO_SEARCH_HOST_FIRST_DIR}"
    POCO_SEARCH_HOST_SECOND_DIR="${POCO_SEARCH_HOST_SECOND_DIR}"
)
target_link_libraries(poco_poe_search_path PRIVATE Poco::poco)
add_dependencies(poco_poe_search_path
    poe_search_ordered_script
    poe_search_ordered_cwd
    poe_search_ordered_exe
    poe_search_ordered_host
    poe_search_buffer_virtual
    poe_search_buffer_cwd
    poe_search_buffer_exe
    poe_search_buffer_host
    poe_search_executable
    poe_search_executable_host
    poe_search_host_first
    poe_search_host_second
)

add_test(NAME poco_poe_search_path COMMAND $<TARGET_FILE:poco_poe_search_path>)
set_tests_properties(poco_poe_search_path PROPERTIES
    WORKING_DIRECTORY "${POCO_SEARCH_CWD_DIR}"
    TIMEOUT 20
)
