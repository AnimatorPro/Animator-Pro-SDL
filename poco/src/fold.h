/*
 * fold.c - constant folding of compiled expressions.
 */
#ifndef POCO_FOLD_H
#define POCO_FOLD_H

#include "poco_internal.h"

void po_fold_const(Poco_cb* pcb, Exp_frame* exp);
int po_eval_const_expression(Poco_cb* pcb, Exp_frame* exp);
bool po_is_static_init_const(Poco_cb* pcb, Code_buf* cb);

#endif /* POCO_FOLD_H */
