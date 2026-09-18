# Poco-private libffi.  Built natively by CMake from the vendored sources:
# no autoconf, no shell `configure`, no `make`, and no ExternalProject.  The
# generated headers this build needs (fficonfig.h, ffi.h, ffitarget.h) are
# produced at configure time, so ffi.h exists as an ordinary target include
# before anything is compiled.
#
# Supported matrix (anything else is a hard error rather than a silent
# mis-build):
#   Windows arm64  (MSVC)   ARM_WIN64   src/aarch64/{ffi.c,win64_armasm.S}
#   Windows x86-64 (MSVC)   X86_WIN64   src/x86/{ffiw64.c,win64_intel.S}
#   macOS arm64             AARCH64     src/aarch64/{ffi.c,sysv.S}
#   Linux arm64             AARCH64     src/aarch64/{ffi.c,sysv.S}
#   Linux x86-64            X86_64      src/x86/{ffi64.c,unix64.S,ffiw64.c,win64.S}

include(CheckIncludeFile)
include(CheckSymbolExists)
include(CheckCSourceCompiles)

set(POCO_LIBFFI_VERSION "3.5.2")
set(FFI_VERSION_STRING "${POCO_LIBFFI_VERSION}")
set(FFI_VERSION_NUMBER 30502)

set(_ffi_src "${CMAKE_CURRENT_SOURCE_DIR}/main")
set(_ffi_gen "${CMAKE_CURRENT_BINARY_DIR}/include")

# ---------------------------------------------------------------------------
# Target selection.  This is the CMake translation of libffi's configure.host.
# ---------------------------------------------------------------------------
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
    set(_ffi_arch aarch64)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64|EM64T|x64)$")
    set(_ffi_arch x86_64)
elseif(MSVC AND CMAKE_GENERATOR_PLATFORM MATCHES "^(ARM64|arm64)$")
    set(_ffi_arch aarch64)
elseif(MSVC AND CMAKE_GENERATOR_PLATFORM MATCHES "^(x64|X64)$")
    set(_ffi_arch x86_64)
else()
    message(FATAL_ERROR
        "Poco's libffi build supports arm64 and x86-64 only; "
        "CMAKE_SYSTEM_PROCESSOR is '${CMAKE_SYSTEM_PROCESSOR}'.")
endif()

if(CMAKE_SIZEOF_VOID_P AND NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR
        "Poco's libffi build supports 64-bit targets only "
        "(pointer size is ${CMAKE_SIZEOF_VOID_P}).")
endif()

# MSVC assembles through ml64/armasm64 and needs a separate preprocessing
# pass; every other toolchain feeds .S straight to the compiler driver.
set(_ffi_msvc_asm OFF)
if(MSVC)
    set(_ffi_msvc_asm ON)
endif()

if(_ffi_arch STREQUAL "aarch64")
    set(_ffi_targetdir aarch64)
    if(WIN32)
        set(FFI_TARGET ARM_WIN64)
    else()
        set(FFI_TARGET AARCH64)
    endif()
else()
    set(_ffi_targetdir x86)
    if(WIN32)
        set(FFI_TARGET X86_WIN64)
    else()
        set(FFI_TARGET X86_64)
    endif()
endif()

set(_ffi_c_sources
    "${_ffi_src}/src/prep_cif.c"
    "${_ffi_src}/src/types.c"
    "${_ffi_src}/src/raw_api.c"
    "${_ffi_src}/src/java_raw_api.c"
    "${_ffi_src}/src/closures.c"
    "${_ffi_src}/src/tramp.c"
)
set(_ffi_asm_sources "")

if(FFI_TARGET STREQUAL "AARCH64")
    list(APPEND _ffi_c_sources "${_ffi_src}/src/aarch64/ffi.c")
    list(APPEND _ffi_asm_sources "${_ffi_src}/src/aarch64/sysv.S")
elseif(FFI_TARGET STREQUAL "ARM_WIN64")
    list(APPEND _ffi_c_sources "${_ffi_src}/src/aarch64/ffi.c")
    list(APPEND _ffi_asm_sources "${_ffi_src}/src/aarch64/win64_armasm.S")
elseif(FFI_TARGET STREQUAL "X86_64")
    list(APPEND _ffi_c_sources
        "${_ffi_src}/src/x86/ffi64.c"
        "${_ffi_src}/src/x86/ffiw64.c")
    list(APPEND _ffi_asm_sources
        "${_ffi_src}/src/x86/unix64.S"
        "${_ffi_src}/src/x86/win64.S")
