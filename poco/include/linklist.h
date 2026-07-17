/*
 * Simple intrusive doubly-linked list helpers for Poco.
 *
 * Dlheader is part of the legacy Poco_lib compatibility ABI.  Keep its
 * three-pointer sentinel layout in sync with Animator while legacy callers
 * still exchange Poco_lib values across the boundary.
 */
#ifndef LINKLIST_H
#define LINKLIST_H

typedef struct Dlnode {
    struct Dlnode *next;
    struct Dlnode *prev;
} Dlnode;

typedef struct Dlheader {
    Dlnode *head;
    Dlnode *tail;
    Dlnode *tails_prev;
} Dlheader;

#define RNODE_FIELDS Dlnode node; void *resource;

static inline void init_list(Dlheader *h)
{
    h->head = (Dlnode *)&h->tail;
    h->tail = 0;
    h->tails_prev = (Dlnode *)&h->head;
}

static inline void add_head(Dlheader *h, Dlnode *n)
{
    Dlnode *first = h->head;

    n->prev = (Dlnode *)&h->head;
    n->next = first;
    first->prev = n;
    h->head = n;
}

static inline void rem_node(Dlnode *n)
{
    n->prev->next = n->next;
    n->next->prev = n->prev;
    n->next = n->prev = 0;
}

static inline void rem_from_list(Dlheader *h, Dlnode *n)
{
    (void)h;
    rem_node(n);
}

#endif /* LINKLIST_H */
