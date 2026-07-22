cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

foreach(required_variable POCO_SOURCE_DIR ANIMATOR_SOURCE_DIR POCO_EXECUTABLE)
    if(NOT DEFINED ${required_variable})
        message(FATAL_ERROR "${required_variable} is required")
    endif()
endforeach()

function(require_literal path literal)
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "Required variadic-binding evidence is missing: ${path}")
    endif()
    file(READ "${path}" content)
    string(FIND "${content}" "${literal}" found_at)
    if(found_at EQUAL -1)
        message(FATAL_ERROR "Missing variadic-binding evidence '${literal}' in ${path}")
    endif()
endfunction()

set(POCO_MAIN "${POCO_SOURCE_DIR}/src/main.c")
set(POCO_STRING "${POCO_SOURCE_DIR}/src/strlib.c")
set(POCO_FILE "${POCO_SOURCE_DIR}/src/safefile.c")
set(POCO_FFI "${POCO_SOURCE_DIR}/src/poco_ffi.c")
set(POCO_USER "${ANIMATOR_SOURCE_DIR}/src/pocouser.c")
set(POCO_QNUM "${ANIMATOR_SOURCE_DIR}/src/pocoqnum.c")
set(POLIB_USER "${ANIMATOR_SOURCE_DIR}/src/inc/pocolib.h")

# CLI registry: printf is libc's int-returning implementation; Qtext must
# expose po_qtext's void return rather than an invented integer result.
foreach(evidence
    "{printf, \"int printf(char *format, ...);\"}"
    "void po_qtext(char* format, ...)"
    "{po_qtext, \"void Qtext(char *format, ...);\"}")
    require_literal("${POCO_MAIN}" "${evidence}")
endforeach()

# Portable standard-library wrappers retain the C int result type.
foreach(evidence
	"static int po_sprintf(char* buf, char* format, PocoVm* vm, ...)"
	"{ po_sprintf, \"int     sprintf(char *buf, char *format, ...);\", NULL"
	"POCO_BINDING_RUN_CONTEXT")
    require_literal("${POCO_STRING}" "${evidence}")
endforeach()
foreach(evidence
	"static int po_fprintf(FILE* f, char* format, PocoVm* vm, ...)"
	"POCO_BINDING_RUN_CONTEXT"
	"static const PocoBindingContract fprintf_contract"
    "{po_fprintf,"
	"&fprintf_contract,")
    require_literal("${POCO_FILE}" "${evidence}")
endforeach()

# Animator's registration table is also parsed by Poco.  Boolean is an int
# in the Poco declaration language, so the native functions must return int
# rather than C's one-byte bool.  The legacy vtable keeps the same result
# representation so its function-pointer type cannot diverge from libffi.
foreach(evidence
    "static int po_ttextf(char* fmt, ...)"
    "static void po_TextBox(char* fmt, ...)"
    "static Errcode po_ChoiceBox(char** pchoices, int ccount, char* fmt, ...)"
    "static int po_YesNo(char* question, ...)"
    "static Errcode po_ErrBox(Errcode err, char* fmt, ...)"
    "extern int po_UdSlider(int* inum, int min, int max, void* update,"
    "\"int     printf(char *format, ...);\""
    "\"void    Qtext(char *format, ...);\""
    "\"int     Qchoice(char **buttons, int bcount, char *header, ...);\""
    "\"Boolean Qquestion(char *question, ...);\""
    "\"ErrCode Qerror(ErrCode err, char *format, ...);\""
    "\"Boolean UdQnumber(int *num, int min, int max,"
    "Errcode (*update)(void *data, int num), void *data, char *fmt,...);\"")
    require_literal("${POCO_USER}" "${evidence}")
endforeach()
require_literal("${POCO_QNUM}" "int po_UdSlider(int* inum, int min, int max, void* update,")
require_literal("${POLIB_USER}" "int (*plQquestion)(char* question, ...);")
require_literal("${POLIB_USER}" "int (*plUdQnumber)(int* inum, int min, int max, void* update,")

# The parser's only supported result forms used by the ten bindings map to
# libffi's exact int and void result descriptors.
require_literal("${POCO_FFI}" "case IDO_INT:\n\t\t\treturn &ffi_type_sint;")
require_literal("${POCO_FFI}" "case IDO_VOID:\n\t\t\treturn &ffi_type_void;")

execute_process(
    COMMAND "${POCO_EXECUTABLE}" "${POCO_SOURCE_DIR}/test/pos/live_variadic_bindings.poc"
    WORKING_DIRECTORY "${POCO_SOURCE_DIR}/test"
    RESULT_VARIABLE run_result
    OUTPUT_VARIABLE run_output
    ERROR_VARIABLE run_error
)
file(REMOVE "${POCO_SOURCE_DIR}/test/live_variadic_bindings.tmp")
if(NOT run_result EQUAL 0 OR NOT run_output MATCHES "Success")
    message(FATAL_ERROR
        "Live variadic promotion fixture failed (status ${run_result}).\n"
        "stdout:\n${run_output}\n"
        "stderr:\n${run_error}")
endif()
