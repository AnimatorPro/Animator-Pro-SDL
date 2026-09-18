/*******************************************************************************
 * vm_api.c - the public embedding API: VM lifecycle, library registration,
 * the compile entry points, program ownership and host-to-script calls.
 *
 * The original compiler interface consumes Poco_lib and Names control blocks.
 * Those remain implementation details here: a host registers public descriptors
 * and each compiled program receives an owned, immutable legacy-compatible
 * snapshot, so registrations stay deterministic while neither the host nor an
 * already compiled program can observe internal control blocks.
 ******************************************************************************/

#include "vm_api.h"

#include "activation.h"
#include "filepath.h"
#include "poco_errcodes.h"
#include "poco_ffi.h"
#include "poco_hash.h"
#include "poco_internal.h"
#include "poco_lock.h"
#include "pocmemry.h"
#include "pocoface.h"
#include "pocoload.h"
#include "pocotype.h"
#include "program_internal.h"
#include "runops.h"
#include "standard_library.h"
#include "use_graph.h"
#include "vm_diagnostics.h"

#include <ctype.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const int po_version_number = VRSN_NUM; /* Compile-time constant; read by hosts. */

typedef enum Poco_registered_library_kind {
	POCO_REGISTERED_PUBLIC_LIBRARY,
	POCO_REGISTERED_LEGACY_LIBRARY
} Poco_registered_library_kind;

typedef struct Poco_registered_library {
	struct Poco_registered_library* next;
	Poco_registered_library_kind kind;
	PocoLibrary library;
	PocoBinding* bindings;
	PocoBindingContract* contracts;
	PocoBindingPointerContract** pointer_contracts;
	Poco_lib* legacy_library;
	PocoLibraryRuntimeCleanup runtime_cleanup;
} Poco_registered_library;

const PocoModuleHooks* poco_vm_module_hooks(PocoVm* vm)
{
	return vm != NULL ? &vm->module_hooks : NULL;
}

Poco_lib* poco_active_library(PocoVm* vm, const char* identity)
{
	PocoActivation* activation = vm != NULL ? vm->activation : NULL;
	Poco_lib* library;

	if (activation == NULL || identity == NULL) {
		return NULL;
	}
	for (library = activation->builtin_libraries; library != NULL; library = library->next) {
		if (library->name != NULL && strcmp(library->name, identity) == 0) {
			return library;
		}
	}
	return NULL;
}

PocoPointerRegistry* poco_active_pointer_registry(PocoVm* vm)
{
	return vm != NULL && vm->activation != NULL ? vm->activation->pointer_registry : NULL;
}

char* po_copy_string(const char* value)
{
	size_t length;
	char* copy;

	if (value == NULL) {
		return NULL;
	}
	length = strlen(value) + 1;
	copy = malloc(length);
	if (copy != NULL) {
		memcpy(copy, value, length);
	}
	return copy;
}

char* po_source_directory(const char* source_name)
{
	const char* separator;
	const char* alternate_separator;
	size_t length;
	char* directory;

	separator = strrchr(source_name, '/');
	alternate_separator = strrchr(source_name, '\\');
	if (alternate_separator != NULL && (separator == NULL || alternate_separator > separator)) {
		separator = alternate_separator;
	}
	/* A basename lives in the process working directory, represented by the
	 * legacy preprocessor's empty path prefix. */
	length = separator != NULL ? (size_t)(separator - source_name) + 1 : 0;
	directory = malloc(length + 1);
	if (directory == NULL) {
		return NULL;
	}
	memcpy(directory, source_name, length);
	directory[length] = '\0';
	return directory;
}

static void poco_api_free_names(Names* names)
{
	Names* next;

	while (names != NULL) {
		next = names->next;
		free(names->name);
		free(names);
		names = next;
	}
}

static void poco_api_free_registered_library(Poco_registered_library* registered)
{
	size_t index;

	if (registered == NULL) {
		return;
	}
	if (registered->kind == POCO_REGISTERED_PUBLIC_LIBRARY) {
		if (registered->bindings != NULL) {
			for (index = 0; index < registered->library.binding_count; ++index) {
				free((char*)registered->bindings[index].prototype);
				free(registered->pointer_contracts != NULL ? registered->pointer_contracts[index]
														   : NULL);
			}
		}
		free(registered->pointer_contracts);
		free(registered->contracts);
		free(registered->bindings);
		free((char*)registered->library.identity);
	}
	free(registered);
}

static void poco_api_free_registered_libraries(Poco_registered_library* registered)
{
	Poco_registered_library* next;

	while (registered != NULL) {
		next = registered->next;
		poco_api_free_registered_library(registered);
		registered = next;
	}
}

