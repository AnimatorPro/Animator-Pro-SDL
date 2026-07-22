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
    if(_details_length GREATER 1600)
        string(SUBSTRING "${_details}" 0 1600 _details)
        string(APPEND _details "\n[output truncated]")
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

macro(verify_installed_surface _install_prefix)
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
            "Poco install is missing the bundled shared-library artifact")
    endif()

    file(GLOB _private_dependency_artifacts LIST_DIRECTORIES FALSE
        "${_install_prefix}/lib/*ffi*"
        "${_install_prefix}/lib/*hashmap*"
        "${_install_prefix}/lib64/*ffi*"
        "${_install_prefix}/lib64/*hashmap*"
        "${_install_prefix}/bin/*ffi*"
        "${_install_prefix}/bin/*hashmap*")
    if(NOT "${_private_dependency_artifacts}" STREQUAL "")
        list(JOIN _private_dependency_artifacts "\n    " _dependency_report)
        list(APPEND _gate_failures
            "Poco install leaked private libffi/hashmap artifacts:\n    ${_dependency_report}")
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
        string(FIND "${_package_target_contents}" "add_library(Poco::poco SHARED IMPORTED)" _shared_import_offset)
        if(_shared_import_offset EQUAL -1)
            list(APPEND _gate_failures
                "Poco package must export Poco::poco as the bundled shared library")
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

set(_copied_poco_source "${_sandbox}/poco")
file(COPY "${POCO_SOURCE_DIR}" DESTINATION "${_sandbox}")

set(_install_prefix "${_sandbox}/install")

if(DEFINED TEST_GENERATOR AND NOT "${TEST_GENERATOR}" STREQUAL "")
    set(_generator_args -G "${TEST_GENERATOR}")
else()
    set(_generator_args)
endif()

# Reject parent-owned dependencies and Animator paths from the standalone
# CMake surface.  PocoModule is the sole installed helper; the legacy poekit
# helper remains outside the standalone package.
set(_cmake_files
    "${POCO_SOURCE_DIR}/CMakeLists.txt"
    "${POCO_SOURCE_DIR}/cmake/PocoModule.cmake"
)

foreach(_cmake_file IN LISTS _cmake_files)
    if(NOT EXISTS "${_cmake_file}")
        continue()
    endif()
    file(READ "${_cmake_file}" _cmake_contents)

    foreach(_target IN ITEMS ffi_static hashmap trdutil)
        string(REGEX MATCH
            "target_link_libraries[ \t\r\n]*\\([^\\)]*([ \t\r\n])${_target}([ \t\r\n\\)])"
            _target_match
            "${_cmake_contents}")
        if(NOT "${_target_match}" STREQUAL "")
            list(APPEND _gate_failures
                "Poco CMake links the parent-owned target '${_target}' in ${_cmake_file}")
        endif()
    endforeach()

    string(REGEX MATCH
        "CMAKE_SOURCE_DIR[^\n\r]*(src/inc|poco/include)"
        _animator_path_match
        "${_cmake_contents}")
    if(NOT "${_animator_path_match}" STREQUAL "")
        list(APPEND _gate_failures
            "Poco CMake reaches through the consumer source root in ${_cmake_file}: ${_animator_path_match}")
    endif()
endforeach()

# Keep the fixture contract deliberately narrow.  Both consumers must use the
# public API to register one native function and run a script, rather than
# merely compiling a header-only smoke executable.
verify_consumer_contract("add_subdirectory consumer" "${CMAKE_CURRENT_LIST_DIR}/consumer"
    poco_external_consumer TRUE)
verify_consumer_contract("package-config consumer" "${CMAKE_CURRENT_LIST_DIR}/package_consumer"
    poco_package_consumer FALSE)

# First configure/build Poco by itself.  This must never use the parent build
# directory; only the copied poco/ tree is available to it.  The installed
# package deliverable is the self-contained shared library, so build it shared
# here; the add_subdirectory consumer below exercises the default static embed.
configure_and_build(
    "copied Poco tree"
    "${_copied_poco_source}"
    "${_sandbox}/poco-build"
    "-DCMAKE_INSTALL_PREFIX=${_install_prefix}"
    "-DCMAKE_BUILD_TYPE=Release"
    "-DPOCO_BUILD_SHARED=ON"
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
verify_installed_surface("${_install_prefix}")

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
