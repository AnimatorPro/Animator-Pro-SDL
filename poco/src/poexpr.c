/*******************************************************************************
 * poexpr.c - Expression parser.
 * The recursive descent expression evaluator: primaries, unary and binary
 * operators, casts, sizeof, assignment forms and the numeric/boolean type
 * coercions they need.  Split out of poco.c.
 ******************************************************************************/

#include "poco_internal.h"
#include "bop.h"
#include "code.h"
#include "declare.h"
#include "fold.h"
#include "funccall.h"
#include "pocmemry.h"
#include "pocotype.h"
#include "postring.h"
#include "potokface.h"
#include "povar.h"
#include <stdlib.h>
#include <string.h>

/* internal predeclarations */

static void get_prec1(Poco_cb* pcb, Exp_frame* e);
static void assign_after_value(Poco_cb* pcb, Exp_frame* e, Symbol* var);

/* table to convert from expression type to variable type */
static const SHORT inv_ido[] = {
	TYPE_INT,
	TYPE_LONG,
	TYPE_DOUBLE,
	TYPE_POINTER,
};

/******* MODULE EXPRESSION ************/
/* Here we've got the really recursive part of the recursive descent
   parser - the expression evaluatior */

/*****************************************************************************
 * init a pre-allocated expression frame.
 ****************************************************************************/
void po_init_expframe(Poco_cb* pcb, Exp_frame* e)
{
	poco_zero_bytes(e, sizeof(*e));
	e->pure_const = true;
	po_init_code_buf(pcb, &e->ecd);  /* The right value of expression */
	po_init_code_buf(pcb, &e->left); /* The left value of expression */
	e->ctc.comp = e->ctc_comp;       /* e->ctc - the expression type */
	e->ctc.sdims = e->ctc_dims;
	e->ctc.comp_alloc = MAX_TYPE_COMPS;
}

/*****************************************************************************
 * allocate and init a new expression frame.
 ****************************************************************************/
Exp_frame* po_new_expframe(Poco_cb* pcb)
{
	Exp_frame* new;

	new = po_cache_malloc(pcb, &pcb->expf_cache);
	po_init_expframe(pcb, new);
	return new;
}

/*****************************************************************************
 * free the code buffers from an expression frame.
 ****************************************************************************/
void po_trash_expframe(Poco_cb* pcb, Exp_frame* e)
{
	po_trash_code_buf(pcb, &e->ecd);
	po_trash_code_buf(pcb, &e->left);
}

/*****************************************************************************
 * free an expression frame allocated earlier.
 ****************************************************************************/
void po_dispose_expframe(Poco_cb* pcb, Exp_frame* e)
{
	po_trash_expframe(pcb, e);
	po_freemem(e);
}

/*****************************************************************************
 * ensure an expression is floating point or int, if not, complain and die.
 ****************************************************************************/
SHORT po_force_num_exp(Poco_cb* pcb, Type_info* ti)
{
	SHORT nt;

	nt = ti->ido_type;
	if (!po_ido_table[nt].is_num) {
		po_expecting_got(pcb, "numeric expression");
		return (-1);
	}
	return (nt);
}

/*****************************************************************************
 * ensure an expression is int, if not, complain and die.
 ****************************************************************************/
SHORT po_force_int_exp(Poco_cb* pcb, Type_info* ti)
{
	SHORT nt;

	nt = ti->ido_type;
	if (!po_is_int_ido(nt)) {
		po_expecting_got(pcb, "integer expression");
		return (-1);
	}
	return (nt);
}

/*****************************************************************************
 * ensure an expression is either numeric or pointer, if not, complain and die.
 ****************************************************************************/
SHORT po_force_ptr_or_num_exp(Poco_cb* pcb, Type_info* ti)
{
	SHORT nt;

	nt = ti->ido_type;
	if (!po_ido_table[nt].can_add) {
		po_expecting_got(pcb, "numeric expression or data pointer");
	}
	return (nt);
}

/*****************************************************************************
 * make a cast to upgrade an expression to a given type.
 ****************************************************************************/
