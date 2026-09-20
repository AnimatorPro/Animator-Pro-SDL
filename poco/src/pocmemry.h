/*
 * pocmemry.c - compiler arena allocation and small-block caching.
 */
#ifndef POCO_POCMEMRY_H
#define POCO_POCMEMRY_H

#include "poco_internal.h"

Errcode po_init_memory_management(Poco_cb** pcb);
void po_free_compile_memory(Poco_cb* pcb);
void po_free_all_memory(Poco_cb* pcb);
void po_freemem(void* ptr);
void* po_memalloc(Poco_cb* pcb, unsigned long size);
void* po_memzalloc(Poco_cb* pcb, size_t size);
void* po_cache_malloc(Poco_cb* pcb, Cache_ctl* pctl);
char* po_clone_string(Poco_cb* pcb, char* s);

#endif /* POCO_POCMEMRY_H */
