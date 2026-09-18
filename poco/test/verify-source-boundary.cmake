cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

# Static half of the extraction boundary.  The full gate (see
# verify-extraction-boundary.cmake) copies, configures, builds, installs and
# tests the tree, which is far too expensive to leave on by default.  This
# scan costs a few file reads, so it runs unconditionally and covers the whole
# poco/ tree rather than the two CMake files the full gate used to read.
#
# The rule it enforces: no file under poco/ may name a target, a variable or a
# path that only exists in the consumer's source tree.

if(NOT DEFINED POCO_SOURCE_DIR OR "${POCO_SOURCE_DIR}" STREQUAL "")
    message(FATAL_ERROR "POCO_SOURCE_DIR must be set")
endif()
if(NOT EXISTS "${POCO_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "POCO_SOURCE_DIR is not a Poco source directory: ${POCO_SOURCE_DIR}")
endif()
get_filename_component(POCO_SOURCE_DIR "${POCO_SOURCE_DIR}" REALPATH)

# Files whose reach-throughs are already tracked as outstanding work.  Each
# entry must still be violating something: an entry that has gone clean is
# reported as stale so the exception list cannot outlive its reason.
#
# poekit/ modules reach into the parent for the Animator POE helper and for
# Animator's include directory.  Removing that is the relocate-poekit task,
# which moves the Animator modules beside src/ani_poco and hello/ into
# poco/examples.  Delete these entries when it lands.
set(_known_pending
    "poekit/colorutl/CMakeLists.txt"
    "poekit/pstamp/CMakeLists.txt"
    "poekit/hello/CMakeLists.txt"
    "poekit/test_libs/badver/CMakeLists.txt"
    "poekit/test_libs/empty/CMakeLists.txt"
    "poekit/test_libs/noentry/CMakeLists.txt"
    "poekit/test_libs/nullret/CMakeLists.txt"
)

# The two boundary scanners quote the very spellings they forbid.
set(_self_excluded
    "test/verify-source-boundary.cmake"
    "test/verify-extraction-boundary.cmake"
)

set(_findings)
set(_pending_hits)

function(_relative_to_poco absolute out_var)
    file(RELATIVE_PATH _relative "${POCO_SOURCE_DIR}" "${absolute}")
    set(${out_var} "${_relative}" PARENT_SCOPE)
endfunction()

# Resolve a path written inside a CMake file against that file's directory and
# report it when it leaves the tree.  Literal "../.." spellings cannot be
# matched textually: poco/test/legacy_poe_fence reaches "../../src", which is
# poco's own src, while poco/test reaching "../.." is the consumer's root.
function(_check_escaping_paths file_path contents relative_name)
    get_filename_component(_file_dir "${file_path}" DIRECTORY)
    set(_local_findings)
    foreach(_anchor CMAKE_CURRENT_SOURCE_DIR CMAKE_CURRENT_LIST_DIR)
        string(REGEX MATCHALL "\\$\\{${_anchor}\\}/[^\"'\) \t\r\n]*" _uses "${contents}")
        foreach(_use IN LISTS _uses)
            string(REPLACE "\${${_anchor}}/" "" _suffix "${_use}")
            if(NOT _suffix MATCHES "\\.\\.")
                continue()
            endif()
            get_filename_component(_resolved "${_file_dir}/${_suffix}" ABSOLUTE)
            string(FIND "${_resolved}" "${POCO_SOURCE_DIR}/" _inside)
            if(NOT _inside EQUAL 0 AND NOT "${_resolved}" STREQUAL "${POCO_SOURCE_DIR}")
                list(APPEND _local_findings
                    "${relative_name}: path leaves the Poco tree: ${_use}")
            endif()
        endforeach()
    endforeach()
    set(_escape_findings "${_local_findings}" PARENT_SCOPE)
endfunction()

file(GLOB_RECURSE _cmake_files LIST_DIRECTORIES FALSE
    "${POCO_SOURCE_DIR}/CMakeLists.txt"
    "${POCO_SOURCE_DIR}/*.cmake"
    "${POCO_SOURCE_DIR}/*.cmake.in"
)
file(GLOB_RECURSE _source_files LIST_DIRECTORIES FALSE
    "${POCO_SOURCE_DIR}/*.c"
    "${POCO_SOURCE_DIR}/*.h"
)

foreach(_file IN LISTS _cmake_files _source_files)
    _relative_to_poco("${_file}" _relative)
    # third_party/ is vendored verbatim and build trees are not source.
    if(_relative MATCHES "^third_party/" OR
       _relative MATCHES "(^|/)(_build|build|cmake-build[^/]*)/")
        continue()
    endif()
    list(FIND _self_excluded "${_relative}" _self_excluded_index)
    if(NOT _self_excluded_index EQUAL -1)
        continue()
    endif()

    file(READ "${_file}" _contents)
    set(_file_findings)

    if(_file MATCHES "\\.(c|h)$")
        string(REGEX MATCHALL "#[ \t]*include[ \t]*\"[^\"]*\"" _includes "${_contents}")
        get_filename_component(_file_dir "${_file}" DIRECTORY)
        foreach(_include IN LISTS _includes)
            string(REGEX REPLACE "^#[ \t]*include[ \t]*\"([^\"]*)\"$" "\\1" _header "${_include}")
            if(NOT _header MATCHES "\\.\\.")
                continue()
            endif()
            get_filename_component(_resolved "${_file_dir}/${_header}" ABSOLUTE)
            string(FIND "${_resolved}" "${POCO_SOURCE_DIR}/" _inside)
            if(NOT _inside EQUAL 0 AND NOT "${_resolved}" STREQUAL "${POCO_SOURCE_DIR}")
                list(APPEND _file_findings
                    "${_relative}: include leaves the Poco tree: ${_header}")
            endif()
        endforeach()
    endif()

    # An absolute path to somebody's checkout is never legitimate in a source
    # file, and debugging instrumentation is how it gets there.
    string(REGEX MATCHALL "\"/(Users|home)/[^\"]*\"" _absolute_paths "${_contents}")
    foreach(_absolute_path IN LISTS _absolute_paths)
        list(APPEND _file_findings
            "${_relative}: hardcodes an absolute path from a developer checkout: ${_absolute_path}")
    endforeach()

    if(NOT _file MATCHES "\\.(c|h)$")
        # Targets the consumer defines.  Poco must build its own dependencies.
        foreach(_target ffi_static hashmap trdutil animhost ani_poco_adapter)
            string(REGEX MATCH
                "target_link_libraries[ \t\r\n]*\\([^\\)]*([ \t\r\n])${_target}([ \t\r\n\\)])"
                _target_match
                "${_contents}")
            if(NOT "${_target_match}" STREQUAL "")
                list(APPEND _file_findings
                    "${_relative}: links the consumer-owned target '${_target}'")
            endif()
        endforeach()

        # In a standalone tree these resolve to poco/ itself, so any path
        # appended to them is written for the Animator layout only.
        foreach(_variable CMAKE_SOURCE_DIR PROJECT_SOURCE_DIR)
            string(REGEX MATCH "\\$\\{${_variable}\\}/[^\"'\) \t\r\n]*" _use "${_contents}")
            if(NOT "${_use}" STREQUAL "")
                list(APPEND _file_findings
                    "${_relative}: reaches through the consumer source root: ${_use}")
            endif()
        endforeach()

        _check_escaping_paths("${_file}" "${_contents}" "${_relative}")
        list(APPEND _file_findings ${_escape_findings})
    endif()

    # Names that only mean something when Animator owns the build.
    foreach(_identifier ANIMATOR_SOURCE_DIR POCO_TEST_HAS_ANIMATOR_SOURCE)
        string(FIND "${_contents}" "${_identifier}" _identifier_index)
        if(NOT _identifier_index EQUAL -1)
            list(APPEND _file_findings
                "${_relative}: names the Animator-only identifier '${_identifier}'")
        endif()
    endforeach()

    if(_file_findings)
        list(FIND _known_pending "${_relative}" _pending_index)
        if(_pending_index EQUAL -1)
            list(APPEND _findings ${_file_findings})
        else()
            list(APPEND _pending_hits "${_relative}")
        endif()
    endif()
endforeach()

foreach(_pending IN LISTS _known_pending)
    list(FIND _pending_hits "${_pending}" _hit_index)
    if(_hit_index EQUAL -1)
        list(APPEND _findings
            "${_pending} is listed as a known pending reach-through but is now clean; remove it from _known_pending in this file")
    endif()
endforeach()

if(_findings)
    list(REMOVE_DUPLICATES _findings)
    list(JOIN _findings "\n  " _report)
    message(FATAL_ERROR "Poco source boundary findings:\n  ${_report}")
endif()

list(LENGTH _cmake_files _cmake_count)
list(LENGTH _source_files _source_count)
list(LENGTH _pending_hits _pending_count)
message(STATUS
    "Poco source boundary passed: no untracked file under poco/ reaches into the consumer tree "
    "(${_cmake_count} CMake and ${_source_count} C files scanned; "
    "${_pending_count} files still tracked as pending relocate-poekit).")
