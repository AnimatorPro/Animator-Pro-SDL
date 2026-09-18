/*
 * ani_builtin_err.h - VM-scoped status slot for the Animator bindings.
 *
 * The Animator builtins were written against a process-global `builtin_err`,
 * which made their status reporting shared between every VM in the process and
 * left it disconnected from the activation the interpreter actually reads.
 * `builtin_err` is now an lvalue macro over the running activation's slot, the
 * same place poco's own bindings report through (poco_vm_builtin_error).  Two
 * VMs on two threads therefore report independently.
 *
 * Include this header LAST.  It redefines the identifier `builtin_err`, so any
 * header that still declares the old global must be seen before it.
 */

#ifndef ANI_BUILTIN_ERR_H
#define ANI_BUILTIN_ERR_H

#include "errcodes.h"

/* Defined in libpoco (pocoface.c).  Never returns NULL: with no activation
 * running on this thread it resolves to a thread-local scratch slot. */
Errcode* poco_active_builtin_error(void);

#undef builtin_err
#define builtin_err (*poco_active_builtin_error())

#endif /* ANI_BUILTIN_ERR_H */
