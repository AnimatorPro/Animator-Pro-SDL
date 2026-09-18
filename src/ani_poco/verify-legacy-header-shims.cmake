if(NOT DEFINED POCO_SOURCE_DIR OR NOT DEFINED ANIMATOR_SOURCE_DIR)
    message(FATAL_ERROR "POCO_SOURCE_DIR and ANIMATOR_SOURCE_DIR are required")
endif()

set(POCO_LEGACY_CANONICAL "${POCO_SOURCE_DIR}/compat/poco/poco_legacy.h")
set(POCO_LEGACY_CANONICAL_TYPES "${POCO_SOURCE_DIR}/compat/poco/poco_legacy_types.h")
set(POCO_LEGACY_LIBRARY "${POCO_SOURCE_DIR}/src/pocolib.h")
set(POCO_LEGACY_FACE "${POCO_SOURCE_DIR}/src/pocoface.h")
set(POCO_LEGACY_REX "${POCO_SOURCE_DIR}/src/pocorex.h")
set(ANIMATOR_LEGACY_LIBRARY "${ANIMATOR_SOURCE_DIR}/src/inc/pocolib.h")
set(ANIMATOR_LEGACY_REX "${ANIMATOR_SOURCE_DIR}/src/inc/pocorex.h")

foreach(HEADER IN LISTS POCO_LEGACY_CANONICAL POCO_LEGACY_CANONICAL_TYPES
        POCO_LEGACY_LIBRARY POCO_LEGACY_FACE POCO_LEGACY_REX
        ANIMATOR_LEGACY_LIBRARY ANIMATOR_LEGACY_REX)
    if(NOT EXISTS "${HEADER}")
        message(FATAL_ERROR "Missing compatibility header: ${HEADER}")
    endif()
endforeach()

# The legacy ABI has exactly one definition, exported by Poco under compat/
# and reached by both trees as <poco/poco_legacy.h>.
file(READ "${POCO_LEGACY_CANONICAL}" POCO_LEGACY_CANONICAL_TEXT)
foreach(REQUIRED_TEXT
        "#include <poco/poco.h>"
        "Compatibility-only legacy ABI header"
        "typedef struct poco_lib"
        "typedef struct pocorex")
    string(FIND "${POCO_LEGACY_CANONICAL_TEXT}" "${REQUIRED_TEXT}" FOUND_INDEX)
    if(FOUND_INDEX EQUAL -1)
        message(FATAL_ERROR
            "${POCO_LEGACY_CANONICAL} must retain the documented canonical legacy ABI: ${REQUIRED_TEXT}")
    endif()
endforeach()

# ULONG was unsigned long in Poco and uint32_t in Animator, which is why it
# could never be shared.  It is gone; nothing may put it back on the boundary.
foreach(HEADER IN ITEMS "${POCO_LEGACY_CANONICAL}" "${POCO_LEGACY_CANONICAL_TYPES}")
    file(READ "${HEADER}" HEADER_TEXT)
    # Match declarations, not the prose explaining why they are gone.
    foreach(WIDTH_HAZARD
            "typedef[^;]*[ \t*]ULONG[ \t]*;"
            "typedef[^;]*[ \t*]LONG[ \t]*;"
            "[\r\n][ \t]*U?LONG[ \t]+[A-Za-z_*]")
        string(REGEX MATCH "${WIDTH_HAZARD}" FOUND_MATCH "${HEADER_TEXT}")
        if(NOT "${FOUND_MATCH}" STREQUAL "")
            message(FATAL_ERROR
                "${HEADER} puts a type back on the legacy boundary whose width "
                "differs between the two trees: ${WIDTH_HAZARD}")
        endif()
    endforeach()
endforeach()

# Poco's own copies are shims onto the canonical header, not second layouts.
foreach(HEADER IN ITEMS "${POCO_LEGACY_LIBRARY}" "${POCO_LEGACY_REX}")
    file(READ "${HEADER}" HEADER_TEXT)
    foreach(DUPLICATE_DEFINITION
            "typedef struct popot"
            "typedef union pt_num"
            "typedef struct lib_proto"
            "typedef struct poco_lib"
            "typedef struct pocorex")
        string(FIND "${HEADER_TEXT}" "${DUPLICATE_DEFINITION}" FOUND_INDEX)
        if(NOT FOUND_INDEX EQUAL -1)
            message(FATAL_ERROR
                "${HEADER} still owns duplicate Poco ABI layout: ${DUPLICATE_DEFINITION}")
        endif()
    endforeach()
