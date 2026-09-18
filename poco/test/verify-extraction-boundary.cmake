cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

# This gate deliberately exercises both supported consumption modes.  The
# copied source tree must configure, build, install, and test independently;
# external consumers must use either add_subdirectory() or find_package() and
# interact with Poco only through <poco/poco.h> and Poco::poco.

foreach(_required_var IN ITEMS POCO_SOURCE_DIR)
    if(NOT DEFINED ${_required_var} OR "${${_required_var}}" STREQUAL "")
        message(FATAL_ERROR "${_required_var} must be set")
    endif()
endforeach()

if(NOT EXISTS "${POCO_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "POCO_SOURCE_DIR is not a Poco source directory: ${POCO_SOURCE_DIR}")
endif()
get_filename_component(POCO_SOURCE_DIR "${POCO_SOURCE_DIR}" REALPATH)

set(_gate_failures)

macro(record_command_failure _label _stage _result_var _stdout_var _stderr_var)
    set(_details "${${_stdout_var}}\n${${_stderr_var}}")
    string(STRIP "${_details}" _details)
    string(LENGTH "${_details}" _details_length)
    # Keep both ends.  A configure or build failure explains itself near the
    # top, but ctest names the tests that failed only in its closing summary,
    # and head-only truncation threw that away -- which made every failure of
    # the copied tree's suite report nothing but its first dozen passes.
    if(_details_length GREATER 4000)
        string(SUBSTRING "${_details}" 0 1200 _details_head)
        math(EXPR _details_tail_start "${_details_length} - 2800")
        string(SUBSTRING "${_details}" ${_details_tail_start} -1 _details_tail)
        set(_details "${_details_head}\n[... output elided ...]\n${_details_tail}")
    endif()
    string(REPLACE "\n" "\n    " _details "${_details}")
    list(APPEND _gate_failures
        "${_label} ${_stage} failed (${${_result_var}}):\n    ${_details}")
endmacro()

macro(configure_and_build _label _source_dir _build_dir)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" ${ARGN} -S "${_source_dir}" -B "${_build_dir}" ${_generator_args}
        RESULT_VARIABLE _configure_result
        OUTPUT_VARIABLE _configure_stdout
        ERROR_VARIABLE _configure_stderr
    )
    if(NOT "${_configure_result}" STREQUAL "0")
        record_command_failure("${_label}" "configure" _configure_result _configure_stdout _configure_stderr)
    else()
        execute_process(
            COMMAND "${CMAKE_COMMAND}" --build "${_build_dir}"
            RESULT_VARIABLE _build_result
            OUTPUT_VARIABLE _build_stdout
            ERROR_VARIABLE _build_stderr
        )
        if(NOT "${_build_result}" STREQUAL "0")
            record_command_failure("${_label}" "build" _build_result _build_stdout _build_stderr)
        endif()
    endif()
endmacro()

macro(run_built_consumer _label _build_dir _target)
    set(_consumer_executable "${_build_dir}/${_target}${CMAKE_EXECUTABLE_SUFFIX}")
    if(NOT EXISTS "${_consumer_executable}")
        list(APPEND _gate_failures
            "${_label} runtime executable was not produced: ${_consumer_executable}")
    else()
        execute_process(
            COMMAND "${_consumer_executable}"
            RESULT_VARIABLE _consumer_result
            OUTPUT_VARIABLE _consumer_stdout
            ERROR_VARIABLE _consumer_stderr
        )
        if(NOT "${_consumer_result}" STREQUAL "0")
            record_command_failure("${_label}" "run" _consumer_result _consumer_stdout _consumer_stderr)
        endif()
    endif()
endmacro()

