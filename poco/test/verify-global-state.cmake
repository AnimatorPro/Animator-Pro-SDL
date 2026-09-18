# Automated companion to poco/test/GLOBAL_STATE_INVENTORY.md.
#
# The inventory's original check was a text scan for named declarations, which
# only catches state someone already knew about and cannot see a function-local
# static at all.  This test asks the linker instead: every object in the
# embeddable core that landed in a writable section is mutable file-scope state,
# whatever it is called and wherever it was declared.  A new global therefore
# fails the test on the commit that introduces it, not on the next audit.
#
# Symbols still listed in POCO_KNOWN_MUTABLE are the inventory's documented
# residue.  Adding a name here is a deliberate act: record the reason in
# GLOBAL_STATE_INVENTORY.md at the same time.

if(NOT DEFINED POCO_CORE_LIBRARY OR "${POCO_CORE_LIBRARY}" STREQUAL "")
    message(FATAL_ERROR "POCO_CORE_LIBRARY must name the built poco core library")
endif()

if(NOT EXISTS "${POCO_CORE_LIBRARY}")
    message(FATAL_ERROR "Missing poco core library: ${POCO_CORE_LIBRARY}")
endif()

find_program(NM_EXECUTABLE nm)
if(NOT NM_EXECUTABLE)
    message(FATAL_ERROR "nm not found; it is required to audit poco's mutable globals")
endif()

# Documented residual mutable globals.  Each entry is explained in
# GLOBAL_STATE_INVENTORY.md under "Known residue".
set(POCO_KNOWN_MUTABLE
    # The legacy Poco_lib control structures: 'next', 'local_data',
    # 'resources' and 'vm' are written when a library is chained onto a VM.
    # Draining them means retiring the Poco_lib/Porexlib registration ABI.
    po_FILE_lib
    po_math_lib
    po_mem_lib
    po_str_lib
    # Lib_proto prototype tables.  Immutable in fact, but Poco_lib::lib is a
    # non-const pointer and Animator casts unrelated structs through it, so
    # const-ifying the tables means const-ifying that field first.
    filelib
    lib
    mathlib
    memlib
    poco_path_legacy_bindings
    # Idempotent one-shot derivations of the tables above into PocoBinding
    # form.  Every racing writer stores the same value, but the writes are
    # still writes.
    poco_standard_file_library.bindings
    poco_standard_file_library.initialized
    poco_standard_file_library.library
    poco_standard_memory_library.bindings
    poco_standard_memory_library.initialized
    poco_standard_path_library.bindings
    poco_standard_path_library.initialized
    poco_standard_string_library.bindings
    poco_standard_string_library.initialized
)

if(APPLE)
    # 'nm -m' names the segment and section, which is the only way to tell a
    # const table holding pointers (__DATA,__const - read-only after the
    # dynamic linker finishes) from real mutable data (__DATA,__data).  Plain
    # 'nm' reports both as 'S'/'D'.
    set(NM_ARGS -m)
    set(WRITABLE_PATTERN "^\\(__DATA,__(data|bss|common)\\) [^ ]+ (.+)$")
    set(SYMBOL_MATCH_INDEX 2)
else()
    # GNU nm: d/D .data, b/B .bss, c/C common.  Read-only data is r/R, so it
    # is excluded without needing section names.
    set(NM_ARGS "")
    set(WRITABLE_PATTERN "^[dDbBcC] (.+)$")
    set(SYMBOL_MATCH_INDEX 1)
endif()

execute_process(
    COMMAND "${NM_EXECUTABLE}" ${NM_ARGS} "${POCO_CORE_LIBRARY}"
    OUTPUT_VARIABLE NM_OUTPUT
    ERROR_VARIABLE NM_ERROR
    RESULT_VARIABLE NM_RESULT
)
if(NOT NM_RESULT EQUAL 0)
    message(FATAL_ERROR "nm ${POCO_CORE_LIBRARY} failed (${NM_RESULT}): ${NM_ERROR}")
endif()

string(REPLACE "\n" ";" NM_LINES "${NM_OUTPUT}")

set(WRITABLE_SEEN "")
set(UNEXPECTED "")
foreach(NM_LINE IN LISTS NM_LINES)
    # Drop the address column; undefined symbols have none.
    string(REGEX REPLACE "^[0-9a-fA-F]+ +" "" NM_LINE "${NM_LINE}")
    if(NOT NM_LINE MATCHES "${WRITABLE_PATTERN}")
        continue()
    endif()
    if(SYMBOL_MATCH_INDEX EQUAL 2)
        set(SYMBOL "${CMAKE_MATCH_2}")
    else()
        set(SYMBOL "${CMAKE_MATCH_1}")
    endif()
    string(STRIP "${SYMBOL}" SYMBOL)
    # Mach-O prefixes C identifiers with an underscore; ELF does not.
    string(REGEX REPLACE "^_" "" SYMBOL "${SYMBOL}")
    # Assembler-local labels, not program objects.
    if(SYMBOL MATCHES "^ltmp[0-9]+$")
        continue()
    endif()
    # At -O1+ the AArch64 backend pools internal globals into one anonymous
    # object, which erases the names this audit reads.  Passing would be a
    # lie: every static in the library is hiding inside that blob.
    if(SYMBOL MATCHES "^MergedGlobals")
        message(FATAL_ERROR
            "${POCO_CORE_LIBRARY} contains '${SYMBOL}': the compiler pooled "
            "its internal globals, so individual statics are invisible and "
            "this audit cannot see them.  Build poco_core with "
            "-mno-global-merge (poco/test/CMakeLists.txt attaches it when the "
            "compiler supports it).")
    endif()
    list(APPEND WRITABLE_SEEN "${SYMBOL}")
    if(SYMBOL IN_LIST POCO_KNOWN_MUTABLE)
        continue()
    endif()
    list(APPEND UNEXPECTED "${SYMBOL}")
endforeach()

if(NOT WRITABLE_SEEN)
    message(FATAL_ERROR
        "The symbol scan matched nothing at all in ${POCO_CORE_LIBRARY}. "
        "Either nm's output format changed or the wrong file was passed; "
        "either way the test is vacuous and must not be reported as passing.")
endif()

list(REMOVE_DUPLICATES UNEXPECTED)
list(SORT UNEXPECTED)

if(UNEXPECTED)
    string(REPLACE ";" "\n  " UNEXPECTED_TEXT "${UNEXPECTED}")
    message(FATAL_ERROR
        "Mutable file-scope state in the embeddable poco core:\n  ${UNEXPECTED_TEXT}\n"
        "Two VMs on two threads share every object listed above.  Move it onto "
        "PocoVm, Poco_cb or the activation, or make it const if it is read-only. "
        "If it genuinely must stay shared, add it to POCO_KNOWN_MUTABLE in "
        "poco/test/verify-global-state.cmake and document why in "
        "poco/test/GLOBAL_STATE_INVENTORY.md.")
endif()

list(REMOVE_DUPLICATES WRITABLE_SEEN)
list(LENGTH WRITABLE_SEEN WRITABLE_COUNT)
message(STATUS
    "poco core carries no undocumented mutable file-scope state "
    "(${WRITABLE_COUNT} documented writable symbols in ${POCO_CORE_LIBRARY})")
