if(NOT DEFINED POCO_SOURCE_DIR OR NOT DEFINED ANIMATOR_SOURCE_DIR)
    message(FATAL_ERROR "POCO_SOURCE_DIR and ANIMATOR_SOURCE_DIR are required")
endif()

set(POCO_LEGACY_LIBRARY "${POCO_SOURCE_DIR}/include/pocolib.h")
set(POCO_LEGACY_FACE "${POCO_SOURCE_DIR}/include/pocoface.h")
set(POCO_LEGACY_REX "${POCO_SOURCE_DIR}/include/pocorex.h")
set(ANIMATOR_LEGACY_LIBRARY "${ANIMATOR_SOURCE_DIR}/src/inc/pocolib.h")
set(ANIMATOR_LEGACY_FACE "${ANIMATOR_SOURCE_DIR}/src/inc/pocoface.h")
set(ANIMATOR_LEGACY_REX "${ANIMATOR_SOURCE_DIR}/src/inc/pocorex.h")

foreach(HEADER IN LISTS POCO_LEGACY_LIBRARY POCO_LEGACY_FACE POCO_LEGACY_REX
        ANIMATOR_LEGACY_LIBRARY ANIMATOR_LEGACY_FACE ANIMATOR_LEGACY_REX)
    if(NOT EXISTS "${HEADER}")
        message(FATAL_ERROR "Missing compatibility header: ${HEADER}")
    endif()
endforeach()

file(READ "${POCO_LEGACY_LIBRARY}" POCO_LEGACY_LIBRARY_TEXT)
foreach(REQUIRED_TEXT
        "#include \"poco/poco.h\""
        "Compatibility-only legacy ABI header"
        "typedef struct poco_lib")
    string(FIND "${POCO_LEGACY_LIBRARY_TEXT}" "${REQUIRED_TEXT}" FOUND_INDEX)
    if(FOUND_INDEX EQUAL -1)
        message(FATAL_ERROR
            "${POCO_LEGACY_LIBRARY} must retain the documented canonical legacy ABI: ${REQUIRED_TEXT}")
    endif()
endforeach()

foreach(HEADER IN ITEMS "${ANIMATOR_LEGACY_LIBRARY}" "${ANIMATOR_LEGACY_FACE}" "${ANIMATOR_LEGACY_REX}")
    file(READ "${HEADER}" HEADER_TEXT)
    string(FIND "${HEADER_TEXT}" "Compatibility-only Animator header" FOUND_INDEX)
    if(FOUND_INDEX EQUAL -1)
        message(FATAL_ERROR "${HEADER} must document its temporary compatibility-only status")
    endif()
endforeach()

file(READ "${ANIMATOR_LEGACY_LIBRARY}" ANIMATOR_LEGACY_LIBRARY_TEXT)
foreach(DUPLICATE_DEFINITION
        "typedef struct popot"
        "typedef union pt_num"
        "typedef struct lib_proto"
        "typedef struct poco_lib")
    string(FIND "${ANIMATOR_LEGACY_LIBRARY_TEXT}" "${DUPLICATE_DEFINITION}" FOUND_INDEX)
    if(NOT FOUND_INDEX EQUAL -1)
        message(FATAL_ERROR
            "${ANIMATOR_LEGACY_LIBRARY} still owns duplicate Poco ABI layout: ${DUPLICATE_DEFINITION}")
    endif()
endforeach()

foreach(HEADER IN ITEMS "${ANIMATOR_LEGACY_FACE}" "${ANIMATOR_LEGACY_REX}")
    file(READ "${HEADER}" HEADER_TEXT)
    foreach(DUPLICATE_DEFINITION
            "Errcode compile_poco"
            "Errcode run_poco"
            "void free_poco"
            "typedef struct pocorex_hdr"
            "typedef struct pocorex")
        string(FIND "${HEADER_TEXT}" "${DUPLICATE_DEFINITION}" FOUND_INDEX)
        if(NOT FOUND_INDEX EQUAL -1)
            message(FATAL_ERROR "${HEADER} still owns duplicate Poco compatibility declaration: ${DUPLICATE_DEFINITION}")
        endif()
    endforeach()
endforeach()
