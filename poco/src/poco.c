/****************************************************************************
 *
 * poco.c - the compile driver.
 *
 * Reserved word / built in function list initializers, the 'frame'
 * builder/destructors (one frame is built for each function, and one for the
 * program as a whole), and po_compile_file / po_compile_buffer, which drive
 * one translation unit from preprocessed source to a compressed Func_frame.
 *
 * What used to share this file now sits beside it: podiag.c (diagnostics),
 * posymbol.c (symbol table), potokface.c (token list), povar.c (variable
 * access code generation), poexpr.c (expression parser), polink.c (multi-unit
 * linker) and porunenv.c (program image teardown).  The maintenance log below
 * predates that split and still refers to this file as their home.
 *
 * MAINTENANCE:
 *	08/18/90	(Ian)
 *				Took some of the logic from next_token in token.c, and
 *				moved it to lookup_token in here.  There was a switch
 *				statement essentially duplicated between the two, and
 *				lookup_token was the only routine to call next_token.
 *				A #define was placed in poco.h to inline the lookup_token
 *				routine's check for the reuse flag, so the routine in here
 *				is now called po_lookup_freshtoken -- it will only be called
 *				when a new token is really needed.
 *				A change was made to po_memzalloc such that it directly calls
 *				laskcmem instead of being routed through po_memalloc.
 *				po_expecting_got now calls po_expecting_got_str.
 *				Changed po_hashfunc to use simple addition.
 *				Changed po_in_symbol_list to do a quick first-char test
 *				before calling the more expensive strcmp.
 *	08/22/90	(Ian)
 *				Moved po_say_err from token.c to here.
 *	08/22/90	(Jim)
 *				Changed name of static routine new_name to str_to_name.
 *	08/23/90	(Ian)
 *				Fixed bug in po_lookup_freshtoken.	Case TOK_SQUO now assigns
 *				the constant value to both pcb->curtoken->val.num and t->tval.
 *				Also fixed bug in handling end-of-file in po_lookup_freshtoken.
 *				The 'return' statement following the setting of TOK_EOF was
 *				changed to a 'break' statement.
 *	08/25/90	(Ian)
 *				Added the expression frame caching logic to po_new_expframe.
 *				See comments in that area of the code for details.
 *	08/25/90	(Ian)
 *				Changed all occurances of freemem and gentle_freemem to
 *				po_freemem and poc_gentle_freemem.
 *	08/26/90	(Ian)
 *				Added 'mblk' memory management routines.  The basic idea here
 *				is that we aquire a few large blocks of memory from the
 *				parent, and hand them out to callers of po_memalloc and po_memzalloc.
 *				When a po_freemem call is made, we don't free the memory,
 *				then when the poco program is done running, we free everything.
 *				Yes, it's a horrible kludge, but it makes poco pretty fast.
 *	08/28/90	(Ian)
 *				All the 'mblk' stuff was moved to POCMEMRY.C.  Also, the
 *				caching of expression frames and poco frames was consolidated
 *				under the new generic cache handler coded in POCMEMRY.C.
 *	08/29/90	(Ian)
 *				Began setting up setjmp/longjmp error handling (yay!).	Any
 *				call to fatal or po_say_fatal will result in a longjmp back
 *				to the compile_poco routine in POCOFACE.  Moved some error
 *				cleanup functions out of po_compile_file and into POCOFACE.C,
 *				since a longjmp will return to there.
 *				Also, physically deleted a lot of DEADWOOD code left over
 *				from the memory management changes a few versions back.
 *	08/30/90	(Ian)
 *				Made po_code_elsize routine globally visible, now used by bop.
 *	09/01/90	(Ian)
 *				Nuked po_check_rparen routine.	Tweaked po_get_unop_expression.
 *	09/18/90	(Ian)
 *				Added reserved word 'union'.
 *				Added additional error handling to not_a_member. If the
 *				structure has been defined, the "not a member" message is
 *				still issue.  If the structure is incompletely defined (a
 *				tag exists, but no members were ever specified), it issues
 *				the "undefined structure" message.
 *	09/19/90	(Ian)
 *				Big changes in type parsing to support all ANSI keywords. In
 *				this module, the various keywords were added to the table of
 *				reserved words as PTOK_TYPE.  In addition, all other type-
 *				related entries (register, struct, etc) were also changed to
 *				PTOK_TYPE instead of having their own token values.  Changes
 *				in STATEMEN, DECLARE, and STRUCT also reflect this.
 *	09/20/90	(Ian)
 *				Added error checking for attempting to index a void
 *				pointer (eg, void *a; a[2] = 3; is illegal).  Also, attempt
 *				to dereference a void pointer using '*' now has its own error
 *				message, instead of reporting 'confused pointer dereference'.
 *	10/01/90	(Ian)
 *				Added support for enum constant symbols to po_lookup_freshtoken.
 *	10/02/90	(Ian)
 *				Modifications to sizeof handling; it now deals with any
 *				expression (ANSI-style).  It does not yet handle type or
 *				typedef names, (or expressions involving types: sizeof(int *)).
 *	10/02/90	(Ian)
 *				Fixed bug in not_an_element that was causing a page fault.
 *	10/04/90	(Ian)
 *				Fixed a glitch in make_assign, it now propogates the
 *				pure_const value to the target expression frame, so that we
 *				can properly detect non-constant init expressions for static,
 *				array, and struct initializer expressions.
 *				Added support for static vars, which constisted primarily
 *				of changing instances of var->scope to var->storage_scope
 *				when generating code to access a variable.	Changes in the
 *				po_new_var_space routine cause storage_scope to be set
 *				based upon the scope the variable was declared at and whether
 *				it was declared static or not.	(A static declaration implies
 *				a local symbol scope, but a global storage scope for that
 *				symbol.)
 *	10/05/90	(Ian)
 *				Tweaked po_say_err and po_say_fatal.  po_say_err now take a
 *				pointer to the poco_cb, instead of to a token, just for
 *				consistancy.  po_say_fatal now takes printf-type args.
 *	10/05/90	(Ian)
 *				Converted sprintf/po_say_fatal pairs to po_say_fatal w/formatting.
 *				Added po_say_internal to issue internal error messages.
 *	10/07/90	(Ian)
 *				Removed NULL checks from calls to memory allocation.
 *	10/20/90	(Ian)
 *				Fixed ref_op so that arrays can be accessed via '*'.
 *				Fixed po_find_local_assign and find_global_assign so that
 *				the value of the name of an array cannot be modified.  EG:
 *					char a[5];
 *					something = *a; 	// this is now legal
 *					++a;				// this is now illegal
 *	10/21/90	(Ian)
 *				Changed pcb->next_line to po_pp_next_line in freshtoken.  The
 *				preprocessor is now the single source of input lines from
 *				our point of view in the parser...no more indirection.
 *	10/22/90	(Ian)
 *				Added new_token, free_token, po_free_token_lists, and
 *				po_build_token_list routines, all part of supporting multi-
 *				token lookahead. Rewrote po_lookup_freshtoken correspondingly.
 *				Changed references to ts[0].whatever to curtoken->whatever.
 *				Changed references to t.ctoke to curtoken->ctoke.
 *	10/24/90	(Ian)
 *				Moved po_cat_exp to bop.c and made it static.
 *				Added TYPE_ARRAY case to outer switch of po_coerce_expression,
 *				to make array names and pointers more interchangable. Also,
 *				removed prior changes to ref_op and po_ind_op.
 *	10/26/90	(Ian)
 *				Added check for stack overflow in lookup_freshtoken.
 *				Changed all references to curtoken->type to t.toktype; added
 *				line to freshtoken to set t.toktype.  This removes one
 *				level of indirection in accessing the most-frequently used
 *				field in the token data.
 *				Added an extra check to po_get_pmember, now the type_info of
 *				the pointer must have exactly 2 type_comps, eg, it must be
 *				a single level of indirection and point directly to a struct.
 *	10/28/90	(Ian)
 *				Implemented concatenation of adjacent string literals.	Also,
 *				removed calls to translate_escapes, that functionality is
 *				now built in to the tokenize_word routine.
 *				Fixed a glitch in freshtoken; now a token is only classified
 *				as is_symbol == TRUE if the symbol token type is PTOK_VAR,
 *				PTOK_LABEL, or PTOK_UNDEF.	This prevents keywords from
 *				being used as variable names within non-global scopes, which
 *				was occuring because of the way force_local_symbol works.
 *	10/29/90	(Ian)
 *				Tightened type conversion rules in upgrade_to_ido, we now
 *				prevent attempts to convert IDO_CPT or IDO_VPT to numbers.
 *	10/29/90	(Ian)
 *				Once again attempted to fix problem with making pointers and
 *				array names interchangable.  The 'fix' done on 10/20/90 was
 *				to change ref_op and po_ind_op to handle TYPE_ARRAY the
 *				same as TYPE_POINTER.  This failed dismally, and was backed
 *				out on 10/24/90.  This time, taking our clue from the way
 *				po_get_array works, the make_deref routine looks at the
 *				return value from ref_op or po_ind_op, and if the value
 *				is -1 and the current end type is TYPE_ARRAY, we exit without
 *				further action (a -1 return for any other end type is still
 *				an error.)	The weirdness here is in ref_op and po_ind_op.
 *				If they see a comp_count of 1, they will return the right
 *				opcode.  But, in the case of code such as...
 *					char a[5][5];
 *					**a = 0x00;
 *				...the comp_count will be two during the making of the first
 *				of the two derefs.	Thus, po_ind_op returns -1, to say that
 *				there is no opcode which does this type of derefence.  And,
 *				that's pretty much true.  Since 'a' is an array, an
 *				OP_xxx_ADDRESS has already been generated to stack the
 *				pointer/address value.	To make the first deref, we would
 *				add an offset of zero to the address on the stack, which is
 *				pointless, so we just exit without complaining.  When the
 *				parser un-recurses back to the point of making the deref
 *				on the first star, po_ind_op will now see a comp_count of just
 *				one, so it will return OP_CI_ASS to make the deref.
 *	10/29/90	(Ian)
 *				Added logic to po_rehash to detect a symbol with a null
 *				string (\0) name.  This indicates that within a prototype
 *				which has a code block following it, somebody forgot to
 *				code the parm name(s).	Since po_rehash doesn't get called
 *				unless there is a code block, this allows true prototypes
 *				to be specified without names.
 *	02/07/91	(Ian)
 *				Added 'Errcode' as a variant spelling of 'ErrCode' in the
 *				builtin datatypes list.
 *	05/01/91	(Ian)
 *				Added formatting of error text to po_say_error when the
 *				error status is not Err_syntax.
 *	09/06/91	(Jim)
 *		o		Started to add in String type.	Changed unused TYPE_RAST
 *				to TYPE_STRING.
 *		o		Noticed that the use of TYPE_CPT and
 *				TYPE_NCPT were inconsistant.  The only place either
 *				was generated is TYPE_NCPT for pointer parameters to
 *				library routines that take variable arguments past
 *				the ellipsis.  But elsewhere in code TYPE_CPT was
 *				looked for.  The difference between the two
 *				was simply whether a NULL-check was inserted.
 *				This ended up not being such a hot idea, so
 *				I took the NULL-check out of the type-coercion
 *				of TYPE_CPT, and eliminated TYPE_NCPT
 *		o		Changed name of upgrade_to_ido to
 *				upgrade_numerical_expression.
 *		o		Made po_coerce_numeric_exp not accept void as a numeric type.
 *		o		Got rid of OP_IXXX+ido_type stuff.	Now is
 *				po_xxx_ops[ido_type].
 *	05/31/92	(Ian)
 *				Changed po_get_member so that the code from the left buffer
 *				gets moved to the right buffer to access an array inside
 *				a structure.
 *	06/03/92	(Ian)
 *				Changed make_deref so that the code from the right buffer
 *				(the pointer expression) gets moved to the left buffer when
 *				the end type is TYPE_ARRAY.  This keeps the lval properly
 *				updated during expression parsing, but defers the generation
 *				of an actual ref-op until the subscript expression or
 *				dereferencing '*' is processed.  Oddly enough, this was
 *				only a problem with arrays in structures accessed via a
 *				pointer to the structure, because that's the only time when
 *				the pointer expression in the right buffer differed from
 *				the lval expression in the left buffer.
 *	08/17/92	(Ian)
 *			  > Changed error reporting a bit.	The pcb has a new field,
 *				error_line_number.	If this is zero, we take the line number
 *				from curtoken or fs->line_count as we used to, and we save
 *				the number in error_line_number.  If it is non-zero, we
 *				just go with it.  The latter happens when the preprocessor
 *				detects an error -- it sets error_line_number before calling
 *				po_say_fatal, since curtoken won't have the right line
 *				number on a preprocessor error.  error_line_number gets passed
 *				back to the host as the line to position the builtin editor to.
 *				Also, added a new function, po_say_warning.  This is like
 *				po_say_error except that error_line_number is NOT saved.
 *			  > An unexpected '}' is now reported as error, by compile_file.
 *			  > Changed po_assign_after_equals to handle checking for
 *				constant init expressions before making the assignment.
 *			  > get_address now propogates pure_const to the destination
 *				expression frame.
 *	09/14/92	(Ian)
 *				Fixed po_say_fatal to check for t.file_stack being NULL
 *				before it tries to access t.file_stack->line_number.
 ****************************************************************************/

