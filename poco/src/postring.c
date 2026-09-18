/****************************************************************************
 *
 * postring.c - This file supports the String type in Poco programs.
 * Strings in Poco are (String_ref *) here.  Each string has a reference
 * count associated with it.  Most string expressions just involve
 * bumping the reference count up and down though some, like string
 * concatenation, will create new String_ref's.  When the reference
 * count goes to zero the String_ref is freed.
 *
 * The String type is an experiment from 1991 that never shipped.  It is
 * retained deliberately, behind the POCO_STRING_EXPERIMENT CMake option,
 * and everything belonging to it -- compile-time support, run-time
 * reference counting, and the type coercion rules -- lives in this file so
 * the experiment does not shape the layout of poco.c and runops.c.
 *****************************************************************************/
#include "poco.h"
#include "linklist.h"

#ifdef STRING_EXPERIMENT

#include "pocolib.h"
#include "activation.h"

/*----------------------------------------------------------------------------
 * Compile-time support.
 *--------------------------------------------------------------------------*/

void po_add_local_string(Poco_cb* pcb, Poco_frame* pf, Symbol* symbol)
/*****************************************************************************
 * Allocate and initialize a string-list if symbol is a simple string type.
 ****************************************************************************/
{
	Local_string* new_string;
	Type_info* ti;

	ti = symbol->ti;
	if (ti->comp[0] == TYPE_STRING && ti->comp_count == 1) {
		new_string = po_memalloc(pcb, sizeof(*new_string));
		if (new_string == NULL) {
			return;
		}
		new_string->string_symbol = symbol;
		new_string->next = pf->local_string_list;
		pf->local_string_list = new_string;
	}
}

void po_free_local_string_list(Poco_cb* pcb, Poco_frame* pf)
{
	Local_string *list, *next;

	(void)pcb;
	next = pf->local_string_list;
	while ((list = next) != NULL) {
		next = list->next;
		po_freemem(list);
	}
	pf->local_string_list = NULL;
}

void po_code_free_string_ops(Poco_cb* pcb, Poco_frame* pf)
/*****************************************************************************
 * Generate an OP_FREE_STRING for each local_string in Poco_frame.
 ****************************************************************************/
{
	Local_string* list;

	for (list = pf->local_string_list; list != NULL; list = list->next) {
		po_code_int(pcb, &pf->fcd, OP_FREE_STRING, list->string_symbol->symval.doff);
	}
}

static void cant_convert_to_String(Poco_cb* pcb)
/*****************************************************************************
 * issue error message that string must be a char *, char [], or
 * another string.
 ****************************************************************************/
{
	po_say_fatal(pcb, "expression can't be converted to String type");
	PO_CHECK_ABORT_VOID(pcb);
}

void po_coerce_to_string(Poco_cb* pcb, Exp_frame* e)
/*****************************************************************************
 * make a cast to promote an expression to a String type.
 ****************************************************************************/
{
	static TypeComp st = TYPE_STRING;
	static Type_info string_type_info = {&st, NULL, 1, 1, 0, IDO_STRING};
	po_coerce_expression(pcb, e, &string_type_info, true);
}

void po_coerce_to_string_type(Poco_cb* pcb, Exp_frame* e, TypeComp start_type, int start_count)
/*****************************************************************************
 * Coerce an expression to the String type.  The only things that convert to
 * a String are another String, a char *, or a char [].  Called by
 * po_coerce_expression() when the destination type is TYPE_STRING; it lives
 * here so the String rules stay out of poco.c's coercion switch.
 ****************************************************************************/
{
	if (start_count == 1) {
		if (start_type != TYPE_STRING) {
			cant_convert_to_String(pcb);
		}
		/* else will end up returning happily - both are strings! */
		return;
	}
	if (start_count != 2 || e->ctc.comp[0] != TYPE_CHAR) {
		cant_convert_to_String(pcb);
		return;
	}
	switch (start_type) {
		case TYPE_POINTER:
		case TYPE_ARRAY:
			po_code_op(pcb, &e->ecd, OP_PPT_TO_STRING);
			break;
		case TYPE_CPT:
			po_code_op(pcb, &e->ecd, OP_CPT_TO_STRING);
			break;
		default:
			cant_convert_to_String(pcb);
			return;
	}
	e->ctc.comp[0] = TYPE_STRING;
	e->ctc.comp_count = 1;
	e->ctc.ido_type = IDO_STRING;
}

/*----------------------------------------------------------------------------
 * Run-time support.
 *
 * The allocating entry points take the PocoActivation that asked for the
 * string: allocation is charged to that activation's VM heap and failures are
 * reported through its builtin_error, not through a process-wide error
 * global.
 *--------------------------------------------------------------------------*/

String_ref* po_sr_new(struct PocoActivation* env, int len)
/*****************************************************************************
 * Allocate a new reference for a buffer
 ****************************************************************************/
{
	PocoVm* vm = (env != NULL) ? env->vm : NULL;
	Popot popot_ref;
	String_ref* ref;

	popot_ref = poco_lmalloc_for_vm(vm, sizeof(String_ref));
	if ((ref = popot_ref.pt) == NULL) {
		if (env != NULL) {
			env->builtin_error = Err_no_memory;
		}
		return (NULL);
	}
	ref->string = poco_lmalloc_for_vm(vm, len);
	if (ref->string.pt == NULL) {
		po_free_for_vm(vm, ref);
		if (env != NULL) {
			env->builtin_error = Err_no_memory;
		}
		return (NULL);
	}
	ref->ref_count = 1;
	return (ref);
}

