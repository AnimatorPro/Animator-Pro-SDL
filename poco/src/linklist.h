/*
 * Simple intrusive doubly-linked list helpers for Poco.
 *
 * Dlnode/Dlheader are part of the legacy Poco_lib compatibility ABI, so their
 * layout is owned by <poco/poco_legacy_types.h> and shared with Animator
 * rather than repeated here.  This header adds only Poco's private helpers.
 *
 * The guard is POCO_LINKLIST_H, not LINKLIST_H: Animator has a linklist.h of
 * its own, and the two files sharing one guard used to be the only thing
 * keeping their definitions apart.
 */
#ifndef POCO_LINKLIST_H
#define POCO_LINKLIST_H

#include <poco/poco_legacy_types.h>

#define RNODE_FIELDS \
	Dlnode node;     \
	void* resource;

static inline void init_list(Dlheader* h)
{
	h->head = (Dlnode*)&h->tail;
	h->tail = 0;
	h->tails_prev = (Dlnode*)&h->head;
}

static inline void add_head(Dlheader* h, Dlnode* n)
{
	Dlnode* first = h->head;

	n->prev = (Dlnode*)&h->head;
	n->next = first;
	first->prev = n;
	h->head = n;
}

static inline void rem_node(Dlnode* n)
{
	n->prev->next = n->next;
	n->next->prev = n->prev;
	n->next = n->prev = 0;
}

static inline void rem_from_list(Dlheader* h, Dlnode* n)
{
	(void)h;
	rem_node(n);
}

#endif /* POCO_LINKLIST_H */
