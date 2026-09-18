/*******************************************************************************
 * vm_diagnostics.c - per-VM diagnostic text and thread-local active-VM tracking.
 *
 * A VM's last_error buffer is shared between the threads that may touch it, so
 * every write goes through the VM's diagnostic lock.  The active-VM slot serves
 * the builtin bindings that predate the embedding API and therefore have no VM
 * parameter of their own.
 ******************************************************************************/

#include "vm_diagnostics.h"

#include "activation.h"
#include "poco_lock.h"
#include "pocoface.h"

#include <stdarg.h>
#include <stdio.h>

void po_vm_diagnostic_lock(PocoVm* vm)
{
	po_lock_acquire(vm != NULL ? vm->diagnostic_lock : NULL);
}

void po_vm_diagnostic_unlock(PocoVm* vm)
{
	po_lock_release(vm != NULL ? vm->diagnostic_lock : NULL);
}

void poco_set_error(PocoVm* vm, const char* fmt, ...)
{
	va_list args;

	if (vm == NULL) {
		return;
	}
	po_vm_diagnostic_lock(vm);
	va_start(args, fmt);
	vsnprintf(vm->last_error, sizeof(vm->last_error), fmt, args);
	va_end(args);
	po_vm_diagnostic_unlock(vm);
}

const char* poco_get_last_error(PocoVm* vm)
{
	return vm != NULL ? vm->last_error : "";
}

Errcode* poco_vm_builtin_error(PocoVm* vm)
{
	return vm != NULL && vm->activation != NULL ? &vm->activation->builtin_error : NULL;
}

/*
 * Return buffer for bindings that hand poco code a pointer to error text.
 * Per-VM, so two VMs running on two threads cannot overwrite each other's
 * result between the callee's return and the caller's read.
 */
char* poco_vm_errtext_buffer(PocoVm* vm)
{
	return vm != NULL ? vm->strerror_text : NULL;
}

/*
 * Active-VM tracking.
 *
 * Builtin bindings that predate the embedding API take no PocoVm parameter, so
 * they cannot reach their caller's activation through an argument.  The VM that
 * is currently executing on this thread is recorded here instead, which keeps
 * their status reporting per-activation and lets two VMs run concurrently on
 * different threads.  The slot is thread-local, never process-wide.
 */
static POCO_THREAD_LOCAL PocoVm* poco_thread_active_vm;

/* Written only when no activation is live (a binding called outside a run). */
static POCO_THREAD_LOCAL Errcode poco_thread_detached_builtin_error;

PocoVm* poco_active_vm(void)
{
	return poco_thread_active_vm;
}

PocoVm* poco_push_active_vm(PocoVm* vm)
{
	PocoVm* previous = poco_thread_active_vm;

	poco_thread_active_vm = vm;
	return previous;
}

void poco_pop_active_vm(PocoVm* previous)
{
	poco_thread_active_vm = previous;
}

/*
 * Status slot for the legacy no-vm-parameter bindings.  Resolves to the
 * activation running on this thread; falls back to a thread-local scratch slot
 * so a stray call outside a run cannot corrupt another VM or crash.
 */
Errcode* poco_active_builtin_error(void)
{
	Errcode* slot = poco_vm_builtin_error(poco_thread_active_vm);

	return slot != NULL ? slot : &poco_thread_detached_builtin_error;
}