#include "poco_internal.h"
#include "bop.h"
#include "code.h"
#include "pocmemry.h"
#include "poco_ffi.h"
#include "pocodis.h"
#include "pocotype.h"
#include "polink.h"
#include "posymbol.h"
#include "potokface.h"
#include "povar.h"
#include "pp.h"
#include "statemen.h"
#include "struct.h"
#include "trace.h"
#include <stdlib.h>
#include <string.h>




/**************** MODULE COMPILE **********************************/

/*****************************************************************************
 * alloc & init a poco_frame (and the structures that attach to it, if needed).
 *
 * a poco_frame is a place where code, symbols, and struct_info's live.
 * the poco_frame is used during compilation, but is reduced to a function
 * frame or struct_info once the appropriate point has been reached in the
 * parsing.  (When the prototype, function block, or struct definition has
 * been parsed.)
 *
 * FTY_STRUCT type frames don't get code buffer and line_data structures,
 * since these don't mean anything in the context of parsing a prototype
 * or structure definition (this also lightens up on malloc() a bit).
 ****************************************************************************/
bool po_new_frame(Poco_cb* pcb, int scope, char* name, int type)
{
	Poco_frame* pf;

	pf = po_cache_malloc(pcb, &pcb->pocf_cache);

	poco_zero_bytes(pf, sizeof(Poco_frame) + (HASH_SIZE * sizeof(Symbol*)));

	if (type != FTY_STRUCT) /* no line data or code buf for struct/proto */
	{
		pf->ld = po_new_line_data(pcb);
		po_init_code_buf(pcb, &pf->fcd);
	}

	pf->hash_table = (Symbol**)(pf + 1);
	pf->name = name;
	pf->next = pcb->rframe;
	pcb->rframe = pf;
	pf->scope = scope;
	pf->frame_type = type;
	if (type == FTY_GLOBAL) {
		pf->doff = -pcb->run.data_size;
	}

	return true;
}

