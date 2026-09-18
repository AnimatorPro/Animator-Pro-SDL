/*****************************************************************************
 *
 * compile_driver.c - drives one compile from sources to an executable image.
 *
 *	The compile_poco_*_with_vm family: set up the control block, walk the
 *	'#pragma poco use' graph, compile each unit, link them, and hand back an
 *	executable memory structure that po_free_executable() releases.  The
 *	public API wrapped around this is in vm_api.c; the library prototype feed
 *	is in libproto.c and the use graph in use_graph.c.
 *
 * This file was pocoface.c until it was split; the maintenance log below
 * predates that and covers libproto.c, vm_api.c, vm_diagnostics.c and
 * poco_call.c as well.
 *
 * MAINTENANCE:
 *	08/18/90	(Ian)
 *				Added detection and handling of a NULL library list pointer
 *				in compile_poco().	(OOOPS, except it doesn't work right.
 *				actually, poco runs fine this way standalone, but it locks
 *				up the machine if run under TD or TPROF.  I hope this isn't
 *				a symptom of some deeper problem.)
 *	08/27/90	(Ian)
 *				Added calls to init and free the new memory management system.
 *	08/29/90	(Ian)
 *				Added setjmp/longjmp error handling to poco.  This largely
 *				affected the compile_poco() routine; most error handling and
 *				cleanup actions now occur therein.	Also, new logic was added
 *				to close all files in the file_stack linked list during error
 *				handling, (old version closed the most recently opened file).
 *	08/30/90	(Jim)
 *				Added file close call for pcb->t.err_file in compile_poco().
 *	09/04/90	(Ian)
 *				Ooops.	Turns out po_free_pp() contains the logic to close all
 *				open files.  So, the loop I coded to do that was removed,
 *				and a po_free_pp() call was inserted.
 *	10/21/90	(Ian)
 *				New interface to library prototype data.  Deleted routines
 *				pod_init(), pod_next(), prol() and fixup_lib_symbols(),
 *				replaced them with new routine poc_get_libproto_line().
 *	10/25/90	(Ian)
 *				Reworked the library prototype interface some more, added new
 *				routine po_open_library(), but we still need a function on
 *				the PJ side to actually load libraries for us.	Right now,
 *				the open library function handles only the builtin libraries
 *				passed to us by pj at compile time.
 *	04/14/91	(Peter)
 *				Modified po_open_library to take id_string argument
 *	05/01/91	(Ian)
 *				Added builtin_err declaration.	This global variable is now
 *				owned by poco instead of by the host, because we need to use
 *				it during the compile phase to detect math errors during the
 *				folding of constants.  Also, run_poco() was changed to
 *				remove the pointer to builtin_err that used to get passed in.
 *	01/10/92	(Ian)
 *				Major bugfix in po_get_libproto_line(), to fix the bug that
 *				prevented using multiple #pragma library statements.  See
 *				the comments in that function's header block for details.
 *	05/17/92	(Ian)
 *				Added a sanity check to po_get_libproto_line() to catch
 *				NULL proto string pointers.
 *	09/17/92	(Ian)
 *				Tweaked logic used to report the error file name and line
 *				number back to the host.  We used to return the line number
 *				from curtoken, but if the error was in a preprocessor
 *				statement this could be wildly innacurate.	Now, we have a
 *				new field in the pcb that carries the error line number;
 *				it is set by the error reporting routines in either the
 *				parser or the preprocessor.  Also, if an error happens
 *				in parsing prototypes in a library, we now report the file
 *				and line number of the #pragma poco library statement.
 *				And finally, restored handling of the character position
 *				on the line where the error was detected.  Provisions for
 *				this existed in the tokenizer, but it wasn't being used.
 ****************************************************************************/
/*
 * The 10/21/90 through 05/17/92 entries above describe the library prototype
 * feed, which now lives in libproto.c.
 */

#include "pocoface.h"

