/*
 * funccall.c - function call expression parsing and argument marshalling.
 */
#ifndef POCO_FUNCCALL_H
#define POCO_FUNCCALL_H

#include "poco_internal.h"

int po_get_param_size(Poco_cb* pcb, SHORT ido_type, bool allow_struct);
void po_get_function(Poco_cb* pcb, Exp_frame* e);

#endif /* POCO_FUNCCALL_H */
