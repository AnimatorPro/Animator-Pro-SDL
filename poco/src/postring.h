/*
 * postring.c - the 1991 String type experiment.
 *
 * The whole module is behind STRING_EXPERIMENT (the POCO_STRING_EXPERIMENT
 * CMake option).  It is preserved deliberately, not shipped: with the option
 * off this header declares nothing.
 */
#ifndef POCO_POSTRING_H
#define POCO_POSTRING_H

#include "poco_internal.h"

#ifdef STRING_EXPERIMENT
struct string_ref;

/* compile time */
void po_add_local_string(Poco_cb* pcb, Poco_frame* pf, Symbol* symbol);
void po_free_local_string_list(Poco_cb* pcb, Poco_frame* pf);
void po_code_free_string_ops(Poco_cb* pcb, Poco_frame* pf);
bool po_is_string(Type_info* ti);
void po_coerce_to_string(Poco_cb* pcb, Exp_frame* e);
void po_coerce_to_string_type(Poco_cb* pcb, Exp_frame* e, TypeComp start_type, int start_count);

/* run time.  The allocating calls report failure through env->builtin_error. */
struct string_ref* po_sr_new(struct PocoActivation* env, int len);
struct string_ref* po_sr_new_copy(struct PocoActivation* env, char* pt, int len);
struct string_ref* po_sr_new_string(struct PocoActivation* env, char* pt);
struct string_ref* po_sr_cat(struct PocoActivation* env, struct string_ref* a,
							 struct string_ref* b);
struct string_ref* po_sr_cat_and_clean(struct PocoActivation* env, struct string_ref* a,
									   struct string_ref* b);
void po_sr_inc_ref(struct string_ref* ref);
void po_sr_dec_ref(struct string_ref* ref);
void po_sr_destroy(struct PocoActivation* env, struct string_ref* ref);
bool po_sr_clean_ref(struct PocoActivation* env, struct string_ref* ref);
bool po_sr_eq(struct string_ref* a, struct string_ref* b);
bool po_sr_ge(struct string_ref* a, struct string_ref* b);
bool po_sr_le(struct string_ref* a, struct string_ref* b);
bool po_sr_eq_and_clean(struct PocoActivation* env, struct string_ref* a, struct string_ref* b);
bool po_sr_ge_and_clean(struct PocoActivation* env, struct string_ref* a, struct string_ref* b);
bool po_sr_le_and_clean(struct PocoActivation* env, struct string_ref* a, struct string_ref* b);
#endif /* STRING_EXPERIMENT */

#endif /* POCO_POSTRING_H */
