/*******************************************************************************
 * potokface.h - Token list entry points shared inside the compiler.
 * Only the functions potokface.c exports to other compiler sources live
 * here; po_lookup_freshtoken and the po_eat_* helpers stay in
 * poco_internal.h.
 ******************************************************************************/

#ifndef POCO_POTOKFACE_H
#define POCO_POTOKFACE_H

#include "poco_internal.h"

Tstack* po_build_token_list(Poco_cb* pcb);
void po_free_token_lists(Poco_cb* pcb);
SHORT po_lookahead_type(Poco_cb* pcb);

#endif /* POCO_POTOKFACE_H */