static void upgrade_numerical_expression(Poco_cb* pcb, Exp_frame* e, SHORT ido_type)
{
	SHORT eido_type;

	eido_type = e->ctc.ido_type;
	if (ido_type != eido_type) {
		switch (ido_type) {
			case IDO_CPT:
			case IDO_VPT:
			case IDO_POINTER: {
				switch (eido_type) {
					case IDO_POINTER:
						break;
					default:
						po_say_fatal(pcb, "cannot do pointer<->number conversion");
						PO_CHECK_ABORT_VOID(pcb);
						break;
				}
			} break;
			case IDO_INT:
				switch (eido_type) {
					case IDO_LONG:
						po_code_op(pcb, &e->ecd, OP_LONG_TO_INT);
						goto upgrade_type;
					case IDO_DOUBLE:
						po_code_op(pcb, &e->ecd, OP_DOUBLE_TO_INT);
						goto upgrade_type;
				}
				break;
			case IDO_LONG:
				switch (eido_type) {
					case IDO_INT:
						po_code_op(pcb, &e->ecd, OP_INT_TO_LONG);
						goto upgrade_type;
					case IDO_DOUBLE:
						po_code_op(pcb, &e->ecd, OP_DOUBLE_TO_LONG);
						goto upgrade_type;
				}
				break;
			case IDO_DOUBLE:
				switch (eido_type) {
					case IDO_INT:
						po_code_op(pcb, &e->ecd, OP_INT_TO_DOUBLE);
						goto upgrade_type;
					case IDO_LONG:
						po_code_op(pcb, &e->ecd, OP_LONG_TO_DOUBLE);
						goto upgrade_type;
				}
				break;
		}
	}
	return;
upgrade_type:
	e->ctc.comp[0] = inv_ido[ido_type];
	e->ctc.ido_type = ido_type;
	po_fold_const(pcb, e);
}

/*****************************************************************************
 * make a cast to reduce an expression to a boolean (int) type.
 ****************************************************************************/
void po_coerce_to_boolean(Poco_cb* pcb, Exp_frame* e)
{
	if (e->ctc.ido_type != IDO_INT) {
		switch (e->ctc.ido_type) {
			case IDO_LONG:
			case IDO_CPT:
			case IDO_VPT:
				po_code_op(pcb, &e->ecd, OP_LONG_TO_INT);
				break;
			case IDO_DOUBLE:
				po_code_op(pcb, &e->ecd, OP_DOUBLE_TO_INT);
				break;
			case IDO_POINTER:
				po_code_op(pcb, &e->ecd, OP_PPT_TO_CPT);
				po_code_op(pcb, &e->ecd, OP_LONG_TO_INT);
				break;
			default:
				po_say_fatal(pcb, "expecting boolean expression");
				PO_CHECK_ABORT_VOID(pcb);
				return;
		}
		e->ctc.comp[0] = TYPE_INT;
		e->ctc.comp_count = 1;
		e->ctc.ido_type = IDO_INT;
	}
}

/*****************************************************************************
 * make cast of expression to given type, if bad starting type, complain & die.
 ****************************************************************************/
void po_coerce_numeric_exp(Poco_cb* pcb, Exp_frame* e, SHORT ido_type)
{
	if (!po_ido_table[e->ctc.ido_type].is_num) {
		po_expecting_got(pcb, "numeric expression");
		return;
	}
	upgrade_numerical_expression(pcb, e, ido_type);
}

/*****************************************************************************
 * make a cast of an expression to a given type.
 ****************************************************************************/
