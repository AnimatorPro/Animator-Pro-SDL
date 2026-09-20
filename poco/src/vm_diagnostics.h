/*******************************************************************************
 * vm_diagnostics.h - serialized access to a VM's diagnostic text.
 *
 * poco_set_error(), poco_get_last_error() and the active-VM accessors are
 * declared in pocoface.h, which host translation units already include.  Only
 * the lock helpers are poco-private, so only they are declared here.
 ******************************************************************************/

#ifndef POCO_VM_DIAGNOSTICS_H
#define POCO_VM_DIAGNOSTICS_H

#include <poco/poco.h>

/* Both tolerate a NULL vm and a vm whose lock could not be allocated. */
void po_vm_diagnostic_lock(PocoVm* vm);
void po_vm_diagnostic_unlock(PocoVm* vm);

#endif /* POCO_VM_DIAGNOSTICS_H */