#include "activation.h"
#include "filepath.h"
#include "poco_errcodes.h"
#include "poco_ffi.h"
#include "poco_internal.h"
#include "pocmemry.h"
#include "pocoload.h"
#include "polink.h"
#include "pp.h"
#include "program_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef POCO_DEBUG_DUMP
/*****************************************************************************
 *
 ****************************************************************************/
void po_show_basic_sizes()
{
	fprintf(stderr,
			"Symbol.......%5zu\n"
			"Type_info....%5zu\n"
			"Code_buf.....%5zu\n"
			"Exp_frame....%5zu\n"
			"Func_frame...%5zu\n"
			"Poco_frame...%5zu\n"
			"Tstack.......%5zu\n"
			"PreprocessorState%5zu\n"
			"Poco_cb......%5zu\n",
			sizeof(Symbol), sizeof(Type_info), sizeof(Code_buf),
			sizeof(Exp_frame) + HASH_SIZE * sizeof(Symbol*), sizeof(Func_frame), sizeof(Poco_frame),
			sizeof(Tstack), sizeof(PreprocessorState), sizeof(Poco_cb));
}
#endif /* DEBUG */

/*****************************************************************************
 * create a file, complain if it fails.
 ****************************************************************************/
static FILE* must_fmake(Poco_cb* pcb, char* fn)
{
	FILE* fp = fopen(fn, "w");

	if (fp == NULL) {
		fprintf(pcb->t.err_file, "Can't make %s\n", fn);
	}
	return fp;
}

/*****************************************************************************
 * create a file unless the name is NULL, die if it fails.
 ****************************************************************************/
static Errcode wanna_make(Poco_cb* pcb, FILE** f, char* fn)
{
	if (fn != NULL) {
		if ((*f = must_fmake(pcb, fn)) == NULL) {
			return (Err_create);
		}
	}
	return (Success);
}

/*****************************************************************************
 * close a file if it's open, and if it's not a device file.
 ****************************************************************************/
static void gentle_fclose(FILE* f)
{
	if (f != NULL && f != stdout && f != stderr) {
		fclose(f);
	}
}

/*****************************************************************************
 * compile a poco program.	entry point to the compiler from PJ.
 ****************************************************************************/
