/*
 * declare.c - declaration and prototype parsing.
 */
#ifndef POCO_DECLARE_H
#define POCO_DECLARE_H

#include "poco_internal.h"

Func_frame* po_sym_to_fuf(Symbol* s);
Func_frame* po_get_proto(Type_info* ti);
void po_pop_off_result(Poco_cb* pcb, Exp_frame* e);
void po_get_typedef(Poco_cb* pcb, Poco_frame* pf);
void po_get_typename(Poco_cb* pcb, Type_info* ti);
void po_get_declaration(Poco_cb* pcb, Poco_frame* pf);

#endif /* POCO_DECLARE_H */
