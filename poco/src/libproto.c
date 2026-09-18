/*******************************************************************************
 * libproto.c - the compiler's library prototype feed and the POE lookup the
 * loaded modules see.
 *
 * The preprocessor pulls one prototype line at a time out of the builtin and
 * loaded library lists; po_findpoe() hands a running program the same lists.
 * The maintenance log for this code is at the head of compile_driver.c.
 ******************************************************************************/

#include "pocoface.h"

#include "activation.h"
#include "poco_errcodes.h"
#include "poco_internal.h"
#include "pocolib.h"
#include "pocoload.h"
#include "pp.h"

#include <stdio.h>
#include <string.h>

/*****************************************************************************
 * print a list of function names in a poco lib.
 ****************************************************************************/
static Errcode print_one_lib(FILE* f, const Lib_proto* lib, int lib_size)
{
	while (--lib_size >= 0) {
		fprintf(f, "%s\n", lib->proto);
		lib += 1;
	}
	return (Success);
}

/*****************************************************************************
 * print the names of all functions in all poco libs.
 ****************************************************************************/
Errcode print_pocolib(char* filename, Poco_lib* lib)
{
	FILE* f;
	Errcode err = Success;

	if ((f = fopen(filename, "w")) != NULL) {
		while (lib != NULL) {
			fprintf(f, "/********* %s library ***********/\n", lib->name);
			if ((err = print_one_lib(f, lib->lib, lib->count)) < Success) {
				break;
			}
			lib = lib->next;
		}
	} else {
		err = Err_create;
	}
	fclose(f);
	return (err);
}

/*****************************************************************************
 * find a library or loaded poe module.
 * returns a pointer to its Lib_protos via *plibreturn and count of protos.
 *
 * this function now wears several hats.  if a specific library name is
 * passed, we search the linked list of loaded poe libraries for that name.
 * if the requested library name is "poco$builtin" or "poco$loaded", the
 * first library in the corresponding linked list is returned.	in this case,
 * subsequent calls with a NULL libname pointer will return the next library
 * in the list.
 *
 * if the requested library is not found, or when the end of a list of libs
 * is reached, the return value is Err_not_found, and the returned proto
 * pointer is set to NULL.
 *
 * NOTE!  This routine is part of the POE interface; a pointer to it is
 *		  provided to poe routines in the interface structure.	This routine
 *		  IS NOT intended for use by PJ internally -- it can only return
 *		  valid results while a poco program is currently executing!
 ****************************************************************************/
static int poco_findpoe_in_vm(PocoVm* vm, char* libname, const Lib_proto** plibreturn)
{
	PocoActivation* activation = vm != NULL ? vm->activation : NULL;
	Poco_lib* ll;

	/*------------------------------------------------------------------------
	 * first make sure we don't get crashed by a naughty caller...
	 *----------------------------------------------------------------------*/

	if (NULL == plibreturn) {
		return Err_null_ref; /* defensive programming */
	}

	if (NULL == activation) {
		goto ERROR_EXIT; /* "can't happen" */
	}

	/*------------------------------------------------------------------------
	 * if the libname pointer is NULL, the caller wants the next library in
	 * the linked list, go return it...
	 *----------------------------------------------------------------------*/

	if (NULL == libname) {
		ll = activation->findpoe_next;
		goto RETURN_LIST_ITEM;
	}

	/*------------------------------------------------------------------------
	 * if the requested name is poco$builtin or poco$loaded, the caller wants
	 * to start a series of calls to walk the corresponding list...
	 *----------------------------------------------------------------------*/

	if (0 == stricmp(libname, "poco$builtin")) {
		ll = activation->builtin_libraries;
		goto RETURN_LIST_ITEM;
	}

	if (0 == stricmp(libname, "poco$loaded")) {
		ll = activation->loaded_libraries;
		goto RETURN_LIST_ITEM;
	}

	/*------------------------------------------------------------------------
	 * if we get to here, the caller wants a specific library, look for it...
	 *----------------------------------------------------------------------*/

	for (ll = activation->loaded_libraries; ll != NULL; ll = ll->next) {
		if (0 == stricmp(libname, ll->name)) {
			goto GOOD_EXIT;
		}
	}

ERROR_EXIT:

	*plibreturn = NULL;
	return Err_not_found;

RETURN_LIST_ITEM:

	if (NULL == ll) {
		goto ERROR_EXIT; /* return not-found/end-of-list status */
	}

	activation->findpoe_next = ll->next; /* reset list ptr for next call */

GOOD_EXIT:

	*plibreturn = ll->lib;
	return ll->count;
}