static Errcode compile_poco_sources_with_vm(
	PocoVm* vm, void** ppexe, const char* const* source_names,
	const char* const* physical_source_paths, const char* const* sources,
	const size_t* source_lengths, Names* const* include_dirs, const size_t* const* use_indices,
	const size_t* use_counts, size_t source_count, char* errors_name, char* dump_name,
	Poco_lib* lib, char* err_file, size_t err_file_capacity, long* err_line, int* err_char,
	bool verbose)
{
	Poco_cb* pcb;
	Errcode err;
	Poco_run_env* pev;
	File_stack* fs;
	const char* current_source_name = source_names[0];
	size_t source_index;

	*ppexe = NULL;
	if (vm != NULL) {
		vm->last_error[0] = '\0';
	}

	if (Success != (err = po_init_memory_management(&pcb))) {
		return Err_no_memory; /* MUST return immediately if init fails. */
	}

	pcb->run.vm = vm;
	pcb->compile_aborted = false;
	pcb->compile_err = Success;

	pcb->stack_bottom = ((char*)&ppexe) - MAX_STACK;

	pcb->t.err_file = stdout;
	pcb->t.library_dirs = vm != NULL ? vm->library_dirs : NULL;
	pcb->t.verbose = verbose;

	pcb->libfunc = NULL;
	pcb->libcontract = NULL;
	pcb->libflags = 0;
	pcb->builtin_lib = lib;

	/* Use an anonymous temp file for error output so we don't need
	 * a named temp file path that must be resolved for fopen(). */
	{
		FILE* etmp = tmpfile();
		if (etmp != NULL) {
			pcb->t.err_file = etmp;
		}
		/* else: falls back to stdout */
	}

	if (dump_name != NULL) {
		pcb->po_dump_file = fopen(dump_name, "w");
	}

	for (source_index = 0; source_index < source_count; ++source_index) {
		current_source_name = source_names[source_index];
		pcb->current_unit_index = source_index;
		pcb->current_use_indices = use_indices != NULL ? use_indices[source_index] : NULL;
		pcb->current_use_count = use_counts != NULL ? use_counts[source_index] : 0;
		pcb->t.include_dirs = include_dirs != NULL ? include_dirs[source_index] : NULL;
		pcb->t.script_path =
			sources[source_index] != NULL
				? (physical_source_paths != NULL ? physical_source_paths[source_index] : NULL)
				: source_names[source_index];
		err = sources[source_index] != NULL
				  ? po_compile_buffer(pcb, (char*)source_names[source_index], sources[source_index],
									  source_lengths[source_index])
				  : po_compile_file(pcb, (char*)source_names[source_index]);
		if (err != Success || pcb->compile_aborted) {
			break;
		}
	}
	if (err == Success && !pcb->compile_aborted && !po_link_compiled_units(pcb)) {
		err = pcb->compile_err < Success ? pcb->compile_err : Err_syntax;
	}
	/* include_dirs is caller-owned and may be a transient file-directory
	 * entry.  It is compile-only state, so do not retain a dangling pointer in
	 * the successful program's allocation owner. */
	pcb->t.include_dirs = NULL;
	pcb->t.library_dirs = NULL;
	pcb->t.script_path = NULL;
	if (err != Success || pcb->compile_aborted) {
		/* Read error output into the owning VM's diagnostic buffer. */
		if (vm != NULL && pcb->t.err_file != NULL && pcb->t.err_file != stdout &&
			pcb->t.err_file != stderr) {
			fflush(pcb->t.err_file);
			rewind(pcb->t.err_file);
			size_t n = fread(vm->last_error, 1, sizeof(vm->last_error) - 1, pcb->t.err_file);
			vm->last_error[n] = '\0';
		}
		err = Err_in_err_file; /* we reported it in error file */
		goto OUT;
	}

	pev = pj_zalloc((long)sizeof(*pev));
	if (pev == NULL) {
		err = Err_no_memory;
		goto OUT;
	}

	pcb->run.lib = lib;
	*pev = pcb->run;
	pev->compile_pcb = pcb;
	*ppexe = pev;


OUT:
	gentle_fclose(pcb->po_dump_file);
	gentle_fclose(pcb->t.err_file);

	/* A successfully compiled program retains pcb as its owner for the
	 * compiler/runtime allocation arena.  po_free_executable() releases that arena after
	 * execution; releasing it here leaves a dangling compile_pcb and causes a
	 * second free during program destruction. */
	if (err != Success) {
		*err_char = 0;
		fs = pcb->t.file_stack;
		if (fs == NULL || fs->line_count == 0) {
			*err_line = pcb->error_line_number;
			*err_char = pcb->error_char_number;
			snprintf(err_file, err_file_capacity, "%s", current_source_name);
		} else {
			if ((fs->flags & FSF_ISLIB) && fs->pred != NULL) {
				*err_line = fs->pred->line_count;
				snprintf(err_file, err_file_capacity, "%s", fs->pred->name);
			} else {
				*err_line = pcb->error_line_number;
				*err_char = pcb->error_char_number;
				snprintf(err_file, err_file_capacity, "%s", fs->name);
			}
		}

		po_free_pp(pcb);
		pj_free_pocorexes(&pcb->run.loaded_libs);
		po_free_all_memory(pcb);
	}

	if (err == Success) {
		err = po_ffi_build_structures(pev);
		if (err != Success) {
			po_free_executable(ppexe);
		}
	}

	(void)errors_name;
	return err;
}