static void poco_api_free_vm(PocoVm* vm)
{
	if (vm == NULL) {
		return;
	}
	poco_api_free_names(vm->include_dirs);
	poco_api_free_names(vm->library_dirs);
	poco_api_free_registered_libraries(vm->libraries);
	poco_pointer_registry_destroy(vm->pointer_registry);
	po_lock_destroy(vm->diagnostic_lock);
	free(vm);
}

void po_vm_report(PocoVm* vm, PocoStatus status, const char* source_name, long line, int column,
				  const char* message)
{
	PocoDiagnostic diagnostic;

	if (vm == NULL || vm->diagnostic_callback == NULL) {
		return;
	}
	diagnostic.status = status;
	diagnostic.source_name = source_name;
	diagnostic.line = line;
	diagnostic.column = column;
	diagnostic.message = message;
	vm->diagnostic_callback(vm->diagnostic_user_data, &diagnostic);
}

static Errcode poco_api_library_initialize(Poco_lib* library)
{
	Poco_program_library* program_library = library != NULL ? library->local_data : NULL;
	PocoLibrary* public_library = program_library != NULL ? program_library->public_library : NULL;
	PocoStatus status;

	if (program_library == NULL) {
		return Err_poco_internal;
	}
	if (public_library == NULL || public_library->initialize == NULL) {
		status = POCO_STATUS_OK;
	} else {
		status = public_library->initialize(public_library);
	}
	if (status == POCO_STATUS_OK) {
		program_library->initialized = 1;
	}
	return (Errcode)status;
}

static void poco_api_library_cleanup(Poco_lib* library)
{
	Poco_program_library* program_library = library != NULL ? library->local_data : NULL;
	PocoLibrary* public_library = program_library != NULL ? program_library->public_library : NULL;

	if (program_library != NULL && program_library->initialized &&
		program_library->runtime_cleanup != NULL) {
		program_library->runtime_cleanup(library);
	}
	if (program_library != NULL && program_library->initialized && public_library != NULL &&
		public_library->cleanup != NULL) {
		public_library->cleanup(public_library);
	}
	if (program_library != NULL) {
		program_library->initialized = 0;
	}
}

static PocoStatus poco_api_append_registered_library(PocoVm* vm,
													 Poco_registered_library* registered)
{
	if (vm == NULL || registered == NULL) {
		return POCO_STATUS_NULL_REFERENCE;
	}
	if (vm->libraries_tail != NULL) {
		vm->libraries_tail->next = registered;
	} else {
		vm->libraries = registered;
	}
	vm->libraries_tail = registered;
	return POCO_STATUS_OK;
}

static PocoStatus poco_api_register_legacy_library(PocoVm* vm, Poco_lib* library)
{
	Poco_registered_library* registered;

	if (library == NULL || library->name == NULL) {
		return POCO_STATUS_INTERNAL_ERROR;
	}
	registered = calloc(1, sizeof(*registered));
	if (registered == NULL) {
		return POCO_STATUS_OUT_OF_MEMORY;
	}
	registered->kind = POCO_REGISTERED_LEGACY_LIBRARY;
	registered->legacy_library = library;
	return poco_api_append_registered_library(vm, registered);
}

static void poco_api_free_program_libraries(Poco_program_library* library)
{
	Poco_program_library* next;

	while (library != NULL) {
		next = library->next;
		free(library->prototypes);
		free(library);
		library = next;
	}
}

static PocoStatus poco_api_make_program_libraries(PocoVm* vm, Poco_program_library** out_libraries,
												  Poco_lib** out_first_library)
{
	Poco_registered_library* registered;
	Poco_program_library* first = NULL;
	Poco_program_library* tail = NULL;
	Poco_program_library* program_library;
	size_t index;

	if (out_libraries == NULL || out_first_library == NULL) {
		return POCO_STATUS_NULL_REFERENCE;
	}
	*out_libraries = NULL;
	*out_first_library = NULL;

	for (registered = vm->libraries; registered != NULL; registered = registered->next) {
		program_library = calloc(1, sizeof(*program_library));
		if (program_library == NULL) {
			goto OUT_OF_MEMORY;
		}

		if (registered->kind == POCO_REGISTERED_LEGACY_LIBRARY) {
			program_library->library = *registered->legacy_library;
			program_library->library.next = NULL;
			program_library->library.vm = vm;
		} else {
			if (registered->library.binding_count != 0) {
				program_library->prototypes =
					calloc(registered->library.binding_count, sizeof(*program_library->prototypes));
				if (program_library->prototypes == NULL) {
					free(program_library);
					goto OUT_OF_MEMORY;
				}
				for (index = 0; index < registered->library.binding_count; ++index) {
					program_library->prototypes[index].proto =
						(char*)registered->library.bindings[index].prototype;
					program_library->prototypes[index].func =
						(void*)registered->library.bindings[index].function;
					program_library->prototypes[index].contract =
						registered->library.bindings[index].contract;
					program_library->prototypes[index].flags =
						registered->library.bindings[index].flags;
				}
			}
			program_library->library.next = NULL;
			program_library->library.name = (char*)registered->library.identity;
			program_library->library.lib = program_library->prototypes;
			program_library->library.count = (int)registered->library.binding_count;
			program_library->library.init = poco_api_library_initialize;
			program_library->library.cleanup = poco_api_library_cleanup;
			program_library->public_library = &registered->library;
			program_library->runtime_cleanup = registered->runtime_cleanup;
			program_library->library.local_data = program_library;
			program_library->library.vm = vm;
		}

		if (tail != NULL) {
			tail->next = program_library;
			tail->library.next = &program_library->library;
		} else {
			first = program_library;
		}
		tail = program_library;
	}

	*out_libraries = first;
	*out_first_library = first != NULL ? &first->library : NULL;
	return POCO_STATUS_OK;

OUT_OF_MEMORY:
	poco_api_free_program_libraries(first);
	return POCO_STATUS_OUT_OF_MEMORY;
}

