/*******************************************************************************
 * povar.c - Variable access code generation.
 * Chooses the opcode that reads, writes, references or dereferences a
 * variable for a given type, and compiles array subscripts and structure
 * member access.  Split out of poco.c.
 ******************************************************************************/

#include "poco_internal.h"
#include "code.h"
#include "fold.h"
#include "pocotype.h"
#include "posymbol.h"
#include "povar.h"
#include <stdlib.h>
#include <string.h>

/******* MODULE VARIABLE stuff to assign and use variables *******/

/*****************************************************************************
 * issue void assignment error message, and die.
 ****************************************************************************/
static void no_assign_void(Poco_cb* pcb)
{
	po_say_fatal(pcb, "can't assign to void variable");
	PO_CHECK_ABORT_VOID(pcb);
}

/*****************************************************************************
 * issue error message about assignment type, and die.
 ****************************************************************************/
static void unknown_assignment(Poco_cb* pcb)
{
	po_say_fatal(pcb, "unknown type in assignment");
	PO_CHECK_ABORT_VOID(pcb);
}

/*****************************************************************************
 * issue error message about assignment of struct by value, and die.
 ****************************************************************************/
static void no_struct_assign(Poco_cb* pcb)
{
	po_say_fatal(pcb, "poco can't do struct assignments");
	PO_CHECK_ABORT_VOID(pcb);
}

/*****************************************************************************
 * return the proper opcode for a local variable assignment.
 ****************************************************************************/
SHORT po_find_local_assign(Poco_cb* pcb, Type_info* ti)
{
	SHORT aop;

	if (ti->comp_count == 1) {
		switch (ti->comp[0]) {
			case TYPE_VOID:
				no_assign_void(pcb);
				break;
			case TYPE_CHAR:
				aop = OP_LOC_CASS;
				break;
			case TYPE_SHORT:
				aop = OP_LOC_SASS;
				break;
			case TYPE_INT:
				aop = OP_LOC_IASS;
				break;
			case TYPE_LONG:
				aop = OP_LOC_LASS;
				break;
			case TYPE_FLOAT:
				aop = OP_LOC_FASS;
				break;
			case TYPE_DOUBLE:
				aop = OP_LOC_DASS;
				break;
			case TYPE_STRUCT:
				no_struct_assign(pcb);
				break;
#ifdef STRING_EXPERIMENT
			case TYPE_STRING:
				aop = OP_LOC_STRING_ASS;
				break;
#endif /* STRING_EXPERIMENT */
			default:
				unknown_assignment(pcb);
				break;
		}
	} else {
		if (po_is_array(ti)) {
			po_say_fatal(pcb, "lvalue required");
			PO_CHECK_ABORT(pcb, 0);
		} else {
			aop = OP_LOC_PASS;
		}
	}
	return aop;
}

/*****************************************************************************
 * return the proper opcode for a global variable assignment.
 ****************************************************************************/
static SHORT find_global_assign(Poco_cb* pcb, Type_info* ti)
{
	SHORT aop;

	if (ti->comp_count == 1) {
		switch (ti->comp[0]) {
			case TYPE_VOID:
				no_assign_void(pcb);
				break;
			case TYPE_CHAR:
				aop = OP_GLO_CASS;
				break;
			case TYPE_SHORT:
				aop = OP_GLO_SASS;
				break;
			case TYPE_INT:
				aop = OP_GLO_IASS;
				break;
			case TYPE_LONG:
				aop = OP_GLO_LASS;
				break;
			case TYPE_FLOAT:
				aop = OP_GLO_FASS;
				break;
			case TYPE_DOUBLE:
				aop = OP_GLO_DASS;
				break;
			case TYPE_STRUCT:
				no_struct_assign(pcb);
				break;
#ifdef STRING_EXPERIMENT
			case TYPE_STRING:
				aop = OP_GLO_STRING_ASS;
				break;
#endif /* STRING_EXPERIMENT */
			default:
				unknown_assignment(pcb);
				break;
		}
	} else {
		if (po_is_array(ti)) {
			po_say_fatal(pcb, "lvalue required");
			PO_CHECK_ABORT(pcb, 0);
		} else {
			aop = OP_GLO_PASS;
		}
	}
	return (aop);
}

