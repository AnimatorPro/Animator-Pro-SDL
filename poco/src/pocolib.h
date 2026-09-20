/*
 * Compatibility-only legacy ABI header - Poco core's entry to it.
 *
 * Deprecated for new embedding hosts and native modules: use
 * <poco/poco.h>, PocoVm/PocoProgram, PocoLibrary, and PocoModuleDescriptor.
 * The layouts themselves live in <poco/poco_legacy.h>, which is the single
 * authoritative definition shared with the Animator compatibility shim.  Do
 * not copy them into host headers.
 *
 * Poco core sets no POCO_LEGACY_HOST_* macro: its linklist.h and stdtypes.h
 * take the shared list and scalar types from <poco/poco_legacy_types.h> too,
 * so there is only ever one definition in a Poco translation unit.
 */
#ifndef POCOLIB_H
#define POCOLIB_H

#ifndef POCO_LINKLIST_H
#include "linklist.h"
#endif

#ifndef POCO_STDTYPES_H
#include "stdtypes.h"
#endif

#ifndef POCO_ERRCODES_H
#include "poco_errcodes.h"
#endif

#include <stddef.h>

#include <poco/poco_legacy.h>

/*----------------------------------------------------------------------------
 * pocolib.c -- registration and teardown of the builtin library tables.
 *--------------------------------------------------------------------------*/

Errcode init_poco_libs(Poco_lib* lib);
void po_cleanup_libs(Poco_lib* lib);
void poco_freez(Popot* pt);

/* Weak fallbacks in pocolib.c; an embedding host (Animator) supplies its own. */
Errcode errline(Errcode err, char* fmt, ...);
size_t get_errtext(Errcode err, char* buf);

#endif /* POCOLIB_H */