else() # X86_WIN64
    list(APPEND _ffi_c_sources "${_ffi_src}/src/x86/ffiw64.c")
    list(APPEND _ffi_asm_sources "${_ffi_src}/src/x86/win64_intel.S")
endif()

# ---------------------------------------------------------------------------
# fficonfig.h
# ---------------------------------------------------------------------------
check_include_file("alloca.h" HAVE_ALLOCA_H)
check_include_file("dlfcn.h" HAVE_DLFCN_H)
check_include_file("inttypes.h" HAVE_INTTYPES_H)
check_include_file("stdint.h" HAVE_STDINT_H)
check_include_file("stdio.h" HAVE_STDIO_H)
check_include_file("stdlib.h" HAVE_STDLIB_H)
check_include_file("string.h" HAVE_STRING_H)
check_include_file("strings.h" HAVE_STRINGS_H)
check_include_file("sys/memfd.h" HAVE_SYS_MEMFD_H)
check_include_file("sys/stat.h" HAVE_SYS_STAT_H)
check_include_file("sys/types.h" HAVE_SYS_TYPES_H)
check_include_file("unistd.h" HAVE_UNISTD_H)

if(HAVE_STDIO_H AND HAVE_STDLIB_H AND HAVE_STRING_H)
    set(STDC_HEADERS 1)
endif()

check_symbol_exists(memcpy "string.h" HAVE_MEMCPY)

# libffi's own configure reports these sizes; the ports read SIZEOF_* out of
# fficonfig.h rather than recomputing them.
include(CheckTypeSize)
check_type_size("double" SIZEOF_DOUBLE LANGUAGE C)
check_type_size("long double" SIZEOF_LONG_DOUBLE LANGUAGE C)
check_type_size("size_t" SIZEOF_SIZE_T LANGUAGE C)
foreach(_sz SIZEOF_DOUBLE SIZEOF_LONG_DOUBLE SIZEOF_SIZE_T)
    if(NOT ${_sz})
        message(FATAL_ERROR "libffi configure check failed: ${_sz} is unknown.")
    endif()
endforeach()

# HAVE_LONG_DOUBLE means "long double is wider than double".  It is false on
# arm64 Darwin and on MSVC, true on Linux arm64 and x86-64.
if(SIZEOF_LONG_DOUBLE GREATER SIZEOF_DOUBLE)
    set(HAVE_LONG_DOUBLE 1)
    set(FFI_HAVE_LONG_DOUBLE 1)
else()
    set(HAVE_LONG_DOUBLE "")
    set(FFI_HAVE_LONG_DOUBLE 0)
endif()

if(NOT MSVC)
    check_c_source_compiles(
        "__attribute__((visibility(\"hidden\"))) int poco_ffi_hidden(void) { return 0; }
         int main(void) { return poco_ffi_hidden(); }"
        HAVE_HIDDEN_VISIBILITY_ATTRIBUTE)
    # Both GAS and clang's integrated assembler accept .cfi_* on every
    # non-MSVC target in the matrix; armasm64/ml64 do not.
    set(HAVE_AS_CFI_PSEUDO_OP 1)
    set(EH_FRAME_FLAGS "a")
    set(HAVE_RO_EH_FRAME 1)
else()
    set(HAVE_HIDDEN_VISIBILITY_ATTRIBUTE "")
    set(HAVE_AS_CFI_PSEUDO_OP "")
    set(EH_FRAME_FLAGS "")
    set(HAVE_RO_EH_FRAME "")
endif()

# unix64.S is the only consumer of these two, so they are probed on x86-64
# only.  HAVE_AS_X86_PCREL is not optional there: without it the CIE/FDE
# pointers come out as `X@rel`, which no assembler in the matrix accepts.
if(_ffi_arch STREQUAL "x86_64" AND NOT MSVC)
    check_c_source_compiles([==[
__asm__(".text\n poco_ffi_pcrel: nop\n .data\n .long poco_ffi_pcrel-.\n .text\n");
int main(void) { return 0; }
]==] HAVE_AS_X86_PCREL)
    if(NOT HAVE_AS_X86_PCREL)
        message(FATAL_ERROR
            "libffi's x86-64 unwind tables need an assembler that accepts "
            "pc-relative relocs (`.long sym - .`); this one does not.")
    endif()
    check_c_source_compiles([==[
__asm__(".section .eh_frame,\"a\",@unwind\n .previous\n");
int main(void) { return 0; }
]==] HAVE_AS_X86_64_UNWIND_SECTION_TYPE)
else()
    set(HAVE_AS_X86_PCREL "")
    set(HAVE_AS_X86_64_UNWIND_SECTION_TYPE "")
