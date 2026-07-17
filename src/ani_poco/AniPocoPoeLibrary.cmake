# AniPocoPoeLibrary.cmake
#
# Build a module that intentionally depends on Animator.  This helper is kept
# outside poco/ so a standalone Poco project cannot acquire Animator paths or
# targets through its CMake surface.  LEGACY_POE is a deliberate compatibility
# policy for direct-table modules, not a way to give generic descriptors
# Animator globals.

include(CMakeParseArguments)
include("${CMAKE_SOURCE_DIR}/poco/cmake/PocoPoeLibrary.cmake")

function(ani_add_poe_library TARGET)
    cmake_parse_arguments(
        ANI_POE
        "LEGACY_POE"
        "INSTALL_DIR;TEST_FILE;RUNNER"
        "SOURCES;INCLUDES;DEPS;SCRIPTS"
        ${ARGN}
    )

    if(NOT ANI_POE_SOURCES)
        message(FATAL_ERROR "ani_add_poe_library(${TARGET}): SOURCES is required")
    endif()
    foreach(_ani_target animhost gfxlib raster)
        if(NOT TARGET ${_ani_target})
            message(FATAL_ERROR
                "ani_add_poe_library(${TARGET}) requires Animator target ${_ani_target}")
        endif()
    endforeach()

    if(NOT ANI_POE_INSTALL_DIR)
        set(ANI_POE_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}/resource")
    endif()

    set(_ani_poe_dependencies animhost gfxlib raster ${ANI_POE_DEPS})
    if(ANI_POE_LEGACY_POE)
        if(NOT TARGET ani_poco_adapter)
            message(FATAL_ERROR
                "ani_add_poe_library(${TARGET} LEGACY_POE) requires ani_poco_adapter")
        endif()
        list(APPEND _ani_poe_dependencies ani_poco_adapter)
        set(_ani_poe_legacy_property TRUE)
    else()
        set(_ani_poe_legacy_property FALSE)
        if(ANI_POE_RUNNER STREQUAL "ani-policy")
            message(FATAL_ERROR
                "ani_add_poe_library(${TARGET}): RUNNER ani-policy requires LEGACY_POE")
        endif()
    endif()

    add_poe_library(${TARGET}
        SOURCES ${ANI_POE_SOURCES}
        INCLUDES "${CMAKE_SOURCE_DIR}/src/inc" ${ANI_POE_INCLUDES}
        DEPS ${_ani_poe_dependencies}
        INSTALL_DIR "${ANI_POE_INSTALL_DIR}"
    )
    # Add both the Animator root (animhost) and the Poco library directory.
    # Set each entry separately so CMake does not collapse a list passed
    # through cmake_parse_arguments() into a single RPATH value.
    if(APPLE)
        set_property(TARGET ${TARGET} PROPERTY INSTALL_RPATH
            "@loader_path/.." "@loader_path/../lib")
    elseif(UNIX)
        set_property(TARGET ${TARGET} PROPERTY INSTALL_RPATH
            "$ORIGIN/.." "$ORIGIN/../lib")
    endif()
    set_property(TARGET ${TARGET} PROPERTY ANI_POCO_LEGACY_POE
        ${_ani_poe_legacy_property})

    foreach(script IN LISTS ANI_POE_SCRIPTS)
        install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/${script}"
            DESTINATION "${CMAKE_INSTALL_PREFIX}/tests")
    endforeach()
    if(ANI_POE_TEST_FILE)
        get_filename_component(_test_filename "${ANI_POE_TEST_FILE}" NAME)
        install(TARGETS ${TARGET} DESTINATION "${CMAKE_INSTALL_PREFIX}/tests")
        install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/${ANI_POE_TEST_FILE}"
            DESTINATION "${CMAKE_INSTALL_PREFIX}/tests")
        if(NOT ANI_POE_RUNNER)
            set(ANI_POE_RUNNER "ani")
        endif()
        if(ANI_POE_RUNNER STREQUAL "ani")
            add_test(
                NAME "poco_${TARGET}"
                COMMAND ${CMAKE_INSTALL_PREFIX}/ani -poc
                    ${CMAKE_INSTALL_PREFIX}/tests/${_test_filename}
                WORKING_DIRECTORY ${CMAKE_INSTALL_PREFIX}
            )
        elseif(ANI_POE_RUNNER STREQUAL "ani-policy" AND ANI_POE_LEGACY_POE)
            add_test(
                NAME "poco_${TARGET}"
                COMMAND $<TARGET_FILE:${TARGET}_test_host>
                    ${CMAKE_INSTALL_PREFIX}/tests/${_test_filename}
                WORKING_DIRECTORY ${CMAKE_INSTALL_PREFIX}
            )
        else()
            message(FATAL_ERROR
                "ani_add_poe_library(${TARGET}): unsupported RUNNER '${ANI_POE_RUNNER}'")
        endif()
    endif()
endfunction()