static int poco_api_prototype_names_function(const char* prototype, const char* name)
{
	const char* open;
	const char* end;
	const char* start;
	size_t name_length;

	if (prototype == NULL || name == NULL) {
		return 0;
	}
	open = strchr(prototype, '(');
	if (open == NULL) {
		return 0;
	}
	end = open;
	while (end > prototype && isspace((unsigned char)end[-1])) {
		--end;
	}
	start = end;
	while (start > prototype && (isalnum((unsigned char)start[-1]) || start[-1] == '_')) {
		--start;
	}
	name_length = strlen(name);
	return (size_t)(end - start) == name_length && memcmp(start, name, name_length) == 0;
}

int po_vm_resolve_serialized_binding(PocoVm* vm, const Poco_lib* loaded_libraries, const char* name,
									 void** out_function, const PocoBindingContract** out_contract,
									 uint32_t* out_flags)
{
	Poco_registered_library* registered;
	const Poco_lib* loaded;

	if (vm == NULL || name == NULL || out_function == NULL || out_contract == NULL ||
		out_flags == NULL) {
		return 0;
	}
	for (loaded = loaded_libraries; loaded != NULL; loaded = loaded->next) {
		int index;

		for (index = 0; index < loaded->count; ++index) {
			const Lib_proto* binding = &loaded->lib[index];

			if (!poco_api_prototype_names_function(binding->proto, name)) {
				continue;
			}
			*out_function = binding->func;
			*out_contract = binding->contract;
			*out_flags = binding->flags;
			return 1;
		}
	}
	for (registered = vm->libraries; registered != NULL; registered = registered->next) {
		if (registered->kind == POCO_REGISTERED_PUBLIC_LIBRARY) {
			size_t index;

			for (index = 0; index < registered->library.binding_count; ++index) {
				const PocoBinding* binding = &registered->library.bindings[index];

				if (!poco_api_prototype_names_function(binding->prototype, name)) {
					continue;
				}
				*out_function = (void*)binding->function;
				*out_contract = binding->contract;
				*out_flags = binding->flags;
				return 1;
			}
		} else if (registered->legacy_library != NULL) {
			int index;

			for (index = 0; index < registered->legacy_library->count; ++index) {
				Lib_proto* binding = &registered->legacy_library->lib[index];

				if (!poco_api_prototype_names_function(binding->proto, name)) {
					continue;
				}
				*out_function = binding->func;
				*out_contract = binding->contract;
				*out_flags = binding->flags;
				return 1;
			}
		}
	}
	return 0;
}

PocoStatus po_program_adopt_decoded(PocoVm* vm, Poco_run_env* executable, PocoProgram** out_program)
{
	PocoProgram* program;
	Poco_lib* libraries;
	PocoStatus status;

	if (vm == NULL || executable == NULL || out_program == NULL) {
		return POCO_STATUS_NULL_REFERENCE;
	}
	*out_program = NULL;
	if (vm->destroy_requested) {
		return POCO_STATUS_PARAMETER_RANGE;
	}
	program = calloc(1, sizeof(*program));
	if (program == NULL) {
		return POCO_STATUS_OUT_OF_MEMORY;
	}
	status = poco_api_make_program_libraries(vm, &program->libraries, &libraries);
	if (status != POCO_STATUS_OK) {
		free(program);
		return status;
	}
	executable->vm = vm;
	executable->lib = libraries;
	if (po_ffi_build_structures(executable) != Success) {
		poco_api_free_program_libraries(program->libraries);
		free(program);
		return POCO_STATUS_INTERNAL_ERROR;
	}
	program->vm = vm;
	program->executable = executable;
	program->code.stack_size = executable->stack_size;
	program->code.data_size = executable->data_size;
	program->code.functions = executable->fff;
	program->code.literals = executable->literals;
	program->code.prototypes = executable->protos;
	program->code.ffi_bindings = executable->func_map;
	program->code.builtin_libraries = executable->lib;
	program->code.loaded_libraries = executable->loaded_libs;
	program->code.program_libraries = program->libraries;
	program->code.allocation_owner = executable->compile_pcb;
	++vm->program_count;
	*out_program = program;
	return POCO_STATUS_OK;
}