endforeach()

foreach(HEADER IN ITEMS "${ANIMATOR_LEGACY_LIBRARY}" "${ANIMATOR_LEGACY_REX}")
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

foreach(HEADER IN ITEMS "${ANIMATOR_LEGACY_REX}")
    file(READ "${HEADER}" HEADER_TEXT)
    foreach(DUPLICATE_DEFINITION
            "typedef struct pocorex_hdr"
            "typedef struct pocorex")
        string(FIND "${HEADER_TEXT}" "${DUPLICATE_DEFINITION}" FOUND_INDEX)
        if(NOT FOUND_INDEX EQUAL -1)
            message(FATAL_ERROR "${HEADER} still owns duplicate Poco compatibility declaration: ${DUPLICATE_DEFINITION}")
        endif()
    endforeach()
endforeach()

# The legacy compile_poco()/run_poco()/free_poco() entry points were retired.
# They must not reappear in Poco's headers or in an Animator compatibility shim.
foreach(HEADER IN ITEMS "${POCO_LEGACY_FACE}" "${POCO_LEGACY_LIBRARY}" "${POCO_LEGACY_REX}"
        "${ANIMATOR_LEGACY_LIBRARY}" "${ANIMATOR_LEGACY_REX}")
    file(READ "${HEADER}" HEADER_TEXT)
    foreach(RETIRED_DECLARATION
            "Errcode compile_poco("
            "Errcode run_poco("
            "void free_poco(")
        string(FIND "${HEADER_TEXT}" "${RETIRED_DECLARATION}" FOUND_INDEX)
        if(NOT FOUND_INDEX EQUAL -1)
            message(FATAL_ERROR
                "${HEADER} re-declares the retired legacy entry point: ${RETIRED_DECLARATION}")
        endif()
    endforeach()
endforeach()

if(EXISTS "${ANIMATOR_SOURCE_DIR}/src/inc/pocoface.h")
    message(FATAL_ERROR
        "src/inc/pocoface.h was retired with the legacy compile API and must not return")
endif()

# ---------------------------------------------------------------------------
# The Animator half of the extraction boundary.
#
# poco/test/verify-source-boundary.cmake proves nothing under poco/ reaches
# into the consumer tree.  These two scans prove the other direction, which is
# where the reach-throughs actually were: src/inc/pocolib.h and
# src/inc/pocorex.h used to #include "../../poco/src/...", stepping straight
# past the target boundary into Poco's private include directory.
# ---------------------------------------------------------------------------

set(_shim_findings)

function(_collect_headers root out_var)
    file(GLOB_RECURSE _headers LIST_DIRECTORIES FALSE "${root}/*.h")
    set(_kept)
    foreach(_header IN LISTS _headers)
        file(RELATIVE_PATH _relative "${root}" "${_header}")
        if(_relative MATCHES "^third_party/" OR
           _relative MATCHES "(^|/)(_build[^/]*|build|cmake-build[^/]*|_install[^/]*)/" OR
           _relative MATCHES "^thirdparty/")
            continue()
        endif()
        list(APPEND _kept "${_header}")
    endforeach()
    set(${out_var} "${_kept}" PARENT_SCOPE)
endfunction()

_collect_headers("${POCO_SOURCE_DIR}" _poco_headers)
_collect_headers("${ANIMATOR_SOURCE_DIR}/src" _animator_headers)

# 1. No Animator source may name a path inside poco/.  Poco's headers arrive
#    through Poco::poco as <poco/...>, or not at all.
get_filename_component(_poco_root "${POCO_SOURCE_DIR}" REALPATH)
file(GLOB_RECURSE _animator_sources LIST_DIRECTORIES FALSE
    "${ANIMATOR_SOURCE_DIR}/src/*.c"
    "${ANIMATOR_SOURCE_DIR}/src/*.h")