endif()

# closures.c prefers mkostemp so the temporary exec file is opened with the
# flags it wants in one call; it falls back to mkstemp when absent.
check_c_source_compiles([==[
#define _GNU_SOURCE 1
#include <stdlib.h>
int main(void) { char t[] = "/tmp/pocoffiXXXXXX"; return mkostemp(t, 0) < 0; }
]==] HAVE_MKOSTEMP)

if(APPLE)
    # Darwin mangles C symbols with a leading underscore and forbids
    # PROT_WRITE|PROT_EXEC pages, so closures go through the trampoline table.
    set(SYMBOL_UNDERSCORE 1)
    set(FFI_EXEC_TRAMPOLINE_TABLE 1)
    set(FFI_TRAMPOLINE_TABLE 1)
else()
    set(SYMBOL_UNDERSCORE "")
    set(FFI_EXEC_TRAMPOLINE_TABLE "")
    set(FFI_TRAMPOLINE_TABLE 0)
endif()

# Static trampolines are an optional Linux/Cygwin optimisation.  closures.c
# selects a working allocator on its own for every target in the matrix, so
# this stays off and tramp.c compiles to its unsupported stubs.
set(FFI_EXEC_STATIC_TRAMP "")
set(HAVE_MEMFD_CREATE "")
set(HAVE_ARM64E_PTRAUTH "")
set(HAVE_LONG_DOUBLE_VARIANT "")
set(FFI_NO_RAW_API "")
set(FFI_NO_STRUCTS "")
set(WORDS_BIGENDIAN "")

if(CMAKE_BUILD_TYPE MATCHES "^[Dd]ebug$")
    set(FFI_DEBUG 1)
    list(APPEND _ffi_c_sources "${_ffi_src}/src/debug.c")
else()
    set(FFI_DEBUG "")
endif()

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/fficonfig.h.cmake"
    "${_ffi_gen}/fficonfig.h"
    @ONLY)

# ---------------------------------------------------------------------------
# ffi.h and ffitarget.h.  Substituted with the same keys configure used.
# ---------------------------------------------------------------------------
set(TARGET "${FFI_TARGET}")
set(VERSION "${POCO_LIBFFI_VERSION}")
# ffi.h.in tests these with `#if`, so they must be 0/1, not defined/undefined.
set(_ffi_saved_have_long_double "${HAVE_LONG_DOUBLE}")
set(HAVE_LONG_DOUBLE "${FFI_HAVE_LONG_DOUBLE}")
set(FFI_EXEC_TRAMPOLINE_TABLE "${FFI_TRAMPOLINE_TABLE}")
configure_file(
    "${_ffi_src}/include/ffi.h.in"
    "${_ffi_gen}/ffi.h"
    @ONLY)
set(HAVE_LONG_DOUBLE "${_ffi_saved_have_long_double}")

configure_file(
    "${_ffi_src}/src/${_ffi_targetdir}/ffitarget.h"
    "${_ffi_gen}/ffitarget.h"
    COPYONLY)

set(_ffi_includes
    "${_ffi_gen}"
    "${_ffi_src}/include"
    "${_ffi_src}/src/${_ffi_targetdir}"
)