PocoStatus poco_vm_create(const PocoVmOptions* options, PocoVm** out_vm)
{
	PocoVm* vm;
	PocoStatus status;

	if (out_vm == NULL) {
		return POCO_STATUS_NULL_REFERENCE;
	}
	*out_vm = NULL;
	vm = calloc(1, sizeof(*vm));
	if (vm == NULL) {
		return POCO_STATUS_OUT_OF_MEMORY;
	}
	vm->diagnostic_lock = po_lock_create();
	if (vm->diagnostic_lock == NULL) {
		free(vm);
		return POCO_STATUS_OUT_OF_MEMORY;
	}
	vm->pointer_registry = poco_pointer_registry_create();
	if (vm->pointer_registry == NULL) {
		po_lock_destroy(vm->diagnostic_lock);
		free(vm);
		return POCO_STATUS_OUT_OF_MEMORY;
	}
	if (options != NULL) {
		vm->diagnostic_callback = options->diagnostic_callback;
		vm->diagnostic_user_data = options->diagnostic_user_data;
		vm->verbose = options->verbose;
		if (options->module_hooks != NULL) {
			vm->module_hooks = *options->module_hooks;
		}
	}
	status = poco_vm_set_include_paths(vm, options != NULL ? options->include_paths : NULL,
									   options != NULL ? options->include_path_count : 0);
	if (status != POCO_STATUS_OK) {
		poco_api_free_vm(vm);
		return status;
	}
	*out_vm = vm;
	return POCO_STATUS_OK;
}

void poco_vm_destroy(PocoVm* vm)
{
	if (vm == NULL) {
		return;
	}
	vm->destroy_requested = 1;
	if (vm->program_count == 0) {
		poco_api_free_vm(vm);
	}
}

PocoStatus poco_vm_register_borrowed_span(PocoVm* vm, void* pointer, size_t byte_count,
										  uint32_t permissions)
{
	Errcode status;

	if (vm == NULL) {
		return POCO_STATUS_NULL_REFERENCE;
	}
	if (vm->destroy_requested) {
		return POCO_STATUS_PARAMETER_RANGE;
	}
	status = poco_pointer_registry_register_borrowed(vm->pointer_registry, pointer, byte_count,
													 permissions);
	return (PocoStatus)status;
}

PocoStatus poco_vm_unregister_borrowed_span(PocoVm* vm, const void* pointer)
{
	Errcode status;

	if (vm == NULL) {
		return POCO_STATUS_NULL_REFERENCE;
	}
	if (vm->destroy_requested) {
		return POCO_STATUS_PARAMETER_RANGE;
	}
	status = poco_pointer_registry_unregister_borrowed(vm->pointer_registry, pointer);
	return (PocoStatus)status;
}

PocoStatus poco_vm_set_include_paths(PocoVm* vm, const char* const* paths, size_t path_count)
{
	Names* first = NULL;
	Names* tail = NULL;
	Names* entry;
	size_t index;

	if (vm == NULL) {
		return POCO_STATUS_NULL_REFERENCE;
	}
	if (vm->destroy_requested || (path_count != 0 && paths == NULL)) {
		return POCO_STATUS_PARAMETER_RANGE;
	}
	for (index = 0; index < path_count; ++index) {
		if (paths[index] == NULL) {
			goto INVALID_PATH;
		}
		entry = calloc(1, sizeof(*entry));
		if (entry == NULL) {
			goto OUT_OF_MEMORY;
		}
		entry->name = po_copy_string(paths[index]);
		if (entry->name == NULL) {
			free(entry);
			goto OUT_OF_MEMORY;
		}
		if (tail != NULL) {
			tail->next = entry;
		} else {
			first = entry;
		}
		tail = entry;
	}
	poco_api_free_names(vm->include_dirs);
	vm->include_dirs = first;
	return POCO_STATUS_OK;

INVALID_PATH:
	poco_api_free_names(first);
	return POCO_STATUS_PARAMETER_RANGE;
OUT_OF_MEMORY:
	poco_api_free_names(first);
	return POCO_STATUS_OUT_OF_MEMORY;
}

PocoStatus poco_vm_add_library_path(PocoVm* vm, const char* path)
{
	Names* entry;

	if (vm == NULL || path == NULL) {
		return POCO_STATUS_NULL_REFERENCE;
	}
	if (vm->destroy_requested || path[0] == '\0') {
		return POCO_STATUS_PARAMETER_RANGE;
	}
	entry = calloc(1, sizeof(*entry));
	if (entry == NULL) {
		return POCO_STATUS_OUT_OF_MEMORY;
	}
	entry->name = po_copy_string(path);
	if (entry->name == NULL) {
		free(entry);
		return POCO_STATUS_OUT_OF_MEMORY;
	}
	if (vm->library_dirs_tail != NULL) {
		vm->library_dirs_tail->next = entry;
	} else {
		vm->library_dirs = entry;
	}
	vm->library_dirs_tail = entry;
	return POCO_STATUS_OK;
}

