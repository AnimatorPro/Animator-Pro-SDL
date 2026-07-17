if(NOT DEFINED BASELINE_MANIFEST OR NOT EXISTS "${BASELINE_MANIFEST}")
    message(FATAL_ERROR "BASELINE_MANIFEST must name the Phase 1 binding baseline")
endif()
foreach(required_variable BUILD_DIR INSTALL_PREFIX ANI_EXECUTABLE
        ANI_REGISTRATION_EXECUTABLE POCO_EXECUTABLE)
    if(NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
        message(FATAL_ERROR "${required_variable} is required")
    endif()
endforeach()

function(run_checked label)
    execute_process(
        COMMAND ${ARGN}
        RESULT_VARIABLE command_result
        OUTPUT_VARIABLE command_stdout
        ERROR_VARIABLE command_stderr
    )
    if(NOT command_result EQUAL 0)
        message(FATAL_ERROR
            "${label} failed with exit ${command_result}:\n${command_stdout}${command_stderr}")
    endif()
endfunction()

function(run_checked_in label working_directory)
    execute_process(
        COMMAND ${ARGN}
        WORKING_DIRECTORY "${working_directory}"
        RESULT_VARIABLE command_result
        OUTPUT_VARIABLE command_stdout
        ERROR_VARIABLE command_stderr
    )
    if(NOT command_result EQUAL 0)
        message(FATAL_ERROR
            "${label} failed with exit ${command_result}:\n${command_stdout}${command_stderr}")
    endif()
endfunction()

function(require_file label path)
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "${label} is missing: ${path}")
    endif()
endfunction()

function(compare_inventory inventory_file baseline_file)
    file(READ "${baseline_file}" baseline_json)
    string(JSON expected_count LENGTH "${baseline_json}" catalogs animator)
    file(STRINGS "${inventory_file}" actual_inventory)
    list(LENGTH actual_inventory actual_count)
    if(NOT actual_count EQUAL expected_count)
        message(FATAL_ERROR
            "Animator library inventory count differs from the Phase 1 baseline: "
            "expected ${expected_count}, got ${actual_count}")
    endif()

    set(expected_inventory)
    math(EXPR last_index "${expected_count} - 1")
    foreach(index RANGE ${last_index})
        string(JSON expected_name GET "${baseline_json}" catalogs animator ${index} library)
        list(APPEND expected_inventory "${expected_name}")
    endforeach()

    # Registration order is an implementation detail: the adapter groups the
    # shared standard libraries with the conversion code.  This gate
    # protects the complete category set and rejects a duplicate standing in
    # for a missing category.
    list(SORT expected_inventory)
    list(SORT actual_inventory)
    set(actual_unique_inventory ${actual_inventory})
    list(REMOVE_DUPLICATES actual_unique_inventory)
    list(LENGTH actual_unique_inventory actual_unique_count)
    if(NOT actual_unique_count EQUAL expected_count OR
            NOT actual_inventory STREQUAL expected_inventory)
        list(JOIN expected_inventory ", " expected_report)
        list(JOIN actual_inventory ", " actual_report)
        message(FATAL_ERROR
            "Animator library inventory differs from the Phase 1 baseline.\n"
            "Expected: ${expected_report}\nActual: ${actual_report}")
    endif()
endfunction()

# The configured build and its installed artifacts must be the Poco-enabled
# Animator variant.  `pixi run test` builds and installs this tree before CTest
# runs; a separate CTest dependency builds Animator with Poco disabled.
set(cache_file "${BUILD_DIR}/CMakeCache.txt")
require_file("Animator CMake cache" "${cache_file}")
file(READ "${cache_file}" cache_contents)
foreach(required_option WITH_ANI WITH_POCO)
    string(REGEX MATCH "${required_option}:BOOL=(ON|1)" option_enabled
        "${cache_contents}")
    if(option_enabled STREQUAL "")
        message(FATAL_ERROR
            "Animator verification requires ${required_option}=ON in ${cache_file}")
    endif()
endforeach()
require_file("built Animator executable" "${ANI_EXECUTABLE}")
require_file("Animator registration fixture" "${ANI_REGISTRATION_EXECUTABLE}")
require_file("installed Poco executable" "${POCO_EXECUTABLE}")

# The fixture compiles an API from every registered Animator category and runs
# GetAbort() in the minimal runtime.  Its generated category inventory must
# remain identical to the baseline manifest.
set(inventory_file "${BUILD_DIR}/animator-library-inventory.txt")
run_checked("Animator library inventory"
    "${ANI_REGISTRATION_EXECUTABLE}" --write-inventory "${inventory_file}")
require_file("generated Animator library inventory" "${inventory_file}")
compare_inventory("${inventory_file}" "${BASELINE_MANIFEST}")
run_checked("Animator representative binding script"
    "${ANI_REGISTRATION_EXECUTABLE}")

# Generic modules live with Poco test modules, while Animator-native modules
# are installed in the Animator resource directory.  Load one of each class
# from its installed location and also require the second retained Ani module.
set(generic_module "${INSTALL_PREFIX}/tests/hello.poe")
set(generic_script "${INSTALL_PREFIX}/tests/hello.poc")
set(ani_module "${INSTALL_PREFIX}/resource/colorutl.poe")
set(ani_script "${INSTALL_PREFIX}/tests/COLTEST.POC")
set(ani_second_module "${INSTALL_PREFIX}/resource/pstamp.poe")
foreach(required_pair
    "generic Poco module|${generic_module}"
    "generic Poco module script|${generic_script}"
    "Animator-native module|${ani_module}"
    "Animator-native module script|${ani_script}"
    "second Animator-native module|${ani_second_module}")
    string(REPLACE "|" ";" pair "${required_pair}")
    list(GET pair 0 label)
    list(GET pair 1 path)
    require_file("${label}" "${path}")
endforeach()
run_checked_in("installed generic module load" "${INSTALL_PREFIX}"
    "${POCO_EXECUTABLE}" "${generic_script}")
run_checked_in("installed Animator-native module load" "${INSTALL_PREFIX}"
    "${ANI_REGISTRATION_EXECUTABLE}" "${ani_script}")

message(STATUS
    "Animator Poco verification passed: representative bindings, separate "
    "generic/Ani module locations and loads, and Phase 1 inventory.")