void po_coerce_expression(Poco_cb* pcb, Exp_frame* e, Type_info* ti, bool recast)
{
	TypeComp *pstart_type, start_type;
	TypeComp end_type, *pend_type;
	int start_count, end_count;

	end_count = ti->comp_count;
	pend_type = ti->comp + end_count - 1;
	end_type = *pend_type;
	start_count = e->ctc.comp_count;
	pstart_type = e->ctc.comp + start_count - 1;
	start_type = *pstart_type;
	if (end_type == TYPE_STRUCT) {
		if (recast || start_count != 1 || start_type != TYPE_STRUCT || ti->sdims == NULL ||
			e->ctc.sdims == NULL || ti->sdims[0].pt != e->ctc.sdims[0].pt ||
			!po_types_same(ti, &e->ctc, 0)) {
			po_say_fatal(pcb, "structure type mismatch in assignment");
			PO_CHECK_ABORT_VOID(pcb);
		}
		return;
	}

	if (end_count == 1) {
#ifdef STRING_EXPERIMENT
		if (end_type == TYPE_STRING) {
			po_coerce_to_string_type(pcb, e, start_type, start_count);
		} else
#endif /* STRING_EXPERIMENT */
		/* Then (hopefully) it's a numerical type of some sort */
		{
			if (recast && start_count != 1) {
				po_say_fatal(pcb, "cannot recast pointer expression to numeric type");
			}
			PO_CHECK_ABORT_VOID(pcb);
			po_coerce_numeric_exp(pcb, e, ti->ido_type);
		}
	} else {
		switch (end_type) {
			case TYPE_ARRAY:
			case TYPE_POINTER:
				switch (start_type) {
					case TYPE_POINTER:
						break;
					case TYPE_CPT:
						po_code_op(pcb, &e->ecd, OP_CPT_TO_PPT);
						start_type = TYPE_POINTER;
						e->ctc.ido_type = IDO_POINTER;
						break;
					case TYPE_ARRAY:
						start_type = TYPE_POINTER;
						e->ctc.ido_type = IDO_POINTER;
						break;
					case TYPE_FUNCTION:
						start_type = TYPE_POINTER;
						pstart_type += 1;
						e->ctc.comp_count += 1;
						e->ctc.ido_type = IDO_POINTER;
						break;
#ifdef STRING_EXPERIMENT
					case TYPE_STRING:
						po_code_op(pcb, &e->ecd, OP_STRING_TO_PPT);
						*pstart_type++ = TYPE_CHAR; /* Convert type to (char *) */
						e->ctc.comp_count += 1;
						start_type = TYPE_POINTER;
						e->ctc.ido_type = IDO_POINTER;
						break;
#endif /* STRING_EXPERIMENT */
					default:
						goto WANT_POINTER;
				}
				break;
			case TYPE_CPT:
				switch (start_type) {
					case TYPE_CPT:
						break;
					case TYPE_POINTER:
					case TYPE_ARRAY:
						po_code_op(pcb, &e->ecd, OP_PPT_TO_CPT);
						start_type = TYPE_CPT; /* update expression type */
						e->ctc.ido_type = IDO_CPT;
						break;
					case TYPE_FUNCTION:
						po_code_op(pcb, &e->ecd, OP_PPT_TO_CPT);
						start_type = TYPE_CPT;
						pstart_type += 1;
						e->ctc.comp_count += 1;
						e->ctc.ido_type = IDO_CPT;
						break;
#ifdef STRING_EXPERIMENT
					case TYPE_STRING:
						/* Here were're almost certainly a parameter to a
						 * C function. */
						po_code_op(pcb, &e->ecd, OP_STRING_TO_CPT);
						*pstart_type++ = TYPE_CHAR; /* Convert type to (char *) */
						e->ctc.comp_count += 1;
						start_type = TYPE_CPT;
						e->ctc.ido_type = IDO_CPT;
						break;
#endif /* STRING_EXPERIMENT */
					default:
						goto WANT_POINTER;
				}
				break;
			default:
				goto WANT_POINTER;
		}
		*pstart_type = start_type;
		if (recast) {
			po_copy_type(pcb, ti, &e->ctc);
		} else if (!(ti->comp[0] == TYPE_VOID || e->ctc.comp[0] == TYPE_VOID)) {
			if (!po_types_same(ti, &e->ctc, 0)) {
#ifdef POCO_DEBUG_DUMP
				fprintf(pcb->t.err_file, "s is ");
				po_print_type(pcb, pcb->t.err_file, &e->ctc);
				fprintf(pcb->t.err_file, "\nd is ");
				po_print_type(pcb, pcb->t.err_file, ti);
				fprintf(pcb->t.err_file, "\n");
#endif
				po_say_fatal(pcb, "type mismatch in pointer evaluation");
				PO_CHECK_ABORT_VOID(pcb);
			}
		}
	}
	return;
WANT_POINTER: {
	if (recast) {
		po_say_fatal(pcb, "cannot recast numeric expression to pointer type");
		PO_CHECK_ABORT_VOID(pcb);
	} else {
		po_expecting_got(pcb, "pointer expression");
	}
}
}