void poco_vm_disable_poe_libraries(PocoVm* vm)
{
	if (vm != NULL) {
		vm->poe_libraries_disabled = 1;
	}
}

PocoStatus poco_vm_register_library_with_runtime_cleanup(PocoVm* vm, const PocoLibrary* library,
														 PocoLibraryRuntimeCleanup runtime_cleanup)
{
	Poco_registered_library* registered;
	Poco_registered_library* existing;
	size_t index;

	if (vm == NULL || library == NULL || library->identity == NULL) {
		return POCO_STATUS_NULL_REFERENCE;
	}
	if (vm->destroy_requested || library->binding_count == 0 || library->bindings == NULL) {
		return POCO_STATUS_PARAMETER_RANGE;
	}
	for (existing = vm->libraries; existing != NULL; existing = existing->next) {
		const char* existing_identity = existing->kind == POCO_REGISTERED_PUBLIC_LIBRARY
											? existing->library.identity
											: existing->legacy_library->name;
		if (strcmp(existing_identity, library->identity) == 0) {
			return POCO_STATUS_PARAMETER_RANGE;
		}
	}

	registered = calloc(1, sizeof(*registered));
	if (registered == NULL) {
		return POCO_STATUS_OUT_OF_MEMORY;
	}
	registered->kind = POCO_REGISTERED_PUBLIC_LIBRARY;
	registered->runtime_cleanup = runtime_cleanup;
	registered->library = *library;
	registered->library.identity = po_copy_string(library->identity);
	if (registered->library.identity == NULL) {
		goto OUT_OF_MEMORY;
	}
	if (library->binding_count != 0) {
		registered->bindings = calloc(library->binding_count, sizeof(*registered->bindings));
		if (registered->bindings == NULL) {
			goto OUT_OF_MEMORY;
		}
		registered->contracts = calloc(library->binding_count, sizeof(*registered->contracts));
		registered->pointer_contracts =
			calloc(library->binding_count, sizeof(*registered->pointer_contracts));
		if (registered->contracts == NULL || registered->pointer_contracts == NULL) {
			goto OUT_OF_MEMORY;
		}
		for (index = 0; index < library->binding_count; ++index) {
			if (library->bindings[index].prototype == NULL ||
				library->bindings[index].function == NULL ||
				(library->bindings[index].flags & ~POCO_BINDING_RUN_CONTEXT) != 0) {
				goto INVALID_BINDING;
			}
			registered->bindings[index] = library->bindings[index];
			registered->bindings[index].prototype =
				po_copy_string(library->bindings[index].prototype);
			if (registered->bindings[index].prototype == NULL) {
				goto OUT_OF_MEMORY;
			}
			if (library->bindings[index].contract != NULL) {
				const PocoBindingContract* source = library->bindings[index].contract;
				PocoBindingContract* destination = &registered->contracts[index];

				if (source->pointer_contract_count != 0 && source->pointer_contracts == NULL) {
					goto INVALID_BINDING;
				}
				*destination = *source;
				if (source->pointer_contract_count != 0) {
					registered->pointer_contracts[index] =
						calloc(source->pointer_contract_count,
							   sizeof(*registered->pointer_contracts[index]));
					if (registered->pointer_contracts[index] == NULL) {
						goto OUT_OF_MEMORY;
					}
					memcpy(registered->pointer_contracts[index], source->pointer_contracts,
						   source->pointer_contract_count *
							   sizeof(*registered->pointer_contracts[index]));
					destination->pointer_contracts = registered->pointer_contracts[index];
				}
				registered->bindings[index].contract = destination;
			}
		}
		registered->library.bindings = registered->bindings;
	}
	return poco_api_append_registered_library(vm, registered);

INVALID_BINDING:
	poco_api_free_registered_library(registered);
	return POCO_STATUS_PARAMETER_RANGE;
OUT_OF_MEMORY:
	poco_api_free_registered_library(registered);
	return POCO_STATUS_OUT_OF_MEMORY;
}

PocoStatus poco_vm_register_library(PocoVm* vm, const PocoLibrary* library)
{
	return poco_vm_register_library_with_runtime_cleanup(vm, library, NULL);
}

