/*
 * Poco-internal compiler/runtime declarations.
 *
 * External hosts must use <poco/poco.h> and the PocoVm/PocoProgram lifecycle.
 * The legacy compile_poco()/run_poco()/free_poco() entry points have been
 * removed; nothing outside poco/src should include this header.
 */
#ifndef POCOFACE_H
#define POCOFACE_H

#include "commonst.h"
#ifndef POCOLIB_H
#include "pocolib.h"
#endif

//! TODO: Determine if these sizes are enough for modern use
#define POCO_STACKSIZE_MIN (32 * 1024L)
#define POCO_STACKSIZE_MAX (256 * 1024L)
#define POCO_STACKSIZE_DEFAULT (64 * 1024L) /* default poco runtime stacksize */

extern int po_version_number; /* added 10/30/90, poco's version number */

void po_free_executable(void** ppev);
/* po_free_executable: free up the executable returned by the
   compile_poco_*_with_vm family and set *ppev to NULL */

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

#endif /* POCOFACE_H */