/*****************************************************************************
 * return the right opcode to make a variable assignment.
 ****************************************************************************/
SHORT po_find_assign_op(Poco_cb* pcb, Symbol* var, Type_info* ti)
{
	return (var->storage_scope == SCOPE_GLOBAL ? find_global_assign(pcb, ti)
											   : po_find_local_assign(pcb, ti));
}

/*****************************************************************************
 * issue error message about using a void value, and die.
 ****************************************************************************/
static void no_use_void(Poco_cb* pcb)
{
	po_say_fatal(pcb, "can't use void value");
	PO_CHECK_ABORT_VOID(pcb);
}

/*****************************************************************************
 * issue error message that variable type is too complex, and die.
 ****************************************************************************/
void po_var_too_complex(Poco_cb* pcb)
{
	po_say_fatal(pcb, "variable too complicated to use...");
	PO_CHECK_ABORT_VOID(pcb);
}

const Ido_table po_ido_table[] =
	/* Table that lets us quickly determine what operations are legal on
	 * a certain IDO_TYPE */
	{
		/**  is_num can_add ido_type	**/
		{
			true,
			true,
			IDO_INT,
		},
		{
			true,
			true,
			IDO_LONG,
		},
		{
			true,
			true,
			IDO_DOUBLE,
		},
		{
			false,
			true,
			IDO_POINTER,
		},
		{
			false,
			true,
			IDO_CPT,
		},
		{
			false,
			false,
			IDO_VOID,
		},
		{
			false,
			false,
			IDO_VPT,
		},
#ifdef STRING_EXPERIMENT
		{
			false,
			false,
			IDO_STRING,
		},
#endif /* STRING_EXPERIMENT */
		{
			false,
			false,
			IDO_STRUCT,
		},
};

/*****************************************************************************
 * Run a sanity check on table to correlate IDO-types with instructions
 * to pop result of expression stack.
 ****************************************************************************/
bool po_check_ido_table(Poco_cb* pcb)
{
#ifdef DEVELOPMENT
	size_t i;

	for (i = 0; i < Array_els(po_ido_table); i++) {
		if (i != (size_t)po_ido_table[i].ido_type) {
			fprintf(pcb->t.err_file, "%d != %d\n", i, po_ido_table[i].ido_type);
			po_say_internal(pcb, "po_ido_table doesn't check");
			PO_CHECK_ABORT(pcb, false);
			return (false);
		}
	}
#endif
	return (true);
}

/*****************************************************************************
 * return the proper opcode to use a global variable.
 ****************************************************************************/
static SHORT find_global_use(Poco_cb* pcb, Type_info* ti)
{
	SHORT op;

	if (ti->ido_type == IDO_POINTER) {
		op = OP_GLO_PVAR;
	} else if (ti->comp_count == 1) {
		switch (ti->comp[0]) {
			case TYPE_VOID:
				no_use_void(pcb);
				break;
			case TYPE_CHAR:
				op = OP_GLO_CVAR;
				break;
			case TYPE_SHORT:
				op = OP_GLO_SVAR;
				break;
			case TYPE_INT:
				op = OP_GLO_IVAR;
				break;
			case TYPE_LONG:
				op = OP_GLO_LVAR;
				break;
			case TYPE_FLOAT:
				op = OP_GLO_FVAR;
				break;
			case TYPE_DOUBLE:
				op = OP_GLO_DVAR;
				break;
			case TYPE_STRUCT:
				op = OP_GLO_FVAR; /* filled in later */
				break;
#ifdef STRING_EXPERIMENT
			case TYPE_STRING:
				op = OP_GLO_STRING_VAR;
				break;
#endif /* STRING_EXPERIMENT */
			default:
				goto ERR;
		}
	} else {
ERR:
		po_var_too_complex(pcb);
	}
	return (op);
}

/*****************************************************************************
 * return the proper opcode to use a local variable.
 ****************************************************************************/