PocoStatus poco_vm_register_standard_library(PocoVm* vm)
{
	Poco_registered_library* previous_tail;
	PocoStatus status;

	if (vm == NULL) {
		return POCO_STATUS_NULL_REFERENCE;
	}
	if (vm->destroy_requested) {
		return POCO_STATUS_PARAMETER_RANGE;
	}
	if (vm->standard_library_registered) {
		return POCO_STATUS_OK;
	}

	previous_tail = vm->libraries_tail;
	status = poco_register_standard_library_catalog(vm);
	if (status != POCO_STATUS_OK) {
		Poco_registered_library* first_new =
			previous_tail != NULL ? previous_tail->next : vm->libraries;
		if (previous_tail != NULL) {
			previous_tail->next = NULL;
		} else {
			vm->libraries = NULL;
		}
		vm->libraries_tail = previous_tail;
		poco_api_free_registered_libraries(first_new);
		return status;
	}
	vm->standard_library_registered = 1;
	return POCO_STATUS_OK;
}

PocoStatus poco_vm_register_trusted_graph_library(PocoVm* vm)
{
	PocoStatus status;

	if (vm == NULL) {
		return POCO_STATUS_NULL_REFERENCE;
	}
	if (vm->destroy_requested) {
		return POCO_STATUS_PARAMETER_RANGE;
	}
	if (vm->trusted_graph_library_registered) {
		return POCO_STATUS_OK;
	}

	status = poco_vm_register_library(vm, poco_standard_math_library());
	if (status == POCO_STATUS_OK) {
		vm->trusted_graph_library_registered = 1;
	}
	return status;
}

PocoStatus poco_vm_register_untrusted_expression_library(PocoVm* vm)
{
	PocoStatus status;

	if (vm == NULL) {
		return POCO_STATUS_NULL_REFERENCE;
	}
	if (vm->destroy_requested) {
		return POCO_STATUS_PARAMETER_RANGE;
	}
	if (vm->untrusted_expression_library_registered) {
		return POCO_STATUS_OK;
	}
	/* Changing an already-published program's VM into the restrictive tier
	 * could leave that program holding pointer-capable FFI descriptors. */
	if (vm->program_count != 0) {
		return POCO_STATUS_PARAMETER_RANGE;
	}

	/* Set the policy before publishing the table so every subsequent compile
	 * uses the restrictive tier.  Roll it back if registration fails. */
	vm->untrusted_expression_library_registered = 1;
	status = poco_vm_register_library(vm, poco_standard_math_library());
	if (status != POCO_STATUS_OK) {
		vm->untrusted_expression_library_registered = 0;
	}
	return status;
}

static int poco_api_replace_frame_source_path(Poco_cb* owner, Func_frame* frames,
											  const char* source_path, const char* source_name)
{
	Func_frame* frame;

	for (frame = frames; frame != NULL; frame = frame->next) {
		char* replacement;

		if (frame->name == NULL || strcmp(frame->name, source_path) != 0) {
			continue;
		}
		replacement = po_clone_string(owner, source_name);
		if (replacement == NULL) {
			return 0;
		}
		frame->name = replacement;
	}
	return 1;
}

/* A file path is compiler input for include/module resolution, not minimal
 * serialized metadata. Replace the main frame's path-shaped name after the
 * compile has finished but before the immutable program is published. */
static int poco_api_hide_compiled_source_path(Poco_run_env* executable, const char* source_path,
											  const char* source_name)
{
	if (source_path == NULL) {
		return 1;
	}
	return poco_api_replace_frame_source_path(executable->compile_pcb, executable->fff, source_path,
											  source_name) &&
		   poco_api_replace_frame_source_path(executable->compile_pcb, executable->protos,
											  source_path, source_name);
}