macro(verify_consumer_contract _label _source_dir _target _requires_subdirectory)
    set(_consumer_cmake "${_source_dir}/CMakeLists.txt")
    set(_consumer_main "${_source_dir}/main.c")
    set(_consumer_script "${_source_dir}/consumer.poc")

    foreach(_consumer_file IN ITEMS "${_consumer_cmake}" "${_consumer_main}" "${_consumer_script}")
        if(NOT EXISTS "${_consumer_file}")
            list(APPEND _gate_failures "${_label} is missing required fixture file: ${_consumer_file}")
        endif()
    endforeach()

    if(EXISTS "${_consumer_cmake}")
        file(READ "${_consumer_cmake}" _consumer_cmake_contents)
        string(REGEX MATCH
            "target_link_libraries[ \t\r\n]*\\([ \t\r\n]*${_target}[ \t\r\n]+PRIVATE[ \t\r\n]+Poco::poco[ \t\r\n]*\\)"
            _only_poco_link
            "${_consumer_cmake_contents}")
        if("${_only_poco_link}" STREQUAL "")
            list(APPEND _gate_failures
                "${_label} must link ${_target} only to Poco::poco")
        endif()

        if(${_requires_subdirectory})
            string(REGEX MATCH "add_subdirectory[ \t\r\n]*\\(" _subdirectory_call "${_consumer_cmake_contents}")
            if("${_subdirectory_call}" STREQUAL "")
                list(APPEND _gate_failures "${_label} must consume Poco through add_subdirectory()")
            endif()
        else()
            string(REGEX MATCH "find_package[ \t\r\n]*\\([ \t\r\n]*Poco" _package_call "${_consumer_cmake_contents}")
            if("${_package_call}" STREQUAL "")
                list(APPEND _gate_failures "${_label} must consume Poco through find_package(Poco CONFIG)")
            endif()
        endif()
    endif()

    if(EXISTS "${_consumer_main}")
        file(READ "${_consumer_main}" _consumer_main_contents)
        string(REGEX MATCHALL "#[ \t]*include[^\n\r]*" _consumer_includes "${_consumer_main_contents}")
        list(LENGTH _consumer_includes _consumer_include_count)
        if(NOT _consumer_include_count EQUAL 1 OR
                NOT "${_consumer_includes}" MATCHES "^[ \t]*#[ \t]*include[ \t]*<poco/poco\\.h>[ \t]*$")
            list(APPEND _gate_failures
                "${_label} main.c must include only <poco/poco.h>")
        endif()

        foreach(_required_api IN ITEMS
                PocoLibrary
                poco_vm_create
                poco_vm_register_library
                poco_vm_compile_file
                poco_vm_run)
            string(FIND "${_consumer_main_contents}" "${_required_api}" _api_offset)
            if(_api_offset EQUAL -1)
                list(APPEND _gate_failures
                    "${_label} main.c does not exercise ${_required_api}")
            endif()
        endforeach()
    endif()

    if(EXISTS "${_consumer_script}")
        file(READ "${_consumer_script}" _consumer_script_contents)
        string(FIND "${_consumer_script_contents}" "ConsumerAnswer" _script_binding_offset)
        if(_script_binding_offset EQUAL -1)
            list(APPEND _gate_failures
                "${_label} script does not call its registered native binding")
        endif()
    endif()
endmacro()