SHORT po_find_local_use(Poco_cb* pcb, Type_info* ti)
{
	SHORT op;

	if (ti->ido_type == IDO_POINTER) {
		op = OP_LOC_PVAR;
	} else if (ti->comp_count == 1) {
		switch (ti->comp[0]) {
			case TYPE_VOID:
				no_use_void(pcb);
				break;
			case TYPE_CHAR:
				op = OP_LOC_CVAR;
				break;
			case TYPE_SHORT:
				op = OP_LOC_SVAR;
				break;
			case TYPE_INT:
				op = OP_LOC_IVAR;
				break;
			case TYPE_LONG:
				op = OP_LOC_LVAR;
				break;
			case TYPE_FLOAT:
				op = OP_LOC_FVAR;
				break;
			case TYPE_DOUBLE:
				op = OP_LOC_DVAR;
				break;
			case TYPE_STRUCT:
				op = OP_LOC_FVAR; /* filled in later */
				break;
#ifdef STRING_EXPERIMENT
			case TYPE_STRING:
				op = OP_LOC_STRING_VAR;
				break;
#endif /* STRING_EXPERIMENT */
			default:
				goto ERR;
		}
	} else {
ERR:
		po_var_too_complex(pcb);
	}
	return (op);
}

/*****************************************************************************
 * return the proper opcode to use a variable.
 ****************************************************************************/
static SHORT find_use_op(Poco_cb* pcb, Symbol* var, Type_info* ti)
{
	return (var->storage_scope == SCOPE_GLOBAL ? find_global_use(pcb, ti)
											   : po_find_local_use(pcb, ti));
}

/*****************************************************************************
 * return the proper opcode for an indirect use of a variable.
 ****************************************************************************/
static SHORT ref_op(Poco_cb* pcb, Type_info* ti)
{
	(void)pcb;

	if (po_is_pointer(ti)) {
		return (OP_PI_VAR);
	} else {
		if (ti->comp_count != 1) {
			return (-1);
		} else {
			switch (ti->comp[0]) {
				case TYPE_CHAR:
					return (OP_CI_VAR);
				case TYPE_SHORT:
					return (OP_SI_VAR);
				case TYPE_INT:
					return (OP_II_VAR);
				case TYPE_LONG:
					return (OP_LI_VAR);
				case TYPE_FLOAT:
					return (OP_FI_VAR);
				case TYPE_DOUBLE:
					return (OP_DI_VAR);
				case TYPE_STRUCT: /* return dummy value, will be patched later */
					return (OP_II_VAR);
#ifdef STRING_EXPERIMENT
				case TYPE_STRING:
					return (OP_STRING_I_VAR);
#endif /* STRING_EXPERIMENT */
				default:
					return (-1);
			}
		}
	}
}

/*****************************************************************************
 * return the proper opcode for an indirect assign of a variable.
 ****************************************************************************/
SHORT po_ind_op(Poco_cb* pcb, Type_info* ti)
{
	(void)pcb;

	if (po_is_pointer(ti)) {
		return (OP_PI_ASS);
	} else {
		if (ti->comp_count != 1) {
			return (-1);
		} else {
			switch (ti->comp[0]) {
				case TYPE_CHAR:
					return (OP_CI_ASS);
				case TYPE_SHORT:
					return (OP_SI_ASS);
				case TYPE_INT:
					return (OP_II_ASS);
				case TYPE_LONG:
					return (OP_LI_ASS);
				case TYPE_FLOAT:
					return (OP_FI_ASS);
				case TYPE_DOUBLE:
					return (OP_DI_ASS);
				case TYPE_STRUCT: /* return dummy value, will be patched later */
					return (OP_END);
#ifdef STRING_EXPERIMENT
				case TYPE_STRING:
					return (OP_STRING_I_ASS);
#endif /* STRING_EXPERIMENT */
				default:
					return (-1);
			}
		}
	}
}

/*****************************************************************************
 * generate radix value to reach an element of an array.
 ****************************************************************************/
