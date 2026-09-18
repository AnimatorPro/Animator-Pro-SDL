/*
 * bop.c - binary operator expression parsing.
 */
#ifndef POCO_BOP_H
#define POCO_BOP_H

#include "poco_internal.h"

void po_init_qbop_table(Poco_cb* pcb);
void po_get_binop_expression(Poco_cb* pcb, Exp_frame* e);

#endif /* POCO_BOP_H */