/*****************************************************************************
 * get primitive - a number, variable, or expression in parenthesis
 ****************************************************************************/
void po_get_prim(Poco_cb* pcb, Exp_frame* e)
{
	PO_CHECK_ABORT_VOID(pcb);
	switch (pcb->t.toktype) {
		case TOK_LPAREN: {
			po_get_comma_expression(pcb, e);
			lookup_token(pcb);
			if (pcb->t.toktype != TOK_RPAREN) {
				po_say_fatal(pcb, "missing right parenthesis");
				PO_CHECK_ABORT_VOID(pcb);
			}
			break;
		}
		case TOK_INT: {
			po_code_int(pcb, &e->ecd, OP_ICON, pcb->curtoken->val.num);
			po_set_base_type(pcb, &e->ctc, TYPE_INT, 0, NULL);
			clear_code_buf(pcb, &e->left);
			break;
		}
		case TOK_LONG: {
			po_code_long(pcb, &e->ecd, OP_LCON, pcb->curtoken->val.num);
			po_set_base_type(pcb, &e->ctc, TYPE_LONG, 0, NULL);
			clear_code_buf(pcb, &e->left);
			break;
		}
		case TOK_DOUBLE: {
			po_code_double(pcb, &e->ecd, OP_DCON, pcb->curtoken->val.dnum);
			po_set_base_type(pcb, &e->ctc, TYPE_DOUBLE, 0, NULL);
			clear_code_buf(pcb, &e->left);
			break;
		}
		case PTOK_QUO: {
			po_code_popot(pcb, &e->ecd, OP_PCON, pcb->curtoken->val.string,
						  pcb->curtoken->val.string + strlen(pcb->curtoken->val.string),
						  pcb->curtoken->val.string);
			po_set_base_type(pcb, &e->ctc, TYPE_CHAR, 0, NULL);
			po_append_type(pcb, &e->ctc, TYPE_POINTER, 0, NULL);
			clear_code_buf(pcb, &e->left);
			break;
		}
		case PTOK_NULL: {
			po_code_popot(pcb, &e->ecd, OP_PCON, (void*)1, NULL, NULL);
			po_set_base_type(pcb, &e->ctc, TYPE_VOID, 0, NULL);
			po_append_type(pcb, &e->ctc, TYPE_POINTER, 0, NULL);
			clear_code_buf(pcb, &e->left);
			break;
		}
		case PTOK_VAR: {
			po_use_var(pcb, e, e->var = pcb->curtoken->val.symbol);
			e->pure_const = false;
			break;
		}
		case PTOK_UNDEF: {
			po_undefined(pcb, pcb->curtoken->val.symbol->name);
			break;
		}
		default: {
			po_expecting_got(pcb, "a number, variable, or string");
			break;
		}
	}
}

/*****************************************************************************
 * do precedence-1 right end operators. ('->'  '['  ']'  '.')
 ****************************************************************************/
static void get_prec1(Poco_cb* pcb, Exp_frame* e)
{
	SHORT ttype;

	PO_CHECK_ABORT_VOID(pcb);
	po_get_prim(pcb, e);
	for (;;) {
		PO_CHECK_ABORT_VOID(pcb);
		lookup_token(pcb);
		ttype = pcb->t.toktype;
		switch (ttype) {
			case '[':
				po_get_array(pcb, e);
				break;
			case '(':
				po_get_function(pcb, e);
				break;
			case '.':
				po_get_member(pcb, e);
				break;
			case TOK_ARROW:
				po_get_pmember(pcb, e);
				break;
			default:
				pushback_token(&pcb->t);
				return;
		}
		po_fold_const(pcb, e);
	}
}

/*****************************************************************************
 * code a sizeof(something) value.
 ****************************************************************************/