void po_code_elsize(Poco_cb* pcb, Exp_frame* e, int el_size)
{
	int bipower;
	int i;

	if (el_size == 0) {
		po_say_fatal(pcb, "size of type is zero (eg, pointer to void)");
	}
	PO_CHECK_ABORT_VOID(pcb);

	if (el_size != 1) {
		bipower = 2;
		for (i = 1; i < 15; i++) /* try to do it as a shift... */
		{
			if (el_size == bipower) {
				if (e->ctc.ido_type == IDO_INT) {
					po_code_int(pcb, &e->ecd, OP_ICON, i);
				} else {
					po_code_long(pcb, &e->ecd, OP_LCON, (long)i);
				}
				po_code_op(pcb, &e->ecd, po_lshift_ops[e->ctc.ido_type]);
				goto GOT_SCALING;
			}
			bipower <<= 1;
		}
		/* if it's not shiftable force index to be computed with long multiply */
		po_coerce_numeric_exp(pcb, e, IDO_LONG);
		po_code_long(pcb, &e->ecd, OP_LCON, el_size);
		po_code_op(pcb, &e->ecd, OP_LMUL);
GOT_SCALING:
		po_fold_const(pcb, e);
	}
}

/*****************************************************************************
 * generate code to reach a given element in an array.
 ****************************************************************************/
void po_get_array(Poco_cb* pcb, Exp_frame* e)
{
	Type_info* ti;
	Exp_frame iex;
	long el_size;
	int op;
	int end_type = e->ctc.comp[e->ctc.comp_count - 1];

	if (!any_code(pcb, &e->left)) {
		po_say_fatal(pcb, "bizarre circumstances for array.");
		PO_CHECK_ABORT_VOID(pcb);
		return;
	}
	if (end_type == TYPE_POINTER) {
		/* if it's a pointer move the rval to the lval at this point... */
		po_copy_code(pcb, &e->ecd, &e->left);
	} else if (end_type == TYPE_ARRAY) {
		/* do nothing */
	} else {
		po_say_fatal(pcb, "indexing non-pointer");
		PO_CHECK_ABORT_VOID(pcb);
		return;
	}
	ti = &e->ctc;
	ti->comp_count -= 1;
	po_set_ido_type(ti);
	el_size = po_get_type_size(ti);
	po_init_expframe(pcb, &iex);
	po_get_expression(pcb, &iex);
	if (po_force_num_exp(pcb, &iex.ctc) < 0) {
		goto TRASHIT;
	}
	if (iex.ctc.ido_type != IDO_INT) {
		po_coerce_numeric_exp(pcb, &iex, IDO_LONG);
	}
	if (!po_eat_rbracket(pcb)) {
		goto TRASHIT;
	}
	po_code_elsize(pcb, &iex, el_size);
	po_code_op(pcb, &iex.ecd, po_add_offset_ops[iex.ctc.ido_type]);
	/* have computed parts of array expression common to left and right side... */
	po_concatenate_code(pcb, &e->left, &iex.ecd); /* this is all for left side */
	po_concatenate_code(pcb, &e->ecd, &iex.ecd);  /* Right side still needs a OP_XREF */
	if ((op = ref_op(pcb, ti)) < 0) {
		goto TRASHIT;
	}
	po_code_op(pcb, &e->ecd, op);
	e->left_complex = true;
TRASHIT:
	po_trash_expframe(pcb, &iex);
}

/*****************************************************************************
 * generate code to dereference a pointer.
 ****************************************************************************/
void po_make_deref(Poco_cb* pcb, Exp_frame* e)
{
	SHORT op;

	e->left_complex = true;
	if (e->ctc.ido_type != IDO_VPT) {
		po_copy_code(pcb, &e->ecd, &e->left);
		if ((op = ref_op(pcb, &e->ctc)) < 0) {
			if (e->ctc.ido_type == IDO_VOID) {
				po_say_fatal(pcb, "cannot dereference a void pointer");
				PO_CHECK_ABORT_VOID(pcb);
			} else if (e->ctc.comp[e->ctc.comp_count - 1] == TYPE_ARRAY) {
				/* if we got a negative (bad) refop because the current
				   end type is TYPE_ARRAY, then we do nothing, because we
				   will generate the refop (if even needed) when we see
				   the subscript expression or when we unrecurse and
				   generate the code for the '*' in front of the name.
				*/
			} else {
				po_say_fatal(pcb, "confused pointer dereference");
				PO_CHECK_ABORT_VOID(pcb);
			}
		} else {
			po_code_op(pcb, &e->ecd, op);
		}
	}

	// printf("Exit rval:\n");
	// po_dump_codebuf(pcb, &e->ecd);
	// printf("Exit lval:\n");
	// po_dump_codebuf(pcb, &e->left);
}

