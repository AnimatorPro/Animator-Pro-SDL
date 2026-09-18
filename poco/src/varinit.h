/*
 * varinit.c - initializer parsing for declared variables.
 */
#ifndef POCO_VARINIT_H
#define POCO_VARINIT_H

#include "poco_internal.h"

void po_var_init(Poco_cb* pcb, Exp_frame* e, Symbol* var, SHORT frame_type);

#endif /* POCO_VARINIT_H */
