/* fficonfig.h template for the native CMake build of the vendored libffi.
   Replaces main/fficonfig.h.in and the autoconf machinery that filled it.
   Only the knobs the Poco platform matrix needs are represented here:
   Windows arm64/x86-64 (MSVC), macOS arm64, Linux arm64/x86-64.  */

#ifndef POCO_FFICONFIG_H
#define POCO_FFICONFIG_H

/* Define to the flags needed for the .section .eh_frame directive. */
#cmakedefine EH_FRAME_FLAGS "@EH_FRAME_FLAGS@"

/* Define this if you want extra debugging. */
#cmakedefine FFI_DEBUG 1

/* Define this if you want statically defined trampolines. */
#cmakedefine FFI_EXEC_STATIC_TRAMP 1

/* Cannot use PROT_EXEC on this target, so we revert to alternative means. */
#cmakedefine FFI_EXEC_TRAMPOLINE_TABLE 1

/* Define this if you do not want support for the raw API. */
#cmakedefine FFI_NO_RAW_API 1

/* Define this if you do not want support for aggregate types. */
#cmakedefine FFI_NO_STRUCTS 1

#cmakedefine HAVE_ALLOCA_H 1
#cmakedefine HAVE_ARM64E_PTRAUTH 1
#cmakedefine HAVE_AS_CFI_PSEUDO_OP 1

/* x86 only: without these the .eh_frame emitted by unix64.S uses the @rel
   variant and the @progbits section type, and the former does not assemble. */
#cmakedefine HAVE_AS_X86_PCREL 1
#cmakedefine HAVE_AS_X86_64_UNWIND_SECTION_TYPE 1

#cmakedefine HAVE_DLFCN_H 1
#cmakedefine HAVE_HIDDEN_VISIBILITY_ATTRIBUTE 1
#cmakedefine HAVE_INTTYPES_H 1
#cmakedefine HAVE_LONG_DOUBLE 1
#cmakedefine HAVE_LONG_DOUBLE_VARIANT 1
#cmakedefine HAVE_MEMCPY 1
#cmakedefine HAVE_MEMFD_CREATE 1
#cmakedefine HAVE_MKOSTEMP 1
#cmakedefine HAVE_RO_EH_FRAME 1
#cmakedefine HAVE_STDINT_H 1
#cmakedefine HAVE_STDIO_H 1
#cmakedefine HAVE_STDLIB_H 1
#cmakedefine HAVE_STRINGS_H 1
#cmakedefine HAVE_STRING_H 1
#cmakedefine HAVE_SYS_MEMFD_H 1
#cmakedefine HAVE_SYS_STAT_H 1
#cmakedefine HAVE_SYS_TYPES_H 1
#cmakedefine HAVE_UNISTD_H 1

/* Sizes the ports read out of fficonfig.h rather than computing. */
#define SIZEOF_DOUBLE @SIZEOF_DOUBLE@
#define SIZEOF_LONG_DOUBLE @SIZEOF_LONG_DOUBLE@
#define SIZEOF_SIZE_T @SIZEOF_SIZE_T@

/* Define to 1 if all of the C89 standard headers exist. */
#cmakedefine STDC_HEADERS 1

/* Define if symbols are underscored. */
#cmakedefine SYMBOL_UNDERSCORE 1

#define PACKAGE "libffi"
#define PACKAGE_BUGREPORT "http://github.com/libffi/libffi/issues"
#define PACKAGE_NAME "libffi"
#define PACKAGE_STRING "libffi @POCO_LIBFFI_VERSION@"
#define PACKAGE_TARNAME "libffi"
#define PACKAGE_URL ""
#define PACKAGE_VERSION "@POCO_LIBFFI_VERSION@"
#define VERSION "@POCO_LIBFFI_VERSION@"

/* Define WORDS_BIGENDIAN to 1 if your processor stores words with the most
   significant byte first.  Every target in the matrix is little-endian. */
#cmakedefine WORDS_BIGENDIAN 1

#ifdef HAVE_HIDDEN_VISIBILITY_ATTRIBUTE
#ifdef LIBFFI_ASM
#ifdef __APPLE__
#define FFI_HIDDEN(name) .private_extern name
#else
#define FFI_HIDDEN(name) .hidden name
#endif
#else
#define FFI_HIDDEN __attribute__((visibility("hidden")))
#endif
#else
#ifdef LIBFFI_ASM
#define FFI_HIDDEN(name)
#else
#define FFI_HIDDEN
#endif
#endif

#endif /* POCO_FFICONFIG_H */