/*****************************************************************************
 * return to parent frame, free child frame and any structures attached to it.
 ****************************************************************************/
void po_old_frame(Poco_cb* pcb)
{
	struct poco_frame* rf;

	if ((rf = pcb->rframe) != NULL) {
		pcb->rframe = rf->next;

		if (rf->symbols) {
			po_free_symbol_list(&rf->symbols);
		}
		if (rf->fsif) {
			po_free_sif_list(&rf->fsif);
		}

		if (rf->frame_type != FTY_STRUCT) {
			po_trash_code_buf(pcb, &rf->fcd);
			po_free_line_data(rf->ld);
		}
		po_freemem(rf);
	}
}

/*****************************************************************************
 * Make up symbolic tokens for reserved words, add symbols to root poco_frame.
 ****************************************************************************/
static Errcode init_reserved_words(Poco_cb* pcb)
{
	int i;
	Symbol* n;
	Poco_frame* rf = pcb->rframe;

	static struct rwinit {
		char* string;
		SHORT type;
		SHORT val;
	} const rwi[] = {
		{
			"void",
			PTOK_TYPE,
			TYPE_VOID,
		},
		{
			"char",
			PTOK_TYPE,
			TYPE_CHAR,
		},
		{
			"short",
			PTOK_TYPE,
			TYPE_SHORT,
		},
		{
			"int",
			PTOK_TYPE,
			TYPE_INT,
		},
		{
			"long",
			PTOK_TYPE,
			TYPE_LONG,
		},
		{
			"float",
			PTOK_TYPE,
			TYPE_FLOAT,
		},
		{"double", PTOK_TYPE, TYPE_DOUBLE},
		{"signed", PTOK_TYPE, TYPE_SIGNED},
		{"unsigned", PTOK_TYPE, TYPE_UNSIGNED},
		{"Screen", PTOK_TYPE, TYPE_SCREEN},
#ifdef STRING_EXPERIMENT
		{"String", PTOK_TYPE, TYPE_STRING},
#endif /* STRING_EXPERIMENT */
		{"ErrCode", PTOK_TYPE, TYPE_INT},
		{"Errcode", PTOK_TYPE, TYPE_INT},
		{"Boolean", PTOK_TYPE, TYPE_CHAR},
		{"FILE", PTOK_TYPE, TYPE_FILE},
		{"struct", PTOK_TYPE, TYPE_STRUCT},
		{"union", PTOK_TYPE, TYPE_UNION},
		{"enum", PTOK_TYPE, TYPE_ENUM},
		{"const", PTOK_TYPE, TYPE_CONST},
		{"volatile", PTOK_TYPE, TYPE_VOLATILE},
		{"extern", PTOK_TYPE, TYPE_EXTERN},
		{"static", PTOK_TYPE, TYPE_STATIC},
		{"auto", PTOK_TYPE, TYPE_AUTO},
		{"register", PTOK_TYPE, TYPE_REGISTER},
		{"typedef", PTOK_TYPEDEF, 0},
		{
			"...",
			PTOK_ELLIPSIS,
			TYPE_ELLIPSIS,
		},
		{"for", PTOK_FOR, 0},
		{"if", PTOK_IF, 0},
		{"while", PTOK_WHILE, 0},
		{"return", PTOK_RETURN, 0},
		{"switch", PTOK_SWITCH, 0},
		{"goto", PTOK_GOTO, 0},
		{"do", PTOK_DO, 0},
		{"else", PTOK_ELSE, 0},
		{"break", PTOK_BREAK, 0},
		{"continue", PTOK_CONTINUE, 0},
		{"sizeof", PTOK_SIZEOF, 0},
		{"NULL", PTOK_NULL, 0},
		{"case", PTOK_CASE, 0},
		{"default", PTOK_DEFAULT, 0},
	};

	for (i = 0; i < Array_els(rwi); i++) {
		if ((n = po_new_symbol_tok(pcb, rwi[i].string, rwi[i].type)) == NULL) {
			return (Err_no_memory);
		}
		n->symval.i = rwi[i].val;
		n->link = rf->parameters;
		rf->parameters = n;
	}
	return Success;
}

