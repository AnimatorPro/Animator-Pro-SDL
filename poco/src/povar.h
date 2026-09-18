/*******************************************************************************
 * povar.h - Variable access entry points shared inside the compiler.
 * Only the functions povar.c exports to other compiler sources live here;
 * po_find_assign_op, po_make_deref and friends stay in poco_internal.h.
 ******************************************************************************/

#ifndef POCO_POVAR_H
#define POCO_POVAR_H

#include "poco_internal.h"

bool po_check_ido_table(Poco_cb* pcb);
SHORT po_ind_op(Poco_cb* pcb, Type_info* ti);
void po_get_array(Poco_cb* pcb, Exp_frame* e);
void po_get_member(Poco_cb* pcb, Exp_frame* e);
void po_get_pmember(Poco_cb* pcb, Exp_frame* e);
void po_use_var(Poco_cb* pcb, Exp_frame* e, Symbol* var);

#endif /* POCO_POVAR_H */
