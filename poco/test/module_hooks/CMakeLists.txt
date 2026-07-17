include(PocoModule)

poco_add_module(poco_module_hook_valid
    SOURCES valid.c
)

poco_add_module(poco_module_hook_invalid
    SOURCES invalid.c
)

configure_file(valid.poc valid.poc COPYONLY)
configure_file(invalid.poc invalid.poc COPYONLY)

add_executable(poco_module_hooks host.c)
target_compile_definitions(poco_module_hooks PRIVATE
    POCO_MODULE_HOOK_FIXTURE_DIR="${CMAKE_CURRENT_BINARY_DIR}"
)
target_link_libraries(poco_module_hooks PRIVATE Poco::poco)
add_dependencies(poco_module_hooks
    poco_module_hook_valid
    poco_module_hook_invalid
)

add_test(NAME poco_module_hooks COMMAND $<TARGET_FILE:poco_module_hooks>)
set_tests_properties(poco_module_hooks PROPERTIES TIMEOUT 10)
