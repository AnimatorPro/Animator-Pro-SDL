# PocoPoeLibrary.cmake
#
# Build an in-tree native Poco module that depends only on Poco and on
# dependencies explicitly supplied by its caller.  This helper is not
# installed as an external-package API; external generic modules use
# poco_add_module().  A host whose modules link against the host's own
# libraries supplies its own helper in the host tree, rather than extending
# this one with implicit host dependencies.
#
# Usage:
#   add_poe_library(name
#       SOURCES module.c [more.c]
#       [INCLUDES include-dir ...]
#       [DEPS dependency-target ...]
#       [OUTPUT_NAME module-file-name]
#       [NO_INSTALL]
#       [INSTALL_DIR destination]
#       [RPATH path])
#
# Test deployment is deliberately absent: where a module's scripts are
# installed and which interpreter runs them is the embedding host's layout
# decision, not Poco's.  Register those tests beside the module that needs
# them.

include(CMakeParseArguments)

function(add_poe_library TARGET)
    cmake_parse_arguments(
        POE
        "NO_INSTALL"
        "INSTALL_DIR;OUTPUT_NAME;RPATH"
        "SOURCES;INCLUDES;DEPS"
        ${ARGN}
    )

    if(POE_NO_INSTALL AND POE_INSTALL_DIR)
        message(FATAL_ERROR
            "add_poe_library(${TARGET}): NO_INSTALL and INSTALL_DIR are mutually exclusive")
    endif()

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

    # Loader fixtures exist only to be dlopen()ed out of the build tree, so
    # they are never part of an installed package.
    if(NOT POE_NO_INSTALL)
        if(NOT POE_INSTALL_DIR)
            set(POE_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}/poco")
        endif()
        install(TARGETS ${TARGET} DESTINATION "${POE_INSTALL_DIR}")
    endif()
endfunction()