PocoStatus po_vm_compile_sources(PocoVm* vm, const char* const* source_names,
								 const char* const* physical_source_paths,
								 const char* const* sources, const size_t* source_lengths,
								 Names* const* include_dirs, const size_t* const* use_indices,
								 const size_t* use_counts, size_t source_count,
								 PocoProgram** out_program)
{
	PocoProgram* program;
	Poco_lib* libraries;
	const char* stored_source_name;
	const char* separator;
	const char* alternate_separator;
	char* error_source;
	size_t error_source_capacity;
	size_t source_index;
	PocoStatus status;
	Errcode compile_status;
	long error_line = 0;
	int error_column = 0;

	if (vm == NULL || source_names == NULL || physical_source_paths == NULL || sources == NULL ||
		source_lengths == NULL || include_dirs == NULL || out_program == NULL) {
		return POCO_STATUS_NULL_REFERENCE;
	}
	if (vm->destroy_requested) {
		return POCO_STATUS_PARAMETER_RANGE;
	}
	*out_program = NULL;
	if (source_count == 0) {
		return POCO_STATUS_PARAMETER_RANGE;
	}
	stored_source_name = source_names[0];
	error_source_capacity = PATH_SIZE;
	for (source_index = 0; source_index < source_count; ++source_index) {
		if (source_names[source_index] == NULL || sources[source_index] == NULL) {
			return POCO_STATUS_NULL_REFERENCE;
		}
		if (memchr(sources[source_index], '\0', source_lengths[source_index]) != NULL) {
			poco_set_error(vm, "Source buffer contains an embedded NUL byte");
			po_vm_report(vm, POCO_STATUS_PARAMETER_RANGE, source_names[source_index], 0, 0,
						 poco_get_last_error(vm));
			return POCO_STATUS_PARAMETER_RANGE;
		}
		if (strlen(source_names[source_index]) + 1 > error_source_capacity) {
			error_source_capacity = strlen(source_names[source_index]) + 1;
		}
	}
	if (error_source_capacity < PATH_SIZE) {
		error_source_capacity = PATH_SIZE;
	}
	error_source = malloc(error_source_capacity);
	if (error_source == NULL) {
		return POCO_STATUS_OUT_OF_MEMORY;
	}
	error_source[0] = '\0';
	program = calloc(1, sizeof(*program));
	if (program == NULL) {
		free(error_source);
		return POCO_STATUS_OUT_OF_MEMORY;
	}
	program->sources = calloc(source_count, sizeof(*program->sources));
	if (program->sources == NULL) {
		free(program);
		free(error_source);
		return POCO_STATUS_OUT_OF_MEMORY;
	}
	program->source_count = source_count;
	for (source_index = 0; source_index < source_count; ++source_index) {
		const char* stored_name = source_names[source_index];
		const char* source_separator = strrchr(stored_name, '/');
		const char* alternate = strrchr(stored_name, '\\');

		if (alternate != NULL && (source_separator == NULL || alternate > source_separator)) {
			source_separator = alternate;
		}
		if (physical_source_paths[source_index] != NULL && source_separator != NULL &&
			source_separator[1] != '\0') {
			stored_name = source_separator + 1;
		}
		program->sources[source_index].name = po_copy_string(stored_name);
		program->sources[source_index].path =
			physical_source_paths[source_index] != NULL
				? po_copy_string(physical_source_paths[source_index])
				: NULL;
		if (program->sources[source_index].name == NULL ||
			(physical_source_paths[source_index] != NULL &&
			 program->sources[source_index].path == NULL)) {
			size_t cleanup_index;
			for (cleanup_index = 0; cleanup_index <= source_index; ++cleanup_index) {
				free(program->sources[cleanup_index].path);
				free(program->sources[cleanup_index].name);
			}
			free(program->sources);
			free(program);
			free(error_source);
			return POCO_STATUS_OUT_OF_MEMORY;
		}
		po_blake3_hash(sources[source_index], source_lengths[source_index],
					   program->sources[source_index].hash);
	}
	if (physical_source_paths[0] != NULL) {
		separator = strrchr(source_names[0], '/');
		alternate_separator = strrchr(source_names[0], '\\');
		if (alternate_separator != NULL && (separator == NULL || alternate_separator > separator)) {
			separator = alternate_separator;
		}
		if (separator != NULL && separator[1] != '\0') {
			stored_source_name = separator + 1;
		}
	}
	program->source_name = malloc(strlen(stored_source_name) + 1);
	if (program->source_name == NULL) {
		for (source_index = 0; source_index < program->source_count; ++source_index) {
			free(program->sources[source_index].path);
			free(program->sources[source_index].name);
		}
		free(program->sources);
		free(program);
		free(error_source);
		return POCO_STATUS_OUT_OF_MEMORY;
	}
	memcpy(program->source_name, stored_source_name, strlen(stored_source_name) + 1);
	if (physical_source_paths[0] != NULL) {
		program->source_path = malloc(strlen(physical_source_paths[0]) + 1);
		if (program->source_path == NULL) {
			for (source_index = 0; source_index < program->source_count; ++source_index) {
				free(program->sources[source_index].path);
				free(program->sources[source_index].name);
			}
			free(program->sources);
			free(program->source_name);
			free(program);
			free(error_source);
			return POCO_STATUS_OUT_OF_MEMORY;
		}
		memcpy(program->source_path, physical_source_paths[0],
			   strlen(physical_source_paths[0]) + 1);
	}
	program->debug_level = POCO_DEBUG_LEVEL_MINIMAL;
	/* The program-level hash tracks the primary unit; per-unit hashes live in
	 * the source table. */
	po_blake3_hash(sources[0], source_lengths[0], program->source_hash);
	status = poco_api_make_program_libraries(vm, &program->libraries, &libraries);
	if (status != POCO_STATUS_OK) {
		free(error_source);
		free(program->source_path);
		free(program->source_name);
		for (source_index = 0; source_index < program->source_count; ++source_index) {
			free(program->sources[source_index].path);
			free(program->sources[source_index].name);
		}
		free(program->sources);
		free(program);
		return status;
	}
	compile_status = compile_poco_files_with_vm(
		vm, &program->executable, source_names, physical_source_paths, sources, source_lengths,
		include_dirs, use_indices, use_counts, source_count, libraries, error_source,
		error_source_capacity, &error_line, &error_column, vm->verbose != 0);
	if (compile_status != Success) {
		PocoStatus public_status =
			compile_status == Err_in_err_file ? POCO_STATUS_REPORTED : (PocoStatus)compile_status;
		po_vm_report(vm, public_status, error_source, error_line, error_column,
					 poco_get_last_error(vm));
		if (program->executable != NULL) {
			po_free_executable(&program->executable);
		}
		poco_api_free_program_libraries(program->libraries);
		free(program->source_path);
		free(program->source_name);
		for (source_index = 0; source_index < program->source_count; ++source_index) {
			free(program->sources[source_index].path);
			free(program->sources[source_index].name);
		}
		free(program->sources);
		free(program);
		free(error_source);
		return public_status;
	}
	free(error_source);
	for (source_index = 0; source_index < source_count; ++source_index) {
		const char* unit_name = source_names[source_index];

		if (physical_source_paths[source_index] != NULL) {
			separator = strrchr(unit_name, '/');
			alternate_separator = strrchr(unit_name, '\\');
			if (alternate_separator != NULL &&
				(separator == NULL || alternate_separator > separator)) {
				separator = alternate_separator;
			}
			if (separator != NULL && separator[1] != '\0') {
				unit_name = separator + 1;
			}
		}
		if (!poco_api_hide_compiled_source_path(program->executable,
												physical_source_paths[source_index], unit_name)) {
			po_free_executable(&program->executable);
			poco_api_free_program_libraries(program->libraries);
			free(program->source_path);
			free(program->source_name);
			for (source_index = 0; source_index < program->source_count; ++source_index) {
				free(program->sources[source_index].path);
				free(program->sources[source_index].name);
			}
			free(program->sources);
			free(program);
			return POCO_STATUS_OUT_OF_MEMORY;
		}
	}
	program->vm = vm;
	{
		Poco_run_env* executable = program->executable;
		program->code.stack_size = executable->stack_size;
		program->code.data_size = executable->data_size;
		program->code.functions = executable->fff;
		program->code.literals = executable->literals;
		program->code.prototypes = executable->protos;
		program->code.ffi_bindings = executable->func_map;
		program->code.builtin_libraries = executable->lib;
		program->code.loaded_libraries = executable->loaded_libs;
		program->code.program_libraries = program->libraries;
		program->code.allocation_owner = executable->compile_pcb;
	}
	++vm->program_count;
	*out_program = program;
	return POCO_STATUS_OK;
}