static void use_sizeof(Poco_cb* pcb, Exp_frame* e)
{
	Exp_frame vexp;
	long size = 0;

	po_init_expframe(pcb, &vexp);

	if (!po_is_next_token(pcb, TOK_LPAREN)) {
		po_get_unop_expression(pcb, &vexp); /* no parens, we have sizeof unary_expression */
	} else {
		switch (po_lookahead_type(pcb)) /* lookahead to token after paren */
		{
			case PTOK_TYPE:
			case PTOK_USER_TYPE:

				po_get_typename(pcb, &vexp.ctc);
				break;

			default:

				po_get_unop_expression(pcb, &vexp);
				break;
		}
	}

	size = po_get_type_size(&vexp.ctc);

	po_code_long(pcb, &e->ecd, OP_LCON, size);
	po_set_base_type(pcb, &e->ctc, TYPE_LONG, 0, NULL);

	clear_code_buf(pcb, &e->left);
	po_trash_expframe(pcb, &vexp);
}

/*****************************************************************************
 * decide whether a sizeof() is being handled.
 ****************************************************************************/
static void get_sizeof(Poco_cb* pcb, Exp_frame* e)
{
	if (pcb->t.toktype == PTOK_SIZEOF) {
		use_sizeof(pcb, e);
	} else {
		get_prec1(pcb, e);
	}
}

/*****************************************************************************
 * handle a recast.
 ****************************************************************************/
static void get_cast(Poco_cb* pcb, Exp_frame* e)
{
	Itypi tip;
	Type_info* ti;

	pushback_token(&pcb->t);
	ti = po_typi_type(&tip);
	po_get_typename(pcb, ti);
	po_get_unop_expression(pcb, e);
	po_coerce_expression(pcb, e, ti, true);
	return;
}

/*****************************************************************************
 * code a constant value of 1 in a given type.
 ****************************************************************************/
static void code_one(Poco_cb* pcb, Code_buf* cb, Type_info* ti)
{
	switch (ti->ido_type) {
		case IDO_INT:
			po_code_int(pcb, cb, OP_ICON, 1);
			break;
		case IDO_LONG:
			po_code_long(pcb, cb, OP_LCON, 1L);
			break;
		case IDO_DOUBLE:
			po_code_double(pcb, cb, OP_DCON, (double)1.0);
			break;
		case IDO_POINTER:
			po_code_int(pcb, cb, OP_ICON, (int)po_get_subtype_size(pcb, ti));
			break;
	}
}

/*****************************************************************************
 * code a post-increment or post-decrement, depending of value of op_group.
 ****************************************************************************/
static void get_post_increment(Poco_cb* pcb, Exp_frame* e, const Op_type op_group[NUM_IDOS])
{
	Symbol* v;
	SHORT ido_type;

	if (any_code(pcb, &e->left)) {
		v = e->var;
		if ((ido_type = po_force_ptr_or_num_exp(pcb, &e->ctc)) >= 0) {
			po_code_op(pcb, &e->ecd, po_dupe_ops[ido_type]);
			code_one(pcb, &e->ecd, &e->ctc);
			po_code_op(pcb, &e->ecd, op_group[ido_type]);
			assign_after_value(pcb, e, v);
			po_code_op(pcb, &e->ecd, po_find_clean_op(pcb, &e->ctc));
			/* clear_code_buf(pcb, &e->left); */
		}
	} else {
		po_say_fatal(pcb, "trying to increment a non-variable");
		PO_CHECK_ABORT_VOID(pcb);
	}
}

/*****************************************************************************
 * code a pre-increment or pre-decrement, depending on value of op_group.
 ****************************************************************************/
static void get_pre_increment(Poco_cb* pcb, Exp_frame* e, const Op_type op_group[NUM_IDOS])
{
	Symbol* v;
	SHORT ido_type;

	po_get_unop_expression(pcb, e);
	if (any_code(pcb, &e->left)) {
		v = e->var;
		if ((ido_type = po_force_ptr_or_num_exp(pcb, &e->ctc)) >= 0) {
			code_one(pcb, &e->ecd, &e->ctc);
			po_code_op(pcb, &e->ecd, op_group[ido_type]);
			assign_after_value(pcb, e, v);
		}
	} else {
		po_say_fatal(pcb, "trying to increment a non-variable");
		PO_CHECK_ABORT_VOID(pcb);
	}
}

/*****************************************************************************
 * check for a dereference, call the coder routine if so.
 ****************************************************************************/