/*****************************************************************************
 * issue error message about struct/union access, then die.
 ****************************************************************************/
static void not_a_member(Poco_cb* pcb, Struct_info* si, char* mbrname)
{
	char* structname;
	char* type;

	structname = (si->name == NULL) ? "\0" : si->name;

	type = (si->type == TYPE_UNION) ? "union" : "struct";

	if (si->size == 0) {
		po_say_fatal(pcb, "elements for %s type %s have not been defined", type, structname);
		PO_CHECK_ABORT_VOID(pcb);
	} else {
		po_say_fatal(pcb, "%s isn't a member of %s %s", mbrname, type, structname);
		PO_CHECK_ABORT_VOID(pcb);
	}
}

/*****************************************************************************
 * generate code to access a struct/union member being accessed via pointer.
 ****************************************************************************/
void po_get_pmember(Poco_cb* pcb, Exp_frame* e)
{
	PO_CHECK_ABORT_VOID(pcb);
	Struct_info* si;
	Symbol* msym;
	int doff;

	if (e->ctc.comp[0] != TYPE_STRUCT || e->ctc.comp_count > 2) {
		po_say_fatal(pcb, "using -> on something that isn't a struct");
		PO_CHECK_ABORT_VOID(pcb);
		goto OUT;
	}
	if (e->ctc.comp_count == 1) {
		po_say_fatal(pcb, "-> where there should be a . perhaps?");
		PO_CHECK_ABORT_VOID(pcb);
		goto OUT;
	}
	si = e->ctc.sdims[0].pt;
	lookup_token(pcb);
	if ((msym = po_in_symbol_list(si->elements, pcb->curtoken->ctoke)) == NULL) {
		not_a_member(pcb, si, pcb->curtoken->ctoke);
		goto OUT;
	}
	po_copy_type(pcb, msym->ti, &e->ctc); /* update exp type with member type */
	if ((doff = msym->symval.doff) != 0) {
		po_code_int(pcb, &e->ecd, OP_ICON, doff);
		po_code_op(pcb, &e->ecd, OP_ADD_IOFFSET);
	}
	po_make_deref(pcb, e);
	e->left_complex = true;
OUT:

	return;
}

/*****************************************************************************
 * generate code to access a struct/union member.
 ****************************************************************************/
void po_get_member(Poco_cb* pcb, Exp_frame* e)
{
	PO_CHECK_ABORT_VOID(pcb);
	Struct_info* si;
	Symbol* vsym;
	Symbol* msym;
	int* patch;
	int doff;
	long elsize; /* for patching adcon in left buffer */
	int op;

	vsym = e->var;
	if (e->ctc.comp[0] != TYPE_STRUCT) {
		goto NOTSTRUCT;
	}
	if (e->ctc.comp_count != 1) {
		if (po_is_pointer(&e->ctc)) {
			po_say_fatal(pcb, ". where there should be a -> perhaps?");
			PO_CHECK_ABORT_VOID(pcb);
			goto OUT;
		} else if (po_is_array(&e->ctc)) {
			po_say_fatal(pcb, "need [] before .");
			PO_CHECK_ABORT_VOID(pcb);
			goto OUT;
		} else {
			goto NOTSTRUCT;
		}
	}
	si = e->ctc.sdims[0].pt;
	lookup_token(pcb);
	if ((msym = po_in_symbol_list(si->elements, pcb->curtoken->ctoke)) == NULL) {
		not_a_member(pcb, si, pcb->curtoken->ctoke);
		goto OUT;
	}
	po_copy_type(pcb, msym->ti, &e->ctc); /* update exp type with member type */
	doff = msym->symval.doff;
	e->doff += doff;
	if (!e->left_complex) {
		elsize = po_get_type_size(msym->ti) - 1;
		patch = OPTR(e->left.code_pt, -(sizeof(int) + sizeof(long)));
		*patch += doff; /* add var offset to OP_XXX_ADDRESS */
		patch = OPTR(e->left.code_pt, -(sizeof(long)));
		*(long*)patch = elsize;

		if (po_is_array(msym->ti)) /* move left side (OP_XXX_ADDRESS) */
		{                          /* to right side.				   */
			e->ecd.code_pt = OPTR(e->ecd.code_pt, -(OPY_SIZE + sizeof(int)));
			po_concatenate_code(pcb, &e->ecd, &e->left);
		} else {
			patch = OPTR(e->ecd.code_pt, -(OPY_SIZE + sizeof(int)));
			*patch = find_use_op(pcb, vsym, &e->ctc); /* update OP_XXX_XVAR */
			patch = OPTR(e->ecd.code_pt, -sizeof(int));
			*patch += doff; /* update var offset in OP_XXX_XVAR */
		}
	} else {
		po_backup_code(pcb, &e->ecd, OPY_SIZE); /* get rid of ref_op */
		if (doff) {
			po_code_int(pcb, &e->left, OP_ICON, doff);
			po_code_op(pcb, &e->left, OP_ADD_IOFFSET);
			po_code_int(pcb, &e->ecd, OP_ICON, doff);
			po_code_op(pcb, &e->ecd, OP_ADD_IOFFSET);
		}
		if ((op = ref_op(pcb, &e->ctc)) < 0) {
			goto OUT;
		}
		po_code_op(pcb, &e->ecd, op);
	}
OUT:
	return;
NOTSTRUCT:
	po_say_fatal(pcb, "using . on something that isn't a struct");
	PO_CHECK_ABORT_VOID(pcb);
	return;
}