static PocoStatus poco_vm_compile_source(PocoVm* vm, const char* source_name,
										 const char* physical_source_path, const char* source,
										 size_t source_length, Names* include_dirs,
										 PocoProgram** out_program)
{
	const char* source_names[] = {source_name};
	const char* physical_source_paths[] = {physical_source_path};
	const char* sources[] = {source};
	size_t source_lengths[] = {source_length};
	Names* source_include_dirs[] = {include_dirs};

	return po_vm_compile_sources(vm, source_names, physical_source_paths, sources, source_lengths,
								 source_include_dirs, NULL, NULL, 1, out_program);
}

PocoStatus poco_vm_compile_buffer(PocoVm* vm, const char* source_name, const char* source,
								  size_t source_length, PocoProgram** out_program)
{
	return poco_vm_compile_source(vm, source_name, NULL, source, source_length,
								  vm != NULL ? vm->include_dirs : NULL, out_program);
}

PocoStatus poco_vm_run(PocoVm* vm, PocoProgram* program, const PocoRunOptions* options,
					   int32_t* out_result)
{
	PocoActivation* activation = NULL;
	PocoStatus status;

	if (vm == NULL || program == NULL) {
		return POCO_STATUS_NULL_REFERENCE;
	}
	if (vm->destroy_requested || program->vm != vm) {
		return POCO_STATUS_PARAMETER_RANGE;
	}
	status = poco_activation_acquire(program, &activation);
	if (status != POCO_STATUS_OK) {
		return status;
	}
	status = poco_activation_run(activation, options, out_result);
	poco_activation_release(activation);
	return status;
}

void poco_program_destroy(PocoProgram* program)
{
	PocoVm* vm;

	if (program == NULL) {
		return;
	}
	vm = program->vm;
	if (program->executable != NULL) {
		po_free_executable(&program->executable);
	}
	poco_api_free_program_libraries(program->libraries);
	if (program->sources != NULL) {
		size_t source_index;
		for (source_index = 0; source_index < program->source_count; ++source_index) {
			free(program->sources[source_index].path);
			free(program->sources[source_index].name);
		}
	}
	free(program->sources);
	free(program->source_path);
	free(program->source_name);
	free(program);
	if (vm != NULL) {
		--vm->program_count;
		if (vm->destroy_requested && vm->program_count == 0) {
			poco_api_free_vm(vm);
		}
	}
}