static Errcode compile_poco_source_with_vm(PocoVm* vm, void** ppexe, char* source_name,
										   const char* physical_source_path, const char* source,
										   size_t source_length, bool from_buffer,
										   char* errors_name, char* dump_name, Poco_lib* lib,
										   char* err_file, size_t err_file_capacity, long* err_line,
										   int* err_char, Names* include_dirs, bool verbose)
{
	const char* source_names[] = {source_name};
	const char* physical_source_paths[] = {physical_source_path};
	const char* sources[] = {from_buffer ? source : NULL};
	size_t source_lengths[] = {source_length};
	Names* source_include_dirs[] = {include_dirs};

	return compile_poco_sources_with_vm(vm, ppexe, source_names, physical_source_paths, sources,
										source_lengths, source_include_dirs, NULL, NULL, 1,
										errors_name, dump_name, lib, err_file, err_file_capacity,
										err_line, err_char, verbose);
}

Errcode compile_poco_with_vm(PocoVm* vm, void** ppexe, char* source_name, char* errors_name,
							 char* dump_name, Poco_lib* lib, char* err_file, long* err_line,
							 int* err_char, Names* include_dirs, bool verbose)
{
	return compile_poco_source_with_vm(vm, ppexe, source_name, source_name, NULL, 0, false,
									   errors_name, dump_name, lib, err_file, PATH_SIZE, err_line,
									   err_char, include_dirs, verbose);
}

Errcode compile_poco_buffer_with_vm(PocoVm* vm, void** ppexe, char* source_name,
									const char* physical_source_path, const char* source,
									size_t source_length, char* dump_name, Poco_lib* lib,
									char* err_file, size_t err_file_capacity, long* err_line,
									int* err_char, Names* include_dirs, bool verbose)
{
	return compile_poco_source_with_vm(
		vm, ppexe, source_name, physical_source_path, source, source_length, true, NULL, dump_name,
		lib, err_file, err_file_capacity, err_line, err_char, include_dirs, verbose);
}

Errcode compile_poco_files_with_vm(PocoVm* vm, void** ppexe, const char* const* source_names,
								   const char* const* physical_source_paths,
								   const char* const* sources, const size_t* source_lengths,
								   Names* const* include_dirs, const size_t* const* use_indices,
								   const size_t* use_counts, size_t source_count, Poco_lib* lib,
								   char* err_file, size_t err_file_capacity, long* err_line,
								   int* err_char, bool verbose)
{
	return compile_poco_sources_with_vm(vm, ppexe, source_names, physical_source_paths, sources,
										source_lengths, include_dirs, use_indices, use_counts,
										source_count, NULL, NULL, lib, err_file, err_file_capacity,
										err_line, err_char, verbose);
}

/*****************************************************************************
 * free runtime resources used by a poco program.
 ****************************************************************************/
void po_free_executable(void** ppexe)
{
	Poco_run_env* pp;
	Poco_cb* compile_pcb = NULL;

	if ((pp = *ppexe) != NULL) {
		compile_pcb = (Poco_cb*)pp->compile_pcb;
		po_free_run_env(pp);
		pj_free_pocorexes(&pp->loaded_libs);
		pj_free(pp); /* this is allocated from PJ, free back to PJ. */
		*ppexe = NULL;
	}
	if (compile_pcb != NULL) {
		po_free_all_memory(compile_pcb);
	}
}

/*****************************************************************************
 * return pointer to name of function associated with a given fuf.
 ****************************************************************************/
char* po_fuf_name(void* fuf)
{
	return (((Func_frame*)fuf)->name);
}

/*****************************************************************************
 * return pointer to code buffer associated with a given fuf.
 ****************************************************************************/
void* po_fuf_code(void* fuf)
{
	Func_frame* function = fuf;

	if (function == NULL || function->magic != FUNC_MAGIC || function->code_pt == NULL ||
		function->activation == NULL) {
		return NULL;
	}
	return function;
}

/*****************************************************************************
 * used by PJ when run as 'PJ filename.POC' and an error occurs.
 ****************************************************************************/
Errcode po_file_to_stdout(char* name)
{
	FILE* f;
	int c;

	f = fopen(name, "r");
	if (f == NULL) {
		return Err_create;
	}
	while ((c = fgetc(f)) != EOF) {
		fputc(c, stdout);
	}
	fputc('\n', stdout);
	fclose(f);
	return Success;
}
