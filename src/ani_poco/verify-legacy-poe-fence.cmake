cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

foreach(required_variable POCO_SOURCE_DIR ANIMATOR_SOURCE_DIR)
    if(NOT DEFINED ${required_variable})
        message(FATAL_ERROR "${required_variable} is required")
    endif()
endforeach()

function(require_literal path literal)
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "Required fence evidence is missing: ${path}")
    endif()
    file(READ "${path}" content)
    string(FIND "${content}" "${literal}" found_at)
    if(found_at EQUAL -1)
        message(FATAL_ERROR "Missing fence evidence '${literal}' in ${path}")
    endif()
endfunction()

function(require_absent_literal path literal)
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "Required fence evidence is missing: ${path}")
    endif()
    file(READ "${path}" content)
    string(FIND "${content}" "${literal}" found_at)
    if(NOT found_at EQUAL -1)
        message(FATAL_ERROR "Generic Poco surface leaked '${literal}' through ${path}")
    endif()
endfunction()

set(POCO_PUBLIC_HEADER "${POCO_SOURCE_DIR}/include/poco/poco.h")
set(POCO_LEGACY_HEADER "${POCO_SOURCE_DIR}/src/pocolib.h")
set(ANIMATOR_POE_HEADER "${ANIMATOR_SOURCE_DIR}/src/inc/pocolib.h")
set(POCO_LOADER "${POCO_SOURCE_DIR}/src/pocoload.c")
set(POCO_MODULE_HELPER "${POCO_SOURCE_DIR}/cmake/PocoModule.cmake")
set(ANI_POLICY_HOST "${ANIMATOR_SOURCE_DIR}/src/poekit/colorutl/colorutl_test_host.c")
set(ANI_ADAPTER_POLICY "${ANIMATOR_SOURCE_DIR}/src/ani_poco/module_policy.c")
set(GENERIC_HELLO_MODULE "${POCO_SOURCE_DIR}/examples/hello/hello.c")

require_literal("${ANIMATOR_POE_HEADER}" "Animator-only native-POE function-table ABI")
require_literal("${ANIMATOR_POE_HEADER}" "PolibUser")
foreach(entry plprintf plQtext plQchoice plQquestion plQerror plUdQnumber)
    require_literal("${ANIMATOR_POE_HEADER}" "${entry}")
endforeach()

foreach(forbidden PolibUser _plptr _a_a_pocolib)
    require_absent_literal("${POCO_PUBLIC_HEADER}" "${forbidden}")
    require_absent_literal("${POCO_LEGACY_HEADER}" "${forbidden}")
endforeach()

require_literal("${POCO_PUBLIC_HEADER}" "allow_legacy_poe")
#
# The fence evidence below is deliberately host-neutral.  Poco's public header
# documents the legacy-POE opt-in without naming Animator: the opt-in must be
# described as exposing no host symbols or function tables, whoever the host
# is.  Requiring Animator's name here would make this audit the reason poco/
# still mentions Animator.
require_literal("${POCO_PUBLIC_HEADER}" "exposes no host symbols or function tables")
require_absent_literal("${POCO_PUBLIC_HEADER}" "Animator")
require_literal("${POCO_LOADER}" "allow_legacy_poe")
require_literal("${POCO_LOADER}" "legacy native-POE module")
require_literal("${POCO_MODULE_HELPER}" "no host include paths")
require_absent_literal("${POCO_MODULE_HELPER}" "Animator")
require_literal("${ANI_ADAPTER_POLICY}" ".allow_legacy_poe = 1")
require_literal("${ANI_ADAPTER_POLICY}" "animhost_ensure_pocolib")
require_absent_literal("${ANI_POLICY_HOST}" "animhost_ensure_pocolib")
require_literal("${GENERIC_HELLO_MODULE}" "PocoModuleDescriptor")
require_absent_literal("${GENERIC_HELLO_MODULE}" "pocorex.h")
require_absent_literal("${GENERIC_HELLO_MODULE}" "pocolib.h")

foreach(module_source IN ITEMS
        "pstamp/pstamp.c;poeQerror")
    set(module_parts ${module_source})
    list(GET module_parts 0 relative_path)
    list(GET module_parts 1 required_evidence)
    set(source "${ANIMATOR_SOURCE_DIR}/src/poekit/${relative_path}")
    require_literal("${source}" "${required_evidence}")
endforeach()