foreach(_source IN LISTS _animator_sources)
    file(RELATIVE_PATH _relative "${ANIMATOR_SOURCE_DIR}" "${_source}")
    file(READ "${_source}" _source_text)
    string(REGEX MATCHALL "#[ \t]*include[ \t]*\"[^\"]*\"" _includes "${_source_text}")
    get_filename_component(_source_dir "${_source}" DIRECTORY)
    foreach(_include IN LISTS _includes)
        string(REGEX REPLACE "^#[ \t]*include[ \t]*\"([^\"]*)\"$" "\\1" _header "${_include}")
        if(NOT _header MATCHES "\\.\\.")
            continue()
        endif()
        get_filename_component(_resolved "${_source_dir}/${_header}" ABSOLUTE)
        string(FIND "${_resolved}" "${_poco_root}/" _inside_poco)
        if(_inside_poco EQUAL 0)
            list(APPEND _shim_findings
                "${_relative}: reaches through the target boundary into Poco's tree: ${_header}")
        endif()
    endforeach()
endforeach()

# 2. No include guard may be spelled the same way in both trees.  Two files
#    that share a guard do not coexist: whichever the compiler sees first
#    silently suppresses the other, which is how Poco and Animator ended up
#    with two Dlnode layouts, two sets of scalar typedefs and two commonst.h
#    files that nobody noticed were different.
function(_include_guard_of file out_var)
    file(READ "${file}" _text)
    # Strip comments first.  Every header here opens with a banner, and a
    # commented-out #ifndef/#define pair inside one would otherwise be read as
    # the file's guard - which would make this scan quietly compare the wrong
    # names.  The block-comment pattern is the non-greedy-free form, since
    # CMake regexes are always greedy.
    string(REGEX REPLACE "/\\*[^*]*\\*+([^/*][^*]*\\*+)*/" "" _text "${_text}")
    string(REGEX REPLACE "//[^\n]*" "" _text "${_text}")
    string(REGEX MATCH
        "#[ \t]*ifndef[ \t]+([A-Za-z_][A-Za-z0-9_]*)[ \t]*[\r\n]+[ \t]*#[ \t]*define[ \t]+([A-Za-z_][A-Za-z0-9_]*)"
        _matched "${_text}")
    if("${_matched}" STREQUAL "" OR NOT "${CMAKE_MATCH_1}" STREQUAL "${CMAKE_MATCH_2}")
        set(${out_var} "" PARENT_SCOPE)
    else()
        set(${out_var} "${CMAKE_MATCH_1}" PARENT_SCOPE)
    endif()
endfunction()

set(_poco_guards)
foreach(_header IN LISTS _poco_headers)
    _include_guard_of("${_header}" _guard)
    if(NOT "${_guard}" STREQUAL "")
        list(APPEND _poco_guards "${_guard}")
        set(_poco_guard_owner_${_guard} "${_header}")
    endif()
endforeach()

foreach(_header IN LISTS _animator_headers)
    _include_guard_of("${_header}" _guard)
    if("${_guard}" STREQUAL "")
        continue()
    endif()
    list(FIND _poco_guards "${_guard}" _clash)
    if(NOT _clash EQUAL -1)
        file(RELATIVE_PATH _animator_relative "${ANIMATOR_SOURCE_DIR}" "${_header}")
        file(RELATIVE_PATH _poco_relative "${ANIMATOR_SOURCE_DIR}"
            "${_poco_guard_owner_${_guard}}")
        list(APPEND _shim_findings
            "include guard ${_guard} is used by both trees: ${_poco_relative} and ${_animator_relative}")
    endif()
endforeach()

if(_shim_findings)
    list(REMOVE_DUPLICATES _shim_findings)
    list(JOIN _shim_findings "\n  " _shim_report)
    message(FATAL_ERROR "Animator/Poco header boundary findings:\n  ${_shim_report}")
endif()

list(LENGTH _poco_headers _poco_header_count)
list(LENGTH _animator_headers _animator_header_count)
message(STATUS
    "Animator/Poco header boundary passed: no Animator source reaches into poco/, "
    "and no include guard is shared across the two trees "
    "(${_poco_header_count} Poco and ${_animator_header_count} Animator headers scanned).")
