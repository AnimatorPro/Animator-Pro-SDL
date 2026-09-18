/*
 * safefile.c - the resource-tracking file and memory standard libraries.
 */
#ifndef POCO_SAFEFILE_H
#define POCO_SAFEFILE_H

#include "poco_internal.h"
#include "pocolib.h"

/* Legacy Poco_lib descriptors, still named directly by the CLI's builtin
 * library table and by the resource lookups in this module. */
extern Poco_lib po_FILE_lib;
extern Poco_lib po_mem_lib;

void po_free(void* pt);

/* Resource-list lookup.  Not private to this module: Animator's blit library
 * (src/ani_poco/pocoblit.c, via src/inc/pocolib.h) walks its own resource list
 * with it, so it stays externally linkable. */
Rnode* po_in_rlist(Dlheader* sfi, void* f);

#endif /* POCO_SAFEFILE_H */