# ---------------------------------------------------------------------------
# Assembly
# ---------------------------------------------------------------------------
set(_ffi_msvc_objects "")
if(_ffi_msvc_asm)
    # MSVC's assemblers do not run the C preprocessor, and libffi's .S files
    # are written against it.  Preprocess with `cl /EP`, then assemble --
    # the same two-step the vendored msvc_build project uses.
    if(_ffi_arch STREQUAL "aarch64")
        find_program(POCO_FFI_ARMASM armasm64)
        if(NOT POCO_FFI_ARMASM)
            message(FATAL_ERROR
                "armasm64 was not found; it is required to build libffi for "
                "Windows arm64.  Run from a VS arm64 developer environment.")
        endif()
        set(_ffi_assembler "${POCO_FFI_ARMASM}")
    else()
        find_program(POCO_FFI_ML64 ml64)
        if(NOT POCO_FFI_ML64)
            message(FATAL_ERROR
                "ml64 was not found; it is required to build libffi for "
                "Windows x86-64.  Run from a VS x64 developer environment.")
        endif()
        set(_ffi_assembler "${POCO_FFI_ML64}")
    endif()

    set(_ffi_cl_includes "")
    foreach(_inc IN LISTS _ffi_includes)
        list(APPEND _ffi_cl_includes "/I${_inc}")
    endforeach()

    foreach(_asm IN LISTS _ffi_asm_sources)
        get_filename_component(_asm_name "${_asm}" NAME_WE)
        set(_asm_i "${CMAKE_CURRENT_BINARY_DIR}/asm/${_asm_name}.i")
        set(_asm_obj "${CMAKE_CURRENT_BINARY_DIR}/asm/${_asm_name}${CMAKE_C_OUTPUT_EXTENSION}")
        if(_ffi_arch STREQUAL "aarch64")
            set(_assemble_cmd
                COMMAND "${_ffi_assembler}" "${_asm_i}" -o "${_asm_obj}")
        else()
            set(_assemble_cmd
                COMMAND "${_ffi_assembler}" /nologo /c "/Fo${_asm_obj}" "${_asm_i}")
        endif()
        # /EP strips #line directives, /P writes the result to /Fi instead of
        # stdout: add_custom_command runs no shell, so there is no redirect to
        # spell.  /TC forces C, since .S is not an extension cl recognises.
        add_custom_command(
            OUTPUT "${_asm_obj}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/asm"
            COMMAND "${CMAKE_C_COMPILER}" /nologo /TC /EP /P "/Fi${_asm_i}"
                    ${_ffi_cl_includes} "${_asm}"
            ${_assemble_cmd}
            DEPENDS "${_asm}" "${_ffi_gen}/fficonfig.h" "${_ffi_gen}/ffi.h"
            COMMENT "Assembling ${_asm_name} for libffi"
            VERBATIM)
        list(APPEND _ffi_msvc_objects "${_asm_obj}")
    endforeach()
else()
    enable_language(ASM)
    set_source_files_properties(${_ffi_asm_sources} PROPERTIES LANGUAGE ASM)
endif()

# ---------------------------------------------------------------------------
# The library
# ---------------------------------------------------------------------------
if(_ffi_msvc_asm)
    set_source_files_properties(${_ffi_msvc_objects}
        PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
    add_library(poco_ffi STATIC ${_ffi_c_sources} ${_ffi_msvc_objects})
else()
    add_library(poco_ffi STATIC ${_ffi_c_sources} ${_ffi_asm_sources})
endif()

set_target_properties(poco_ffi PROPERTIES
    OUTPUT_NAME poco_ffi
    C_STANDARD 99
    C_EXTENSIONS YES
    # Poco is deliberately a bundled shared library on ELF platforms, so these
    # objects have to be linkable into it.
    POSITION_INDEPENDENT_CODE ON
)
# The generated headers are on the interface: poco's own sources and the CLI
# include <ffi.h> directly.  The library stays private -- it is never exported
# from the Poco package and never appears in a consumer's link line by name.
target_include_directories(poco_ffi PUBLIC ${_ffi_includes})
# FFI_STATIC_BUILD is PUBLIC on purpose: without it ffi.h decorates every
# entry point with __declspec(dllimport) under MSVC, which is wrong for a
# static archive both here and in every translation unit that includes it.
target_compile_definitions(poco_ffi PUBLIC FFI_STATIC_BUILD)
target_compile_definitions(poco_ffi PRIVATE HAVE_CONFIG_H)
if(MSVC)
    target_compile_definitions(poco_ffi PRIVATE _CRT_SECURE_NO_WARNINGS USE_DL_PREFIX)
    target_compile_options(poco_ffi PRIVATE /wd4267 /wd4244 /wd4018)
else()
    target_compile_options(poco_ffi PRIVATE
        -Wno-unused-function
        -Wno-unused-variable
        -Wno-unused-but-set-variable
        -Wno-incompatible-pointer-types
    )
endif()

# A static Poco install has to ship this archive beside libpoco.a, because the
# objects cannot be folded into it the way a shared poco_core absorbs them.
# The parent installs it under this path and renames it to libpoco_ffi.
set(POCO_FFI_STATIC_LIBRARY "$<TARGET_FILE:poco_ffi>"
    CACHE INTERNAL "Poco-private libffi archive")
