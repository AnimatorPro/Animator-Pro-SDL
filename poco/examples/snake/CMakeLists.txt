set(POCO_SDL3_SOURCE_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../../../thirdparty/sdl3"
    CACHE PATH
    "Path to the SDL3 source tree used by Poco examples"
)

if(NOT TARGET SDL3::SDL3)
    if(NOT EXISTS "${POCO_SDL3_SOURCE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR
            "POCO_BUILD_EXAMPLES requires SDL3. Set POCO_SDL3_SOURCE_DIR to "
            "an SDL3 source tree, or provide an SDL3::SDL3 target before "
            "adding Poco."
        )
    endif()
    add_subdirectory(
        "${POCO_SDL3_SOURCE_DIR}"
        "${CMAKE_CURRENT_BINARY_DIR}/sdl3"
        EXCLUDE_FROM_ALL
    )
endif()

add_executable(snake
    main.c
    sdl_bindings.c
)
target_link_libraries(snake PRIVATE
    Poco::poco
    SDL3::SDL3
)
set_target_properties(snake PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
)

# Keep snake.poc next to the executable so the example runs from any working
# directory, in both the build tree and the install tree.
add_custom_command(TARGET snake POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/snake.poc"
        "$<TARGET_FILE_DIR:snake>/snake.poc"
    VERBATIM
)

install(TARGETS snake RUNTIME DESTINATION examples/snake)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/snake.poc"
    DESTINATION examples/snake)

# When Poco is a shared library, ship libpoco beside the example and resolve it
# through @loader_path so snake runs without a system-wide install.
get_target_property(_poco_linkage Poco::poco TYPE)
if(_poco_linkage STREQUAL "SHARED_LIBRARY")
    add_custom_command(TARGET snake POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Poco::poco>"
            "$<TARGET_FILE_DIR:snake>/$<TARGET_FILE_NAME:Poco::poco>"
        VERBATIM
    )
    install(FILES "$<TARGET_FILE:Poco::poco>"
        DESTINATION examples/snake)
    set_target_properties(snake PROPERTIES
        BUILD_RPATH "@loader_path"
        INSTALL_RPATH "@loader_path"
    )
endif()
