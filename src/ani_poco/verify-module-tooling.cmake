# The generic native-module helper is part of Poco's standalone contract.  It
# must remain free of Animator paths and targets, while the Animator helper is
# the only place where retained native-POE compatibility is configured.

foreach(_required
    POCO_POE_HELPER
    ANI_POE_HELPER
    POEKIT_CMAKE_FILE)
    if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "${_required} is required")
    endif()
endforeach()

foreach(_file
    "${POCO_POE_HELPER}"
    "${ANI_POE_HELPER}"
    "${POEKIT_CMAKE_FILE}")
    if(NOT EXISTS "${_file}")
        message(FATAL_ERROR "Expected module tooling file is missing: ${_file}")
    endif()
endforeach()

file(READ "${POCO_POE_HELPER}" _generic_helper)
if(NOT _generic_helper MATCHES "function\\(add_poe_library TARGET\\)")
    message(FATAL_ERROR "The generic helper must provide add_poe_library()")
endif()
foreach(_animator_dependency
    "src/inc"
    "animhost"
    "gfxlib"
    "raster"
    "ani_poco_adapter")
    if(_generic_helper MATCHES "${_animator_dependency}")
        message(FATAL_ERROR
            "Generic add_poe_library() must not acquire Animator dependency '${_animator_dependency}'")
    endif()
endforeach()
if(NOT _generic_helper MATCHES "Poco::poco")
    message(FATAL_ERROR "Generic add_poe_library() must link Poco::poco")
endif()

file(READ "${ANI_POE_HELPER}" _ani_helper)
if(NOT _ani_helper MATCHES "function\\(ani_add_poe_library TARGET\\)")
    message(FATAL_ERROR "The Animator helper must provide ani_add_poe_library()")
endif()
foreach(_required_ani_dependency
    "src/inc"
    "animhost"
    "gfxlib"
    "raster"
    "ani_poco_adapter"
    "LEGACY_POE")
    if(NOT _ani_helper MATCHES "${_required_ani_dependency}")
        message(FATAL_ERROR
            "Animator helper is missing '${_required_ani_dependency}' support")
    endif()
endforeach()

file(READ "${POEKIT_CMAKE_FILE}" _poekit_cmake)
foreach(_classification
    "POEKIT_GENERIC_MODULES"
    "POEKIT_ANI_MODULES"
    "POEKIT_TEST_ONLY_MODULES"
    "POEKIT_LEGACY_DEAD_MODULES")
    if(NOT _poekit_cmake MATCHES "${_classification}")
        message(FATAL_ERROR "poekit classification is missing ${_classification}")
    endif()
endforeach()
