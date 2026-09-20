/*
 * statemen.c - statement parsing: blocks, loops, switch, goto.
 */
#ifndef POCO_STATEMEN_H
#define POCO_STATEMEN_H

#include "poco_internal.h"

bool po_eat_semi(Poco_cb* pcb);
bool po_eat_rbrace(Poco_cb* pcb);
bool po_eat_lbrace(Poco_cb* pcb);
void po_get_statements(Poco_cb* pcb, Poco_frame* d);
void po_get_block(Poco_cb* pcb, Poco_frame* d);
int po_need_comma_or_brace(Poco_cb* pcb);
void po_check_array_dim(Poco_cb* pcb, Symbol* var);
Code_label* po_label_to_symbol(Poco_cb* pcb, Poco_frame* pf, Symbol* lsym);
void po_exp_statement(Poco_cb* pcb, Poco_frame* pf);

#endif /* POCO_STATEMEN_H */