/*****************************************************************************
 * free a list of c_frames, and the list of symbols attached to each.
 * (Note to self: Does this routine do anything?  A C_frame is typedef'd as
 * a fuf in poco.h, look into this.)
 * -jk - looks dead to me.	I believe pcb->cframes is always NULL now....
 * This is probably a relic from before library functions had ascii
 * prototypes.
 ****************************************************************************/
static void free_cframes(Poco_cb* pcb, C_frame** cframes)
{
	(void)pcb;
	C_frame *next, *cf;

	cf = *cframes;
	while (cf != NULL) {
		next = cf->next;
		po_free_symbol_list(&cf->parameters);
		po_freemem(cf);
		cf = next;
	}
	*cframes = NULL;
}

/*****************************************************************************
 * walk symbol list of root frame, look for referenced functions with no code.
 ****************************************************************************/
bool po_check_undefined_funcs(Poco_cb* pcb, Symbol* sl)
{
	Type_info* ti;
	Func_frame* fuf;
	bool ok = true;

	while (sl != NULL) {
		if (sl->flags & SFL_USED) {
			if (po_is_func(sl->ti)) {
				ti = sl->ti;
				fuf = ti->sdims[ti->comp_count - 1].pt;
				if (!fuf->got_code) {
					po_say_fatal(pcb, "function '%s' not found in source code or builtin library",
								 sl->name);
				}
			}
		}
		sl = sl->link;
	}
	return ok;
}