int po_findpoe(PocoVm* vm, char* libname, const Lib_proto** plibreturn)
{
	return poco_findpoe_in_vm(vm, libname, plibreturn);
}

/*****************************************************************************
 *
 ****************************************************************************/
Poco_lib* po_open_library(Poco_cb* pcb, char* libname, char* id_string)
{
	Errcode err;
	Poco_lib* ll;

	if (0 == po_eqstrcmp("poco$builtin", libname)) {
		return pcb->builtin_lib;
	} else {
		if (pcb->t.verbose) {
			fprintf(stderr, "[poco library] #pragma poco library '%s' encountered\n", libname);
		}
		{
			if ((err = pj_load_pocorex(&ll, pcb->t.script_path, libname, id_string,
									   pcb->t.library_dirs, pcb->t.verbose,
									   poco_vm_module_hooks(pcb->run.vm))) < Success) {
				errline(err, "can't load poco lib.");
				return (NULL);
			}
		}
		ll->next = pcb->run.loaded_libs;
		pcb->run.loaded_libs = ll;
		return (ll);
	}
}

/*****************************************************************************
 * get the next line of library proto data, return NULL if at end of lib(s).
 *	this is called from the preprocessor, which is now the single source of
 *	input lines (no more indirect calls to a 'get next line' routine).
 *
 * 01/10/92 - (Ian)
 *			A fix to a major bug:  The builtin libs are linked into a list
 *			by our parent, and when we hit the end of a given library, we
 *			use pl->next to start working the next library in the list.
 *			Loaded libraries aren't pre-linked, they're loaded and
 *			processed one at a time.  But, as we load them (in the routine
 *			above), we link them together so that they make a list we can
 *			walk at the end of the run to free the loaded libs.  The problem
 *			occurred when multiple libraries were loaded in a single poco
 *			program via #pragma.  Upon hitting the second #pragma, we'd
 *			load the lib, and link it to the prior lib we just got done
 *			processing, then we'd start processing protos from it.  When
 *			we hit the end of the protos, the logic below would follow the
 *			link into the prior library (which had already been processed)
 *			and we'd end up with 'function XXXX redefined' errors.  (Yuck!)
 *			To fix this, another test was added:  if pcb->run.loaded_libs
 *			is non-NULL, that means we've already done the builtin libs and
 *			we're now into loaded libs.  In this case, we don't try to
 *			follow the library links, we just return NULL to say we're done
 *			with the current library.  If the pcb->run.loaded_libs pointer
 *			*is* NULL, that means we haven't started on loaded libs yet,
 *			we're still doing the builtins, and so we follow the library
 *			links until we hit the end of the linked list of builtin libs.
 * 05/17/92 (Ian)
 *			We now 'sanity check' the prototypes by ensuring that we don't
 *			get NULL proto string pointers.  If we find one, we die; it
 *			means that POCOLIB.H has gotten out of sync with our builtin
 *			libs code (POCO*.C in the root), or we have a sick POE module.
 ****************************************************************************/
char* po_get_libproto_line(Poco_cb* pcb)
{
	File_stack* fs = pcb->t.file_stack;
	Poco_lib* pl = fs->source.lib;
	const Lib_proto* pp;

	if (fs->line_count >= pl->count) {
		if (NULL != pcb->run.loaded_libs || NULL == (pl = pl->next)) {
			pcb->libfunc = NULL;
			return NULL;
		}
		fs->source.lib = pl;
		fs->line_count = 0;
	}
	pp = &pl->lib[fs->line_count++];
	pcb->libfunc = pp->func;
	pcb->libcontract = pp->contract;
	pcb->libflags = pp->flags;

	if (pp->proto == NULL) {
		pcb->global_err = Err_poco_internal;
		po_say_fatal(pcb, "NULL prototype string pointer in library %s", pl->name);
		PO_CHECK_ABORT(pcb, NULL);
	}
	return pp->proto;
}
