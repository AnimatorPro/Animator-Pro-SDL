cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

foreach(required_variable POCO_SOURCE_DIR ANIMATOR_SOURCE_DIR MANIFEST)
    if(NOT DEFINED ${required_variable})
        message(FATAL_ERROR "${required_variable} is required")
    endif()
endforeach()

function(require_file path)
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "Required baseline evidence is missing: ${path}")
    endif()
endfunction()

function(require_literal path literal)
    require_file("${path}")
    file(READ "${path}" content)
    string(FIND "${content}" "${literal}" found_at)
    if(found_at EQUAL -1)
        message(FATAL_ERROR "Missing baseline evidence '${literal}' in ${path}")
    endif()
endfunction()

function(require_absent_literal path literal)
    require_file("${path}")
    file(READ "${path}" content)
    string(FIND "${content}" "${literal}" found_at)
    if(NOT found_at EQUAL -1)
        message(FATAL_ERROR "Obsolete callback evidence '${literal}' remains in ${path}")
    endif()
endfunction()

# The manifest is deliberately simple JSON so it remains a reviewable snapshot
# without requiring a new runtime dependency. These checks protect the minimum
# classification data and establish that the cited legacy paths still describe
# the pre-refactor arrangement.
require_file("${MANIFEST}")
foreach(marker
    "\"schema_version\": 2"
    "\"live_standard_varargs\""
    "\"removed_dummy_catalog\""
    "\"c_varargs_callbacks_into_poco\""
    "\"native_poe_compatibility\""
    "\"behavior_fixtures\"")
    require_literal("${MANIFEST}" "${marker}")
endforeach()

foreach(catalog_marker
    "Poco_lib po_main_lib"
    "Poco_lib po_str_lib"
    "Poco_lib po_FILE_lib"
    "Poco_lib po_mem_lib"
    "Poco_lib po_math_lib"
    "Poco_lib po_dos_standalone_lib"
    "Poco_lib po_user_lib"
    "Poco_lib po_draw_lib"
    "Poco_lib po_text_lib"
    "Poco_lib po_mode_lib"
    "Poco_lib po_turtle_lib"
    "Poco_lib po_time_lib"
    "Poco_lib po_cel_lib"
    "Poco_lib po_alt_lib"
    "Poco_lib po_optics_lib"
    "Poco_lib po_blit_lib"
    "Poco_lib po_misc_lib"
    "Poco_lib po_load_save_lib"
    "Poco_lib po_dos_lib"
    "Poco_lib po_globalv_lib"
    "Poco_lib po_title_lib"
    "Poco_lib po_tween_lib"
    "Poco_lib po_flicplay_lib"
    "Poco_lib po_picdrive_lib")
    require_literal("${MANIFEST}" "${catalog_marker}")
endforeach()

set(POCO_MAIN "${POCO_SOURCE_DIR}/src/main.c")
set(POCO_FILE "${POCO_SOURCE_DIR}/src/safefile.c")
set(POCO_STRING "${POCO_SOURCE_DIR}/src/strlib.c")
set(ANIMATOR_LIBS "${ANIMATOR_SOURCE_DIR}/src/pocolibs.c")
set(POCO_USER "${ANIMATOR_SOURCE_DIR}/src/pocouser.c")
set(POCO_QNUM "${ANIMATOR_SOURCE_DIR}/src/pocoqnum.c")
set(POCO_TIME "${ANIMATOR_SOURCE_DIR}/src/pocotime.c")
set(POCO_FLIC "${ANIMATOR_SOURCE_DIR}/src/pocoflic.c")
set(POLIB "${ANIMATOR_SOURCE_DIR}/src/inc/pocolib.h")

foreach(evidence
    "Poco Library"
    "int printf(char *format, ...)"
    "void Qtext(char *format, ...)"
    "po_qtext")
    require_literal("${POCO_MAIN}" "${evidence}")
endforeach()
foreach(evidence
    "int     fprintf(FILE *f, char *format, ...)"
    "po_fprintf")
    require_literal("${POCO_FILE}" "${evidence}")
endforeach()
foreach(evidence
    "int     sprintf(char *buf, char *format, ...)"
    "po_sprintf")
    require_literal("${POCO_STRING}" "${evidence}")
endforeach()

foreach(evidence
    "&po_user_lib"
    "&po_picdrive_lib"
    "&po_FILE_lib"
    "&po_str_lib"
    "&po_mem_lib"
    "&po_math_lib")
    require_literal("${ANIMATOR_LIBS}" "${evidence}")
endforeach()
foreach(evidence
    "int     printf(char *format, ...)"
    "void    Qtext(char *format, ...)"
    "int     Qchoice(char **buttons, int bcount, char *header, ...)"
    "Boolean Qquestion(char *question, ...)"
    "ErrCode Qerror(ErrCode err, char *format, ...)"
    "Boolean UdQnumber(int *num, int min, int max,")
    require_literal("${POCO_USER}" "${evidence}")
endforeach()

foreach(callback_file ${POCO_USER} ${POCO_QNUM} ${POCO_TIME} ${POCO_FLIC})
    require_literal("${callback_file}" "poco_invoke_callback(")
    require_absent_literal("${callback_file}" "poco_cont_ops(")
endforeach()
require_absent_literal("${POCO_SOURCE_DIR}/src/runops.c" "poco_cont_ops(")
foreach(vtable_entry plprintf plQtext plQchoice plQquestion plQerror plUdQnumber)
    require_literal("${POLIB}" "${vtable_entry}")
endforeach()

foreach(poe_source
    "${POCO_SOURCE_DIR}/poekit/flicplay/flicplay.c"
    "${POCO_SOURCE_DIR}/poekit/pstamp/pstamp.c"
    "${POCO_SOURCE_DIR}/poekit/eco/eco.c"
    "${POCO_SOURCE_DIR}/poekit/lookup/lookup.c"
    "${POCO_SOURCE_DIR}/poekit/otdemo/otdemo.c")
    require_file("${poe_source}")
endforeach()
require_literal("${POCO_SOURCE_DIR}/poekit/CMakeLists.txt" "add_subdirectory(pstamp)")

foreach(fixture
    "${POCO_SOURCE_DIR}/test/pos/sprintf_variadic.poc"
    "${POCO_SOURCE_DIR}/test/pos/pointer_return.poc"
    "${POCO_SOURCE_DIR}/test/neg/undefined_var.poc"
    "${POCO_SOURCE_DIR}/test/ATOF.POC")
    require_file("${fixture}")
endforeach()