macro(verify_installed_surface _install_prefix _installed_is_shared)
    set(_installed_include_dir "${_install_prefix}/include")
    set(_installed_public_header "${_installed_include_dir}/poco/poco.h")
    if(NOT EXISTS "${_installed_public_header}")
        list(APPEND _gate_failures
            "Poco install is missing its canonical public header: ${_installed_public_header}")
    endif()

    file(GLOB_RECURSE _installed_headers LIST_DIRECTORIES FALSE
        "${_installed_include_dir}/*.h"
        "${_installed_include_dir}/*.hpp")
    list(SORT _installed_headers)
    set(_expected_installed_headers "${_installed_public_header}")
    if(NOT "${_installed_headers}" STREQUAL "${_expected_installed_headers}")
        list(JOIN _installed_headers "\n    " _installed_header_report)
        list(APPEND _gate_failures
            "Poco install must publish only required public headers. Found:\n    ${_installed_header_report}")
    endif()

    file(GLOB _installed_poco_libraries LIST_DIRECTORIES FALSE
        "${_install_prefix}/lib/libpoco.*"
        "${_install_prefix}/lib64/libpoco.*"
        "${_install_prefix}/bin/poco.dll")
    if("${_installed_poco_libraries}" STREQUAL "")
        list(APPEND _gate_failures
            "Poco install is missing the bundled Poco library artifact")
    endif()

    # The private dependencies must not sit next to libpoco where a consumer
    # could find and name them.  A static install still has to ship them, but
    # only inside lib/poco, reached through Poco::poco and nowhere else.
    file(GLOB _private_dependency_artifacts LIST_DIRECTORIES FALSE
        "${_install_prefix}/lib/*ffi*"
        "${_install_prefix}/lib/*hashmap*"
        "${_install_prefix}/lib/*blake3*"
        "${_install_prefix}/lib64/*ffi*"
        "${_install_prefix}/lib64/*hashmap*"
        "${_install_prefix}/lib64/*blake3*"
        "${_install_prefix}/bin/*ffi*"
        "${_install_prefix}/bin/*hashmap*"
        "${_install_prefix}/bin/*blake3*")
    if(NOT "${_private_dependency_artifacts}" STREQUAL "")
        list(JOIN _private_dependency_artifacts "\n    " _dependency_report)
        list(APPEND _gate_failures
            "Poco install leaked private libffi/hashmap/blake3 artifacts:\n    ${_dependency_report}")
    endif()

    # A static install is only usable if those archives are actually present:
    # libpoco.a alone reaches the consumer with undefined ffi_*/hashmap_*/
    # blake3_* symbols.
    if(NOT _installed_is_shared)
        foreach(_private_name IN ITEMS poco_ffi poco_hashmap poco_blake3)
            file(GLOB _private_archive LIST_DIRECTORIES FALSE
                "${_install_prefix}/lib/poco/*${_private_name}*"
                "${_install_prefix}/lib64/poco/*${_private_name}*")
            if("${_private_archive}" STREQUAL "")
                list(APPEND _gate_failures
                    "Static Poco install does not ship its private ${_private_name} archive under lib/poco")
            endif()
        endforeach()
    endif()

    set(_imports_poco_artifact FALSE)
    file(GLOB_RECURSE _package_target_files LIST_DIRECTORIES FALSE
        "${_install_prefix}/PocoTargets.cmake")
    list(LENGTH _package_target_files _package_target_file_count)
    if(NOT _package_target_file_count EQUAL 1)
        list(APPEND _gate_failures
            "Poco install must contain exactly one exported PocoTargets.cmake file")
    else()
        list(GET _package_target_files 0 _package_target_file)
        file(READ "${_package_target_file}" _package_target_contents)
        string(REGEX MATCH "add_library\\(Poco::poco (STATIC|SHARED) IMPORTED\\)"
            _poco_import_declaration "${_package_target_contents}")
        if("${_poco_import_declaration}" STREQUAL "")
            list(APPEND _gate_failures
                "Poco package must export Poco::poco as an imported Poco library")
        endif()
        get_filename_component(_package_target_dir "${_package_target_file}" DIRECTORY)
        file(GLOB _package_target_config_files LIST_DIRECTORIES FALSE
            "${_package_target_dir}/PocoTargets-*.cmake")
        foreach(_package_target_config IN LISTS _package_target_config_files)
            file(READ "${_package_target_config}" _package_target_config_contents)
            string(REGEX MATCH "IMPORTED_(LOCATION|IMPLIB)[^\n\r]*poco" _poco_artifact_import
                "${_package_target_config_contents}")
            if(NOT "${_poco_artifact_import}" STREQUAL "")
                set(_imports_poco_artifact TRUE)
            endif()
        endforeach()
    endif()
    if(NOT _imports_poco_artifact)
        list(APPEND _gate_failures
            "Poco package target does not resolve to an installed Poco library artifact")
    endif()
endmacro()

# Do not configure a nested build in the Animator build tree.  The temporary
# directory contains only a copied poco/ tree and its bundled dependencies.
if(DEFINED ENV{TMPDIR} AND NOT "$ENV{TMPDIR}" STREQUAL "")
    set(_temporary_root "$ENV{TMPDIR}")
else()
    set(_temporary_root "/tmp")
endif()
string(SHA256 _sandbox_hash "${POCO_SOURCE_DIR};${CMAKE_CURRENT_LIST_FILE}")
string(SUBSTRING "${_sandbox_hash}" 0 16 _sandbox_suffix)
set(_sandbox "${_temporary_root}/poco-extraction-boundary-${_sandbox_suffix}")
file(REMOVE_RECURSE "${_sandbox}")
file(MAKE_DIRECTORY "${_sandbox}")
# TMPDIR is a symlink on macOS (/var -> /private/var).  Tests inside the copied
# tree compare paths they report against the paths they were given, and the
# tools resolve symlinks, so hand the sandbox on already resolved.
get_filename_component(_sandbox "${_sandbox}" REALPATH)

set(_copied_poco_source "${_sandbox}/poco")
file(COPY "${POCO_SOURCE_DIR}" DESTINATION "${_sandbox}")

set(_install_prefix "${_sandbox}/install")

if(DEFINED TEST_GENERATOR AND NOT "${TEST_GENERATOR}" STREQUAL "")
    set(_generator_args -G "${TEST_GENERATOR}")
else()
    set(_generator_args)
endif()

# Reject parent-owned dependencies and consumer paths from the whole source
# tree, not just the top-level CMake surface: poekit/ and test/ reach-throughs
# escaped the two-file scan this gate used to run.  The scan is its own script
# so it can also run as a cheap standalone test.
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DPOCO_SOURCE_DIR=${POCO_SOURCE_DIR}"
        -P "${CMAKE_CURRENT_LIST_DIR}/verify-source-boundary.cmake"
    RESULT_VARIABLE _boundary_result
    OUTPUT_VARIABLE _boundary_stdout
    ERROR_VARIABLE _boundary_stderr
)
if(NOT "${_boundary_result}" STREQUAL "0")
    record_command_failure("Poco source tree" "boundary scan"
        _boundary_result _boundary_stdout _boundary_stderr)
