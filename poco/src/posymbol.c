/*******************************************************************************
 * posymbol.c - Compiler symbol table.
 * Hashing, allocation, lookup and teardown of Symbol records, and the
 * generic singly linked list free used by symbol and literal lists.
 * Split out of poco.c.
 ******************************************************************************/

#include "poco_internal.h"
#include "pocmemry.h"
#include "posymbol.h"
#include <stdlib.h>
#include <string.h>

/******  MODULE MEMORY Some memory allocation routines **********/
/*** memory management has been moved to module pocmemry.c ***/

/*****************************************************************************
 * Free any simple singly linked list with next field at offset 0
 ****************************************************************************/
void po_freelist(void* l)
{
	Names* cur = *((Names**)l);
	Names* next;

	while (cur != NULL) {
		next = cur->next;
		po_freemem(cur);
		cur = next;
	}
	*((Names**)l) = NULL;
}

/****** MODULE SYMBOL Symbol table management ***********/
/*****************************************************************************
 * typical hashing function.
 ****************************************************************************/
int po_hashfunc(UBYTE* s)
{
	int acc;
	int c;

	acc = *s++;
	while ((c = *s++) != 0) {
		acc = (acc /* <<1 */) + c;
	}
	return (acc & (HASH_SIZE - 1));
}

/*****************************************************************************
 * unlink an element from a singly-linked list.
 ****************************************************************************/
Symbol* po_unlink_el(Symbol* list, Symbol* el)
{
	Symbol* next;

	if (list == el) {
		return (el->next);
	}
	next = list;
	while (next->next != el) {
		next = next->next;
	}
	next->next = el->next;
	return (list);
}

/*****************************************************************************
 * remove a symbol from the current poco_frame's hash table.
 ****************************************************************************/
void po_unhash_symbol(Poco_cb* pcb, Symbol* s)
{
	Symbol** hash_slot;

	hash_slot = pcb->rframe->hash_table + po_hashfunc((UBYTE*)s->name);
	*hash_slot = po_unlink_el(*hash_slot, s);
}

/*****************************************************************************
 * alloc and init Symbol, put name after Symbol in memory, hash sym into table.
 ****************************************************************************/
Symbol* po_new_symbol_tok(Poco_cb* pcb, char* s, SHORT tok_type)
{
	Symbol* new;
	Symbol** hash_slot;

	hash_slot = pcb->rframe->hash_table + po_hashfunc((UBYTE*)s);
	new = po_memzalloc(pcb, sizeof(*new) + strlen(s) + 1);
	new->name = (char*)(new + 1);
	strcpy(new->name, s);
	new->unit_name = pcb->current_unit_name;
	new->unit_index = pcb->current_unit_index;
	new->tok_type = tok_type;
	new->next = *hash_slot;
	*hash_slot = new;
	return (new);
}

/*****************************************************************************
 * po_rehash a set of symbols into a hash table.
 * returns zero on success, or if a parm without a name was found, returns
 * the number (first parm is 1, etc) of the bad parm.
 *
 * this is used to copy a set of function parameters from the fuf to
 * the poco_frame, so that the params look like local variables when
 * processing the body of the function.  note that the logic in this routine
 * follows the 'link' pointers, but connects the 'next' pointers.
 ****************************************************************************/
int po_rehash(Poco_cb* pcb, Symbol* s)
{
	int counter = 0;
	Symbol** hash_slot;

	while (s != NULL) {
		++counter;
		if (s->name[0] == '\0') {
			return counter;
		}
		hash_slot = pcb->rframe->hash_table + po_hashfunc((UBYTE*)s->name);
		s->next = *hash_slot;
		*hash_slot = s;
		s = s->link;
	}
	return 0;
}

/*****************************************************************************
 * free a symbol, and its associated type_info, if there is one.
 ****************************************************************************/
void po_free_symbol(Symbol* s)
{
	poc_gentle_freemem(s->ti);
	po_freemem(s);
}

/*****************************************************************************
 * free a list of symbols by following the 'link' (not 'next') pointers.
 ****************************************************************************/
void po_free_symbol_list(Symbol** ps)
{
	Symbol *sym, *link;

	sym = *ps;
	while (sym != NULL) {
		link = sym->link;
		po_free_symbol(sym);
		sym = link;
	}
	*ps = NULL;
}

/*****************************************************************************
 * alloc a new Symbol, and attach it to the current poco_frame.
 ****************************************************************************/
Symbol* po_new_symbol(Poco_cb* pcb, char* name)
{
	Symbol* s;
	Poco_frame* rf = pcb->rframe;

	s = po_new_symbol_tok(pcb, name, PTOK_UNDEF);

	if (s == NULL) {
		return NULL;
	}

	s->link = rf->symbols;
	s->scope = rf->scope;
	rf->symbols = s;

	return s;
}

/*****************************************************************************
 * find symbol via linear search of a linked list (using 'next' pointers).
 ****************************************************************************/
Symbol* po_in_symbol_list(register Symbol* l, char* name)
{
	register char c1 = *name;

	while (l != NULL) {
		if (l->name[0] == c1) { /* quick-check first chars before making call */
			if (po_eqstrcmp(l->name, name) == 0) {
				return (l);
			}
		}
		l = l->next;
	}
	return (NULL);
}

/*****************************************************************************
 * find a symbol by search poco_frames from most current scope down to global.
 ****************************************************************************/
Symbol* po_find_symbol(Poco_cb* pcb, char* name)
{
	struct poco_frame* p;
	Symbol* s;
	int hashval;

	p = pcb->rframe;
	hashval = po_hashfunc((UBYTE*)name);
	while (p != NULL) {
		if ((s = po_in_symbol_list(p->hash_table[hashval], name)) != NULL) {
			return (s);
		}
		p = p->next;
	}
	return (NULL);
}

/*****************************************************************************
 * return the length of (number of items in) a linked list.
 ****************************************************************************/
int po_link_len(Symbol* l)
{
	int count = 0;

	while (l != NULL) {
		count += 1;
		l = l->link;
	}
	return count;
}

/*****************************************************************************
 * reverse the order of the elements in a singly-linked list.
 ****************************************************************************/
void* po_reverse_links(Symbol* el)
{
	Symbol *link, *list;

	list = NULL;
	while (el != NULL) {
		link = el->link;
		el->link = list;
		list = el;
		el = link;
	}
	return (list);
}
