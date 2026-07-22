/*
 * Compatibility-only legacy compiler API.
 *
 * Deprecated for new hosts: use <poco/poco.h> and the PocoVm/PocoProgram
 * lifecycle.  compile_poco(), run_poco(), and free_poco() preserve the
 * existing ABI solely for Animator and legacy callers during migration.
 */
#ifndef POCOFACE_H
#define POCOFACE_H

#ifndef POCO_LEGACY_NAMES_TYPE
#include "commonst.h"
#define POCO_LEGACY_NAMES_TYPE Names
#define POCO_UNDEF_LEGACY_NAMES_TYPE
#endif
#ifndef POCOLIB_H
#include "pocolib.h"
#endif

//! TODO: Determine if these sizes are enough for modern use
#define POCO_STACKSIZE_MIN (32 * 1024L)
#define POCO_STACKSIZE_MAX (256 * 1024L)
#define POCO_STACKSIZE_DEFAULT (64 * 1024L) /* default poco runtime stacksize */

extern int po_version_number; /* added 10/30/90, poco's version number */

Errcode compile_poco(void** ppev,       /* returns executable pexe on Success */
					 char* source_name, /* name of source file */
					 char* errors,      /* error file or NULL for stderr */
					 char* dump_name,   /* disassembly file or NULL for none */
					 /* for built-in function library */
					 Poco_lib* lib,
					 /* stuff for location of 1'st error */
					 char* err_fname, /* file where error detected */
					 long* err_line,  /* line where error detected */
					 int* err_char,   /* character in line where err detected */
					 POCO_LEGACY_NAMES_TYPE* include_dirs, /* include search path */
					 bool verbose                          /* enable verbose debug output */
);
/* Compile poco function.  Leave error messages in a file named errors.
   Otherwise build up executable structure in *ppev */

Errcode run_poco(void** ppev, /* value from compile_poco */
				 char* trace_name, bool (*check_abort)(void*), void* check_abort_data,
				 long* err_line);
/* run_poco:  execute *ppev starting at main() */

void free_poco(void** ppev);
/* free_poco: free up ppev returned by compile_poco and set *pev to NULL */

/* Internal main/named-entry execution path.  This deliberately does not run
 * the program's global initializer or clear its data segment. */
Errcode po_activation_run_entry(PocoActivation* activation, const char* entry);


/**** These next functions are for when you want to run a function
   (not necessarily main) inside a compiled poco program.
   (DON'T USE THEM!  Running something other than main() is untested) */

Errcode pev_alloc_data(void* p);
/* allocate and initialize data areas */

void pev_free_data(void* p);
/* free data areas */


/* Routines useful for calling a specific function in a Poco program
 * in C. */
void* po_fuf_code(void* fuf);
char* po_fuf_name(void* fuf);

/* Boundary constant: poco's value for Err_in_err_file.
 * Use this at the poco/host boundary instead of Err_in_err_file,
 * whose numeric value differs between the two errcodes.h files. */
#define POCO_ERR_IN_ERR_FILE (-11)

#ifdef POCO_UNDEF_LEGACY_NAMES_TYPE
#undef POCO_UNDEF_LEGACY_NAMES_TYPE
#undef POCO_LEGACY_NAMES_TYPE
#endif

#endif /* POCOFACE_H */