/*****************************************************************************
 * the error code to report for a failure that only signalled itself through
 * the pcb (po_say_fatal and friends), or that reported no code at all.
 ****************************************************************************/
static Errcode compile_failure_code(Poco_cb* pcb)
{
	if (pcb->compile_err < Success) {
		return pcb->compile_err;
	}
	return Err_syntax;
}

/*****************************************************************************
 * compile pcb->file into pcb->run.fff.  Returns Success or a negative Errcode.
 ****************************************************************************/
static Errcode po_compile_source(Poco_cb* pcb, char* name, const char* source,
								 size_t source_length, bool from_buffer)
{
	Tstack* dummy_token;
	Func_frame* fuf = NULL;
	Poco_frame* pf = NULL;
	bool globals_retained = false;
	Struct_info* previous_struct_infos = pcb->run.struct_infos;
	Struct_info* struct_tail;
	Errcode err = Success;

	/*
	 * These validate static tables, not this compilation unit, and they bail
	 * out with a bare return rather than through BADOUT.  Run them before the
	 * first per-unit allocation so there is nothing for that early return to
	 * strand.
	 */
#ifdef DEVELOPMENT
	if (!po_check_instr_table(pcb)) {
		po_say_internal(pcb, "instruction table failed self-check\n");
		PO_CHECK_ABORT(pcb, compile_failure_code(pcb));
	}
	if (!po_check_type_names(pcb)) {
		po_say_internal(pcb, "type_names table failed self-check\n");
		PO_CHECK_ABORT(pcb, compile_failure_code(pcb));
	}
	if (!po_check_ido_table(pcb)) {
		po_say_internal(pcb, "ido_table table failed self-check\n");
		PO_CHECK_ABORT(pcb, compile_failure_code(pcb));
	}
#endif

	pcb->current_unit_name = po_clone_string(pcb, name);
	if (pcb->current_unit_name == NULL) {
		return Err_no_memory;
	}

	po_init_qbop_table(pcb);

	if (from_buffer) {
		po_init_pp_buffer(pcb, name, source, source_length);
	} else {
		po_init_pp(pcb, name);
	}

	if (po_new_frame(pcb, SCOPE_GLOBAL, name, FTY_GLOBAL)) {
		pf = pcb->rframe;
		if ((err = init_reserved_words(pcb)) < Success) {
			goto BADOUT;
		}
		if (!po_import_used_symbols(pcb)) {
			err = compile_failure_code(pcb);
			goto BADOUT;
		}

		/*
		 * The lookahead sentinel is returned to free_tokens by the first
		 * lookup.  Allocate it from Poco's managed heap so cleanup can safely
		 * walk that list on every parser exit path.
		 */
		dummy_token = po_memzalloc(pcb, sizeof(*dummy_token));
		pcb->curtoken = dummy_token;
		pcb->free_tokens = NULL;
		dummy_token->next = po_build_token_list(pcb);

		po_get_statements(pcb, pf); /* returns on EOF or unexpected RBRACE */
		lookup_token(pcb);
		if (pcb->t.toktype != TOK_EOF) {
			po_say_fatal(pcb, "unexpected '}'");
			if (pcb->compile_aborted) {
				err = compile_failure_code(pcb);
				goto BADOUT;
			}
		}

		po_code_op(pcb, &pf->fcd, OP_END);
		fuf = po_memzalloc(pcb, sizeof(*fuf));
		fuf->name = po_clone_string(pcb, name);
		fuf->mlink = pcb->run.protos;
		pcb->run.protos = fuf;
		if (!po_compress_func(pcb, pf, fuf)) {
			err = compile_failure_code(pcb);
			goto BADOUT;
		}
		fuf->parameters = po_retain_global_variables(pf, &fuf->pcount);
		globals_retained = true;
		po_dump_file(pcb);
		pcb->run.data_size = -pf->doff;
	} else {
		err = Err_no_memory;
	}

BADOUT:

	po_free_token_lists(pcb);

	/* pf is NULL when the global frame was never built; everything below that
	 * touches it has to be skipped rather than faulting on the error path. */
	if (pf != NULL) {
		po_free_symbol_list(&pf->parameters); /* free res. words */
	}

	if (fuf != NULL && !globals_retained) {
		fuf->parameters = NULL; /* we just freed these above! */
	}

	/* Struct-valued C bindings build their libffi descriptors after parsing
	 * returns.  Keep the root layouts (including member symbols) alive until
	 * the compiled run environment is destroyed. */
	pcb->run.struct_infos = (pf != NULL) ? pf->fsif : NULL;
	if (pcb->run.struct_infos == NULL) {
		pcb->run.struct_infos = previous_struct_infos;
	} else {
		struct_tail = pcb->run.struct_infos;
		while (struct_tail->next != NULL) {
			struct_tail = struct_tail->next;
		}
		struct_tail->next = previous_struct_infos;
	}
	if (pf != NULL) {
		pf->fsif = NULL;
	}

	po_old_frame(pcb);

	free_cframes(pcb, &pcb->cframes);

	po_free_pp(pcb);

	/* A fatal diagnostic unwinds by setting compile_aborted rather than by
	 * returning a code, so the pcb has the last word on success. */
	if (err >= Success && pcb->compile_aborted) {
		err = compile_failure_code(pcb);
	}
	return err;
}

Errcode po_compile_file(Poco_cb* pcb, char* name)
{
	return po_compile_source(pcb, name, NULL, 0, false);
}

Errcode po_compile_buffer(Poco_cb* pcb, char* name, const char* source, size_t source_length)
{
	return po_compile_source(pcb, name, source, source_length, true);
}