static void get_dereference(Poco_cb* pcb, Exp_frame* e)
{
	/* the operand's value is the address, so it need not be an lvalue:
	 * *&x, *"abc" and *(0, p) are all fine. */
	if (po_is_pointer(&e->ctc) || po_is_array(&e->ctc)) {
		e->ctc.comp_count -= 1;
		po_set_ido_type(&e->ctc);
		po_make_deref(pcb, e);
		return;
	}

	po_say_fatal(pcb, " '*' on non-pointer expression");
	PO_CHECK_ABORT_VOID(pcb);
}

/*****************************************************************************
 * code an address-of op.
 ****************************************************************************/
static void get_address(Poco_cb* pcb, Exp_frame* e)
{
	Exp_frame ex;

	po_init_expframe(pcb, &ex);
	po_get_unop_expression(pcb, &ex);
	if (!any_code(pcb, &ex.left)) {
		po_say_fatal(pcb, "trying to take address of non-variable");
		PO_CHECK_ABORT_VOID(pcb);
		goto OUT;
	}
	po_copy_type(pcb, &ex.ctc, &e->ctc);
	po_append_type(pcb, &e->ctc, TYPE_POINTER, 0, NULL);
	po_concatenate_code(pcb, &e->ecd, &ex.left);
	e->pure_const &= ex.pure_const;
	clear_code_buf(pcb, &e->left);
OUT:
	po_trash_expframe(pcb, &ex);
}

/*****************************************************************************
 * get signed primitive - a primitive with optional preceeding unary op.
 ****************************************************************************/
void po_get_unop_expression(Poco_cb* pcb, Exp_frame* e)
{
	PO_CHECK_ABORT_VOID(pcb);
	TypeComp t1;
	const Op_type* op_group;
	int fok = false; /* float ok? */
	int temp;
	register SHORT ttype;
	SHORT ntype;

	if (!po_need_token(pcb)) {
		return;
	}

	ttype = pcb->t.toktype;
	ntype = po_lookahead_type(pcb);

	if (ttype == '-') {
		po_get_unop_expression(pcb, e);
		op_group = po_neg_ops;
		fok = true;
	} else if (ttype == '!') {
		po_get_unop_expression(pcb, e);
		op_group = po_not_ops;
	} else if (ttype == '~') {
		po_get_unop_expression(pcb, e);
		op_group = po_comp_ops;
	} else if (ttype == '+') {
		po_get_unop_expression(pcb, e);
		op_group = NULL;
	} else if (ttype == '*') {
		po_get_unop_expression(pcb, e);
		get_dereference(pcb, e);
		return;
	} else if (ttype == '&') {
		get_address(pcb, e);
		return;
	} else if (ttype == TOK_PLUS_PLUS) {
		get_pre_increment(pcb, e, po_add_ops);
		return;
	} else if (ttype == TOK_MINUS_MINUS) {
		get_pre_increment(pcb, e, po_sub_ops);
		return;
	} else if (ttype == TOK_LPAREN && (ntype == PTOK_TYPE || ntype == PTOK_USER_TYPE)) {
		get_cast(pcb, e);
		return;
	} else {
		get_sizeof(pcb, e);
		if (po_need_token(pcb)) {
			if (pcb->t.toktype == TOK_PLUS_PLUS) {
				get_post_increment(pcb, e, po_add_ops);
			} else if (pcb->t.toktype == TOK_MINUS_MINUS) {
				get_post_increment(pcb, e, po_sub_ops);
			} else {
				pushback_token(&pcb->t);
			}
		}
		return;
	}

	t1 = temp = po_force_num_exp(pcb, &e->ctc);
	if (temp < 0) {
		return;
	}
	if (op_group != NULL) {
		if ((t1 == IDO_DOUBLE) && !fok) {
			po_say_fatal(pcb, "~ and ! operators can't be used with floats or doubles");
			PO_CHECK_ABORT_VOID(pcb);
			return;
		}
		po_code_op(pcb, &e->ecd, op_group[t1]);
		po_fold_const(pcb, e);
	}
	clear_code_buf(pcb, &e->left);
}

/*****************************************************************************
 * code an assignment of an expression value.
 ****************************************************************************/
static void assign_after_value(Poco_cb* pcb, Exp_frame* e, Symbol* var)
{
	if (e->left_complex) {
		po_concatenate_code(pcb, &e->ecd, &e->left);
		po_code_op(pcb, &e->ecd, po_ind_op(pcb, &e->ctc));
	} else {
		po_code_int(pcb, &e->ecd, po_find_assign_op(pcb, var, &e->ctc), e->doff);
	}
	e->includes_assignment++;
}

