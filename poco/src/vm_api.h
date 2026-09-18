/*******************************************************************************
 * vm_api.h - helpers the public embedding API shares with the modules split out
 * of it.
 *
 * The API entry points themselves are declared in <poco/poco.h>.  These are the
 * internals that use_graph.c and poco_call.c reach back into.
 ******************************************************************************/

#ifndef POCO_VM_API_H
#define POCO_VM_API_H

#include <poco/poco.h>

#include "poco_internal.h"

/* malloc'd copy of value, or NULL for a NULL value or a failed allocation. */
char* po_copy_string(const char* value);

/* malloc'd directory prefix of source_name, including its trailing separator.
 * A bare basename yields the empty prefix the legacy preprocessor expects for
 * "the process working directory". */
char* po_source_directory(const char* source_name);

/* Hand one diagnostic to the VM's callback, if it has one. */
void po_vm_report(PocoVm* vm, PocoStatus status, const char* source_name, long line, int column,
				  const char* message);

/* Compile an already-ordered set of in-memory units into one program. */
PocoStatus po_vm_compile_sources(PocoVm* vm, const char* const* source_names,
								 const char* const* physical_source_paths,
								 const char* const* sources, const size_t* source_lengths,
								 Names* const* include_dirs, const size_t* const* use_indices,
								 const size_t* use_counts, size_t source_count,
								 PocoProgram** out_program);

#endif /* POCO_VM_API_H */