const int po_scoped_address_op[2] = {OP_GLO_ADDRESS, OP_LOC_ADDRESS};

/*****************************************************************************
 * generate code to use a variable.
 ****************************************************************************/
void po_use_var(Poco_cb* pcb, Exp_frame* e, Symbol* var)
{
	Type_info* ti = var->ti;
	int op;
	long size;

	po_copy_type(pcb, ti, &e->ctc); /* update expression type with self */
	e->doff = var->symval.doff;

	var->flags |= SFL_USED;

	op = po_scoped_address_op[var->storage_scope];
	size = po_get_type_size(ti) - 1;

	switch (ti->comp[ti->comp_count - 1]) {
		case TYPE_ARRAY: {
			/* code left (address) side of expression 1st */
			po_code_address(pcb, &e->left, op, var->symval.doff, size);
			/* code right (value) side. */
			po_code_address(pcb, &e->ecd, op, var->symval.doff, size);
			break;
		}
		case TYPE_FUNCTION: {
			/* code right (value) side. */
			po_code_void_pt(pcb, &e->ecd, OP_CODE_ADDRESS, ti->sdims[ti->comp_count - 1].pt);
			break;
		}
		default: {
			/* code left (address) side of expression 1st */
			po_code_address(pcb, &e->left, op, var->symval.doff, size);
			/* code right (value) side. */
			po_code_int(pcb, &e->ecd, find_use_op(pcb, var, &e->ctc), var->symval.doff);
			break;
		}
	}
}

/*****************************************************************************
 * allocate space for a variable on appropriate poco_frame.
 *	 if variable was declared with the 'static' keyword, storage is
 *	 allocated on the global frame, otherwise storage is allocated on
 *	 the current frame.
 ****************************************************************************/
void po_new_var_space(Poco_cb* pcb, Symbol* var)
{
	Poco_frame* rf = pcb->rframe;
	long size = po_get_type_size(var->ti);

	if (var->ti->flags & TFL_STATIC) {
		while (rf->frame_type != FTY_GLOBAL) {
			rf = rf->next;
		}
		var->storage_scope = SCOPE_GLOBAL;
	} else {
		var->storage_scope = (var->scope == SCOPE_GLOBAL) ? SCOPE_GLOBAL : SCOPE_LOCAL;
	}

	var->symval.doff = (rf->doff -= size);
}

/*****************************************************************************
 * allocate some local temp space on current poco_frame.
 ****************************************************************************/
int po_get_temp_space(Poco_cb* pcb, int space)
{
	return (pcb->rframe->doff -= space);
}
