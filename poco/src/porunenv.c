/*******************************************************************************
 * porunenv.c - Lifetime of the compiled program image.
 *
 * Poco_run_env holds what a compile produces and a run consumes: the function
 * frames, their code and line data, the literal pool and the root struct
 * layouts.  Tearing that down is a runtime concern, so it lives here rather
 * than in the compiler.
 ******************************************************************************/

#include "poco_internal.h"
#include "pocmemry.h"
#include "poco_ffi.h"
#include "posymbol.h"
#include "struct.h"
#include "trace.h"
#include <stdlib.h>

/*****************************************************************************
 * free list of func_frames, and structures attached to each.
 ****************************************************************************/
static void free_fuf_list(Func_frame** pff)
{
	Func_frame *ff, *next;

	ff = *pff;
	while (ff != NULL) {
		next = ff->mlink;
		po_free_symbol_list(&ff->parameters);
		poc_gentle_freemem(ff->name);
		if (ff->type == CFF_POCO) {
			po_free_line_data(ff->ld);
			poc_gentle_freemem(ff->code_pt);
		}
		poc_gentle_freemem(ff->return_type);
		po_freemem(ff);
		ff = next;
	}
	*pff = NULL;
}

/*****************************************************************************
 * free the compiled program image, and the literals and functions attached to it.
 ****************************************************************************/
void po_free_run_env(Poco_run_env* pev)
{
	po_ffi_free_structures(pev);
	po_freelist(&pev->literals);
	free_fuf_list(&pev->protos);
	po_free_sif_list(&pev->struct_infos);
}