endif()

# Keep the fixture contract deliberately narrow.  Both consumers must use the
# public API to register one native function and run a script, rather than
# merely compiling a header-only smoke executable.
verify_consumer_contract("add_subdirectory consumer" "${CMAKE_CURRENT_LIST_DIR}/consumer"
    poco_external_consumer TRUE)
verify_consumer_contract("package-config consumer" "${CMAKE_CURRENT_LIST_DIR}/package_consumer"
    poco_package_consumer FALSE)

# First configure/build Poco by itself.  This must never use the parent build
# directory; only the copied poco/ tree is available to it.  Pass no linkage
# option: the install path a third party actually gets by default is the static
# one, so that is what this gate has to prove works end to end.
configure_and_build(
    "copied Poco tree"
    "${_copied_poco_source}"
    "${_sandbox}/poco-build"
    "-DCMAKE_INSTALL_PREFIX=${_install_prefix}"
    "-DCMAKE_BUILD_TYPE=Release"
)

# Then configure/build a minimal third-party project.  Its source contains
# only add_subdirectory, Poco::poco, and <poco/poco.h> as the Poco contract.
set(_consumer_source "${_sandbox}/consumer")
file(COPY "${CMAKE_CURRENT_LIST_DIR}/consumer/" DESTINATION "${_consumer_source}")
configure_and_build(
    "external Poco consumer"
    "${_consumer_source}"
    "${_sandbox}/consumer-build"
    "-DPOCO_SOURCE_DIR=${_copied_poco_source}"
)
run_built_consumer("external Poco consumer" "${_sandbox}/consumer-build" poco_external_consumer)

# Installation is part of the public CMake contract.  Configure separately so
# a parent build's install prefix or target namespace cannot affect the result.
execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${_sandbox}/poco-build"
    RESULT_VARIABLE _install_result
    OUTPUT_VARIABLE _install_stdout
    ERROR_VARIABLE _install_stderr
)
if(NOT "${_install_result}" STREQUAL "0")
    record_command_failure("copied Poco tree" "install" _install_result _install_stdout _install_stderr)
endif()
verify_installed_surface("${_install_prefix}" FALSE)

# A package consumer names only Poco::poco in target_link_libraries().
set(_package_consumer_source "${_sandbox}/package-consumer")
file(COPY "${CMAKE_CURRENT_LIST_DIR}/package_consumer/" DESTINATION "${_package_consumer_source}")
configure_and_build(
    "installed Poco package consumer"
    "${_package_consumer_source}"
    "${_sandbox}/package-consumer-build"
    "-DCMAKE_PREFIX_PATH=${_install_prefix}"
)
run_built_consumer("installed Poco package consumer" "${_sandbox}/package-consumer-build" poco_package_consumer)

# The copied source tree must run its complete independent CTest suite after
# installation, not merely configure and compile as a library-only project.
# CTestTestfile.cmake proves Poco enables testing when it is top-level.
set(_copied_ctest_file "${_sandbox}/poco-build/CTestTestfile.cmake")
if(NOT EXISTS "${_copied_ctest_file}")
    list(APPEND _gate_failures
        "copied Poco tree did not enable CTest: ${_copied_ctest_file} is missing")
else()
    get_filename_component(_cmake_program_dir "${CMAKE_COMMAND}" DIRECTORY)
    find_program(_ctest_command NAMES ctest HINTS "${_cmake_program_dir}")
    if(NOT _ctest_command)
        list(APPEND _gate_failures "could not find ctest to run the copied Poco tree")
    else()
        execute_process(
            COMMAND "${_ctest_command}" --test-dir "${_sandbox}/poco-build" --output-on-failure
                -E "^poco_extraction_boundary$"
            RESULT_VARIABLE _ctest_result
            OUTPUT_VARIABLE _ctest_stdout
            ERROR_VARIABLE _ctest_stderr
        )
        if(NOT "${_ctest_result}" STREQUAL "0")
            record_command_failure("copied Poco tree" "test" _ctest_result _ctest_stdout _ctest_stderr)
        endif()
    endif()
endif()

list(JOIN _gate_failures "\n\n" _failure_report)

if(_gate_failures)
    message(FATAL_ERROR "Poco extraction boundary findings:\n${_failure_report}")
else()
    message(STATUS "Poco extraction boundary passed: copied Poco configured, built, installed, and tested; both external consumers registered a native library and ran a script without Animator Pro.")
endif()