String_ref* po_sr_new_copy(struct PocoActivation* env, char* pt, int len)
/*****************************************************************************
 * Allocate a new reference for a buffer and initialize it from pt.
 ****************************************************************************/
{
	String_ref* ret;

	if ((ret = po_sr_new(env, len)) != NULL) {
		poco_copy_bytes(pt, ret->string.pt, len);
	}
	return (ret);
}

String_ref* po_sr_new_string(struct PocoActivation* env, char* pt)
/*****************************************************************************
 * Allocate a new reference for a null terminated character array
 ****************************************************************************/
{
	return (po_sr_new_copy(env, pt, (int)strlen(pt) + 1));
}

String_ref* po_sr_cat(struct PocoActivation* env, String_ref* a, String_ref* b)
/*****************************************************************************
 * Allocate a new string reference for the concatenation of a & b
 ****************************************************************************/
{
	String_ref* ret;
	size_t lena, lenb;
	char* ret_buf;

	/* Concatenation of two NULL strings is NULL */
	if (a == NULL && b == NULL) {
		return (NULL);
	}
	/* Concatenation of a NULL and a non-null is the non-null */
	if (a == NULL) {
		po_sr_inc_ref(b);
		return (b);
	}
	if (b == NULL) {
		po_sr_inc_ref(a);
		return (a);
	}
	/* Concatenation of two non-null strings takes a little work... */
	lena = strlen(a->string.pt);
	lenb = strlen(b->string.pt);
	if ((ret = po_sr_new(env, (int)(lena + lenb + 1))) != NULL) {
		ret_buf = ret->string.pt;
		poco_copy_bytes(a->string.pt, ret_buf, (int)lena);
		ret_buf += lena;
		poco_copy_bytes(b->string.pt, ret_buf, (int)lenb);
		ret_buf[lenb] = 0; /* add zero tag at end */
	}
	return (ret);
}

void po_sr_inc_ref(String_ref* ref)
{
	if (ref != NULL) {
		++ref->ref_count;
	}
}

void po_sr_dec_ref(String_ref* ref)
{
	if (ref != NULL) {
		--ref->ref_count;
	}
}

void po_sr_destroy(struct PocoActivation* env, String_ref* ref)
/*****************************************************************************
 * Destroy string ref and the string data too.
 ****************************************************************************/
{
	PocoVm* vm = (env != NULL) ? env->vm : NULL;

	po_free_for_vm(vm, ref->string.pt);
	po_free_for_vm(vm, ref);
}

bool po_sr_clean_ref(struct PocoActivation* env, String_ref* ref)
/* Decrement the reference count and if down to zero destroy string.
 * Return true if string destroyed. */
{
	if (ref != NULL) {
		po_sr_dec_ref(ref);
		if (ref->ref_count <= 0) {
			po_sr_destroy(env, ref);
			return (true);
		}
	}
	return (false);
}

bool po_sr_eq(String_ref* a, String_ref* b)
/* Are two strings the same */
{
	if (a == b) {
		return (true);
	} else if (a == NULL || b == NULL) {
		return (false);
	} else {
		return (strcmp(a->string.pt, b->string.pt) == 0);
	}
}

bool po_sr_ge(String_ref* a, String_ref* b)
/* return a >= b */
{
	if (a == b) {
		return (false);
	} else if (a == NULL) { /* treat NULL as very small */
		return (false);
	} else if (b == NULL) {
		return (true);
	} else {
		return (strcmp(a->string.pt, b->string.pt) >= 0);
	}
}

bool po_sr_le(String_ref* a, String_ref* b)
/* return a <= b */
{
	if (a == b) {
		return (false);
	} else if (a == NULL) { /* treat NULL as very small */
		return (true);
	} else if (b == NULL) {
		return (false);
	} else {
		return (strcmp(a->string.pt, b->string.pt) <= 0);
	}
}

bool po_sr_eq_and_clean(struct PocoActivation* env, String_ref* a, String_ref* b)
{
	bool ret = po_sr_eq(a, b);
	po_sr_clean_ref(env, a);
	po_sr_clean_ref(env, b);
	return (ret);
}

bool po_sr_ge_and_clean(struct PocoActivation* env, String_ref* a, String_ref* b)
{
	bool ret = po_sr_ge(a, b);
	po_sr_clean_ref(env, a);
	po_sr_clean_ref(env, b);
	return (ret);
}

bool po_sr_le_and_clean(struct PocoActivation* env, String_ref* a, String_ref* b)
{
	bool ret = po_sr_le(a, b);
	po_sr_clean_ref(env, a);
	po_sr_clean_ref(env, b);
	return (ret);
}

String_ref* po_sr_cat_and_clean(struct PocoActivation* env, String_ref* a, String_ref* b)
{
	String_ref* ret = po_sr_cat(env, a, b);
	po_sr_clean_ref(env, a);
	po_sr_clean_ref(env, b);
	return (ret);
}

#endif /* STRING_EXPERIMENT */
