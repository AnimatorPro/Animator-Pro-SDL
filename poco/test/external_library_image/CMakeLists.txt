include(PocoModule)

poco_add_module(serialize_fixture SOURCES module.c)
poco_add_module(badabi____fixture SOURCES bad_abi.c)
poco_add_module(misssym___fixture SOURCES missing_symbol.c)
poco_add_module(fileleg___fixture SOURCES module.c)
set_target_properties(fileleg___fixture PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/file-only")

add_executable(poco_external_library_image host.c)
target_compile_definitions(poco_external_library_image PRIVATE
    POCO_EXTERNAL_LIBRARY_IMAGE_MODULE_DIR="$<TARGET_FILE_DIR:serialize_fixture>"
)
target_include_directories(poco_external_library_image PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src"
    "${CMAKE_CURRENT_SOURCE_DIR}/../support"
)
target_link_libraries(poco_external_library_image PRIVATE Poco::poco poco_ffi)
add_dependencies(poco_external_library_image serialize_fixture badabi____fixture
    misssym___fixture)

add_test(NAME poco_external_library_image COMMAND $<TARGET_FILE:poco_external_library_image>)
set_tests_properties(poco_external_library_image PROPERTIES TIMEOUT 20)

add_executable(poco_external_library_reload_negative reload_negative.c)
target_compile_definitions(poco_external_library_reload_negative PRIVATE
    POCO_EXTERNAL_LIBRARY_IMAGE_MODULE_DIR="$<TARGET_FILE_DIR:serialize_fixture>"
    POCO_EXTERNAL_LIBRARY_FILE_DIR="$<TARGET_FILE_DIR:fileleg___fixture>"
)
target_include_directories(poco_external_library_reload_negative PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src"
)
target_link_libraries(poco_external_library_reload_negative PRIVATE Poco::poco poco_ffi)
add_dependencies(poco_external_library_reload_negative serialize_fixture badabi____fixture
    misssym___fixture fileleg___fixture)

add_test(NAME poco_external_library_reload_negative
    COMMAND $<TARGET_FILE:poco_external_library_reload_negative>)
set_tests_properties(poco_external_library_reload_negative PROPERTIES TIMEOUT 20)
