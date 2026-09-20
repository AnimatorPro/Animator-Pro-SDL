/*******************************************************************************
 * poco_lock.h - the one threading primitive poco uses.
 *
 * A recursive-free mutex, created on the heap and referred to by an opaque
 * pointer so no header in the tree has to name <pthread.h> or <windows.h>.
 * Every entry point tolerates a NULL lock, which is what a VM that could not
 * allocate one holds.
 ******************************************************************************/

#ifndef POCO_LOCK_H
#define POCO_LOCK_H

void* po_lock_create(void);
void po_lock_destroy(void* lock);
void po_lock_acquire(void* lock);
void po_lock_release(void* lock);

#endif /* POCO_LOCK_H */