/*****************************************************************************
 * drive process of coding an assignment.
 *****************************************************************************/
static void make_assign(Poco_cb* pcb, Exp_frame* e, Exp_frame* val_exp, Symbol* var)
{
	TypeComp obase = e->ctc.comp[0];
	long struct_size;

	po_coerce_expression(pcb, val_exp, &e->ctc, false);
	if (e->ctc.ido_type == IDO_STRUCT) {
		struct_size = po_get_type_size(&e->ctc);
		if (struct_size <= 0 || !any_code(pcb, &e->left)) {
			po_say_fatal(pcb, "invalid structure assignment target");
			PO_CHECK_ABORT_VOID(pcb);
			return;
		}
		/* Ordinary struct lvalues and native struct-return temps both travel as
		 * bounded addresses.  Retain one source address so the assignment still
		 * has the usual expression value after OP_COPY consumes its operands. */
		if (any_code(pcb, &val_exp->left)) {
			clear_code_buf(pcb, &val_exp->ecd);
			po_copy_code(pcb, &val_exp->left, &val_exp->ecd);
		}
		po_concatenate_code(pcb, &e->ecd, &val_exp->ecd);
		po_code_op(pcb, &e->ecd, OP_PDUPE);
		po_concatenate_code(pcb, &e->ecd, &e->left);
		po_code_long(pcb, &e->ecd, OP_COPY, struct_size);
		clear_code_buf(pcb, &e->left);
		e->includes_assignment++;
		e->includes_assignment += val_exp->includes_assignment;
		e->includes_function += val_exp->includes_function;
		e->pure_const &= val_exp->pure_const;
		return;
	}
	po_concatenate_code(pcb, &e->ecd, &val_exp->ecd);
	e->ctc.comp[0] = obase;
	assign_after_value(pcb, e, var);
	clear_code_buf(pcb, &e->left);
	e->includes_assignment += val_exp->includes_assignment;
	e->includes_function += val_exp->includes_function;
	e->pure_const &= val_exp->pure_const;
}

/*****************************************************************************
 * drive process of getting an expression after '=' and coding the assign.
 *
 *	if this is called from the variable init parser, and the variable's
 *	storage class is static, then the expression must be a constant.  we
 *	have to check for const-ness before doing the make_assign(), since it
 *	adds code to the buffer which makes the expression look non-constant.
 *	if the pure_const flag in the expression is true, we're in fine shape,
 *	but if it's false, it could be because of taking the address of a static
 *	data item.	taking such an address is constant in terms of init
 *	expressions, but not in terms of constant-folding, so we have a special
 *	routine (in fold.c) for checking a !pure_const expression to see if it
 *	qualifies as constant for an init expression.
 ****************************************************************************/
bool po_assign_after_equals(Poco_cb* pcb, Exp_frame* e, Symbol* var, bool must_be_init_constant)
{
	Exp_frame val_eee;

	po_init_expframe(pcb, &val_eee);
	po_get_expression(pcb, &val_eee);

	if (must_be_init_constant) {
		if (!val_eee.pure_const) {
			if (!po_is_static_init_const(pcb, &val_eee.ecd)) {
				po_say_fatal(pcb, "constant expression required for static initializer");
				PO_CHECK_ABORT(pcb, false);
			}
		}
	}

	make_assign(pcb, e, &val_eee, var);
	po_trash_expframe(pcb, &val_eee);
	return true;
}

/*****************************************************************************
 * code a '+=' type op (eg, *= <<=, etc).
 ****************************************************************************/
