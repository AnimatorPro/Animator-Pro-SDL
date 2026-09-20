/*******************************************************************************
 * posymbol.h - Symbol table entry points shared inside the compiler.
 * Only the functions posymbol.c exports to other compiler sources live
 * here; the long-standing po_* symbol API remains in poco_internal.h.
 ******************************************************************************/

#ifndef POCO_POSYMBOL_H
#define POCO_POSYMBOL_H

#include "poco_internal.h"

Symbol* po_new_symbol_tok(Poco_cb* pcb, char* s, SHORT tok_type);
Symbol* po_in_symbol_list(Symbol* l, char* name);
Symbol* po_find_symbol(Poco_cb* pcb, char* name);

#endif /* POCO_POSYMBOL_H */
