# PocoPoeLibrary.cmake
#
# Build an in-tree native Poco module that depends only on Poco and on
# dependencies explicitly supplied by its caller.  This helper is not
# installed as an external-package API; external generic modules use
# poco_add_module().  Animator-native modules use the separate helper owned by
# src/ani_poco, never this helper plus implicit Animator dependencies.
#
# Usage:
#   add_poe_library(name
#       SOURCES module.c [more.c]
#       [INCLUDES include-dir ...]
#       [DEPS dependency-target ...]
#       [OUTPUT_NAME module-file-name]
#       [INSTALL_DIR destination]
#       [RPATH path]
#       [TEST_FILE script.poc]
#       [SCRIPTS script.poc ...]
#       [RUNNER poco])

include(CMakeParseArguments)

function(add_poe_library TARGET)
    cmake_parse_arguments(
        POE
        ""
        "INSTALL_DIR;OUTPUT_NAME;RPATH;TEST_FILE;RUNNER"
        "SOURCES;INCLUDES;DEPS;SCRIPTS"
        ${ARGN}
    )

    if(NOT POE_SOURCES)
        message(FATAL_ERROR "add_poe_library(${TARGET}): SOURCES is required")
    endif()
    if(NOT TARGET Poco::poco)
        message(FATAL_ERROR
            "add_poe_library(${TARGET}) requires Poco::poco; add Poco before defining modules")
    endif()

    add_library(${TARGET} SHARED ${POE_SOURCES})
    target_link_libraries(${TARGET} PRIVATE Poco::poco ${POE_DEPS})
    if(POE_INCLUDES)
        target_include_directories(${TARGET} PRIVATE ${POE_INCLUDES})
    endif()
    if(NOT POE_OUTPUT_NAME)
        set(POE_OUTPUT_NAME ${TARGET})
    endif()
    set_target_properties(${TARGET} PROPERTIES
        PREFIX ""
        OUTPUT_NAME ${POE_OUTPUT_NAME}
        SUFFIX ".poe"
        POSITION_INDEPENDENT_CODE ON
        C_VISIBILITY_PRESET default
        VISIBILITY_INLINES_HIDDEN NO
    )

    # Modules conventionally install one directory below the prefix.  Keep
    # their Poco dependency relocatable while allowing an embedding host to
    # override this layout with RPATH.
    if(POE_RPATH)
        set(_poe_install_rpath "${POE_RPATH}")
    elseif(APPLE)
        set(_poe_install_rpath "@loader_path/../lib")
    elseif(UNIX)
        set(_poe_install_rpath "$ORIGIN/../lib")
    endif()
    if(DEFINED _poe_install_rpath)
        set_target_properties(${TARGET} PROPERTIES
            INSTALL_RPATH "${_poe_install_rpath}"
        )
    endif()

    if(NOT POE_INSTALL_DIR)
        set(POE_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}/poco")
    endif()
    install(TARGETS ${TARGET} DESTINATION "${POE_INSTALL_DIR}")

    if(POE_TEST_FILE)
        install(TARGETS ${TARGET} DESTINATION "${CMAKE_INSTALL_PREFIX}/tests")
    endif()
    foreach(script IN LISTS POE_SCRIPTS)
        install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/${script}"
            DESTINATION "${CMAKE_INSTALL_PREFIX}/tests")
    endforeach()

    if(POE_TEST_FILE)
        get_filename_component(_test_filename "${POE_TEST_FILE}" NAME)
        install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/${POE_TEST_FILE}"
            DESTINATION "${CMAKE_INSTALL_PREFIX}/tests")
        if(NOT POE_RUNNER)
            set(POE_RUNNER "poco")
        endif()
        if(POE_RUNNER STREQUAL "poco")
            add_test(
                NAME "poco_${TARGET}"
                COMMAND ${CMAKE_INSTALL_PREFIX}/poco
                    ${CMAKE_INSTALL_PREFIX}/tests/${_test_filename}
                WORKING_DIRECTORY ${CMAKE_INSTALL_PREFIX}
            )
        else()
            message(FATAL_ERROR
                "add_poe_library(${TARGET}): RUNNER '${POE_RUNNER}' is not available to generic modules")
        endif()
    endif()
endfunction()
