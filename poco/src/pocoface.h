/*
 * Poco-internal compiler/runtime declarations.
 *
 * External hosts must use <poco/poco.h> and the PocoVm/PocoProgram lifecycle.
 * The legacy compile_poco()/run_poco()/free_poco() entry points have been
 * removed; nothing outside poco/src should include this header.
 */
#ifndef POCOFACE_H
#define POCOFACE_H

#include "poco_names.h"
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

/*----------------------------------------------------------------------------
 * VM-scoped diagnostics (vm_diagnostics.c), the library prototype feed
 * (libproto.c) and the compile entry points (compile_driver.c).
 *
 * Two of these take the compiler control block and one returns the pointer
 * registry.  Both are named by struct tag rather than by typedef so that this
 * header stays independent of poco_internal.h: test and host translation units
 * include pocoface.h without libffi on their include path.
 *--------------------------------------------------------------------------*/

struct poco_cb;
struct poco_pointer_registry;

void poco_set_error(PocoVm* vm, const char* fmt, ...);
Errcode* poco_vm_builtin_error(PocoVm* vm);
char* poco_vm_errtext_buffer(PocoVm* vm);
Errcode* poco_active_builtin_error(void);
PocoVm* poco_active_vm(void);
PocoVm* poco_push_active_vm(PocoVm* vm);
void poco_pop_active_vm(PocoVm* previous);
Errcode print_pocolib(char* filename, Poco_lib* lib);
Poco_lib* po_open_library(struct poco_cb* pcb, char* libname, char* id_str);
char* po_get_libproto_line(struct poco_cb* pcb);
Errcode compile_poco_with_vm(PocoVm* vm, void** ppexe, char* source_name, char* errors,
							 char* dump_name, Poco_lib* lib, char* err_file, long* err_line,
							 int* err_char, Names* include_dirs, bool verbose);
Errcode compile_poco_buffer_with_vm(PocoVm* vm, void** ppexe, char* source_name,
									const char* physical_source_path, const char* source,
									size_t source_length, char* dump_name, Poco_lib* lib,
									char* err_file, size_t err_file_capacity, long* err_line,
									int* err_char, Names* include_dirs, bool verbose);
Errcode compile_poco_files_with_vm(PocoVm* vm, void** ppexe, const char* const* source_names,
								   const char* const* physical_source_paths,
								   const char* const* sources, const size_t* source_lengths,
								   Names* const* include_dirs, const size_t* const* use_indices,
								   const size_t* use_counts, size_t source_count, Poco_lib* lib,
								   char* err_file, size_t err_file_capacity, long* err_line,
								   int* err_char, bool verbose);
Poco_lib* poco_active_library(PocoVm* vm, const char* identity);
struct poco_pointer_registry* poco_active_pointer_registry(PocoVm* vm);
const PocoModuleHooks* poco_vm_module_hooks(PocoVm* vm);
Errcode po_file_to_stdout(char* name);

#endif /* POCOFACE_H */