static void plus_equals(Poco_cb* pcb, Exp_frame* e, Symbol* var, const Op_type op_group[NUM_IDOS],
						SHORT (*enforcer)(Poco_cb* pcb, Type_info* ti))
{
	Exp_frame val_eee;
	int is_pt = po_is_pointer(&e->ctc);

	po_init_expframe(pcb, &val_eee);
	po_get_expression(pcb, &val_eee);
	if ((*enforcer)(pcb, &e->ctc) < 0) {
		goto TRASH;
	}
	if (is_pt) {
		po_coerce_numeric_exp(pcb, &val_eee, IDO_INT);
		po_code_elsize(pcb, &val_eee, po_get_subtype_size(pcb, &e->ctc));
		if (val_eee.ctc.ido_type != IDO_INT) {
			po_coerce_numeric_exp(pcb, &val_eee, IDO_INT);
			po_fold_const(pcb, e);
		}
		po_code_op(pcb, &val_eee.ecd, op_group[e->ctc.ido_type]);
		po_concatenate_code(pcb, &e->ecd, &val_eee.ecd);
		assign_after_value(pcb, e, var);
	} else {
		po_coerce_numeric_exp(pcb, &val_eee, e->ctc.ido_type);
		po_code_op(pcb, &val_eee.ecd, op_group[e->ctc.ido_type]);
		make_assign(pcb, e, &val_eee, var);
	}
TRASH:
	po_trash_expframe(pcb, &val_eee);
}

/*****************************************************************************
 * drive the process of expression parsing and code generation.
 ****************************************************************************/
void po_get_expression(Poco_cb* pcb, Exp_frame* e)
{
	PO_CHECK_ABORT_VOID(pcb);
	Symbol* var;

	po_get_binop_expression(pcb, e);
	if (any_code(pcb, &e->left)) {
		var = e->var;
		lookup_token(pcb);
		switch (pcb->t.toktype) {
			case TOK_PLUS_EQUALS:
				plus_equals(pcb, e, var, po_add_ops, po_force_ptr_or_num_exp);
				break;
			case TOK_MINUS_EQUALS:
				plus_equals(pcb, e, var, po_sub_ops, po_force_ptr_or_num_exp);
				break;
			case TOK_DIV_EQUALS:
				plus_equals(pcb, e, var, po_div_ops, po_force_num_exp);
				break;
			case TOK_MUL_EQUALS:
				plus_equals(pcb, e, var, po_mul_ops, po_force_num_exp);
				break;
			case TOK_MOD_EQUALS:
				plus_equals(pcb, e, var, po_mod_ops, po_force_int_exp);
				break;
			case TOK_LSHIFT_EQUALS:
				plus_equals(pcb, e, var, po_lshift_ops, po_force_int_exp);
				break;
			case TOK_RSHIFT_EQUALS:
				plus_equals(pcb, e, var, po_rshift_ops, po_force_int_exp);
				break;
			case TOK_AND_EQUALS:
				plus_equals(pcb, e, var, po_band_ops, po_force_int_exp);
				break;
			case TOK_OR_EQUALS:
				plus_equals(pcb, e, var, po_bor_ops, po_force_int_exp);
				break;
			case TOK_XOR_EQUALS:
				plus_equals(pcb, e, var, po_xor_ops, po_force_int_exp);
				break;
			case '=':
				/* oops, didn't mean to code that... */
				clear_code_buf(pcb, &e->ecd);
				po_assign_after_equals(pcb, e, var, false);
				break;
			default:
				pushback_token(&pcb->t);
				break;
		}
	}
}

/*****************************************************************************
 * parse the comma operator: evaluate each operand left to right, discarding
 * all but the last, whose type the whole expression takes.  the result is
 * neither an lvalue nor a constant expression.
 ****************************************************************************/
void po_get_comma_expression(Poco_cb* pcb, Exp_frame* e)
{
	Exp_frame ef;

	po_get_expression(pcb, e);
	for (;;) {
		PO_CHECK_ABORT_VOID(pcb);
		lookup_token(pcb);
		if (pcb->t.toktype != ',') {
			pushback_token(&pcb->t);
			break;
		}
		po_pop_off_result(pcb, e);
		po_init_expframe(pcb, &ef);
		po_get_expression(pcb, &ef);
		po_concatenate_code(pcb, &e->ecd, &ef.ecd);
		po_copy_type(pcb, &ef.ctc, &e->ctc);
		clear_code_buf(pcb, &e->left);
		e->var = NULL;
		e->left_complex = false;
		e->pure_const = false;
		e->includes_assignment += ef.includes_assignment;
		e->includes_function += ef.includes_function;
		po_trash_expframe(pcb, &ef);
	}
}
