# PocoModule.cmake
#
# Build a host-neutral native Poco module.  The module ABI is declared in
# <poco/poco.h>; this helper deliberately provides no Animator include paths,
# libraries, compiler definitions, runtime policy, installation RPATH, or
# deployment layout.  It is installed with the Poco package and is the only
# supported helper for an external generic module.
#
# Usage:
#   poco_add_module(name
#       SOURCES module.c [more.c]
#       [INCLUDES include-dir ...]
#       [DEPS dependency-target ...]
#       [INSTALL_DIR destination])

include(CMakeParseArguments)

function(poco_add_module TARGET)
    cmake_parse_arguments(
        POCO_MODULE
        ""
        "INSTALL_DIR"
        "SOURCES;INCLUDES;DEPS"
        ${ARGN}
    )

    if(NOT POCO_MODULE_SOURCES)
        message(FATAL_ERROR "poco_add_module(${TARGET}): SOURCES is required")
    endif()

    if(NOT TARGET Poco::poco)
        message(FATAL_ERROR
            "poco_add_module(${TARGET}) requires the Poco::poco target; add Poco before defining modules")
    endif()

    add_library(${TARGET} MODULE ${POCO_MODULE_SOURCES})
    target_link_libraries(${TARGET} PRIVATE Poco::poco ${POCO_MODULE_DEPS})
    if(POCO_MODULE_INCLUDES)
        target_include_directories(${TARGET} PRIVATE ${POCO_MODULE_INCLUDES})
    endif()

    set_target_properties(${TARGET} PROPERTIES
        PREFIX ""
        SUFFIX ".poe"
        POSITION_INDEPENDENT_CODE ON
    )

    if(POCO_MODULE_INSTALL_DIR)
        install(TARGETS ${TARGET} DESTINATION "${POCO_MODULE_INSTALL_DIR}")
    endif()
endfunction()
