/*****************************************************************************
 *
 * pocoface.c - interface between poco and programs calling poco.
 *
 *	Routine to compile a source file and pass back an executable
 *	memory structure (compile_poco),  execute that structure (run_poco)
 *	and free it up (free_poco).   Most of the work of this module
 *	is converting the function library passed into compile_poco into
 *	the rather complex type-structures used internally.
 *
 * MAINTENANCE:
 *	08/18/90	(Ian)
 *				Added detection and handling of a NULL library list pointer
 *				in compile_poco().	(OOOPS, except it doesn't work right.
 *				actually, poco runs fine this way standalone, but it locks
 *				up the machine if run under TD or TPROF.  I hope this isn't
 *				a symptom of some deeper problem.)
 *	08/27/90	(Ian)
 *				Added calls to init and free the new memory management system.
 *	08/29/90	(Ian)
 *				Added setjmp/longjmp error handling to poco.  This largely
 *				affected the compile_poco() routine; most error handling and
 *				cleanup actions now occur therein.	Also, new logic was added
 *				to close all files in the file_stack linked list during error
 *				handling, (old version closed the most recently opened file).
 *	08/30/90	(Jim)
 *				Added file close call for pcb->t.err_file in compile_poco().
 *	09/04/90	(Ian)
 *				Ooops.	Turns out po_free_pp() contains the logic to close all
 *				open files.  So, the loop I coded to do that was removed,
 *				and a po_free_pp() call was inserted.
 *	10/21/90	(Ian)
 *				New interface to library prototype data.  Deleted routines
 *				pod_init(), pod_next(), prol() and fixup_lib_symbols(),
 *				replaced them with new routine poc_get_libproto_line().
 *	10/25/90	(Ian)
 *				Reworked the library prototype interface some more, added new
 *				routine po_open_library(), but we still need a function on
 *				the PJ side to actually load libraries for us.	Right now,
 *				the open library function handles only the builtin libraries
 *				passed to us by pj at compile time.
 *	04/14/91	(Peter)
 *				Modified po_open_library to take id_string argument
 *	05/01/91	(Ian)
 *				Added builtin_err declaration.	This global variable is now
 *				owned by poco instead of by the host, because we need to use
 *				it during the compile phase to detect math errors during the
 *				folding of constants.  Also, run_poco() was changed to
 *				remove the pointer to builtin_err that used to get passed in.
 *	01/10/92	(Ian)
 *				Major bugfix in po_get_libproto_line(), to fix the bug that
 *				prevented using multiple #pragma library statements.  See
 *				the comments in that function's header block for details.
 *	05/17/92	(Ian)
 *				Added a sanity check to po_get_libproto_line() to catch
 *				NULL proto string pointers.
 *	09/17/92	(Ian)
 *				Tweaked logic used to report the error file name and line
 *				number back to the host.  We used to return the line number
 *				from curtoken, but if the error was in a preprocessor
 *				statement this could be wildly innacurate.	Now, we have a
 *				new field in the pcb that carries the error line number;
 *				it is set by the error reporting routines in either the
 *				parser or the preprocessor.  Also, if an error happens
 *				in parsing prototypes in a library, we now report the file
 *				and line number of the #pragma poco library statement.
 *				And finally, restored handling of the character position
 *				on the line where the error was detected.  Provisions for
 *				this existed in the tokenizer, but it wasn't being used.
 ****************************************************************************/

#include "jfile.h"
#include "filepath.h"
#include <poco/poco.h>
#include "pocoface.h"
#include "pocoload.h"
#include "poco_errcodes.h"
#include "poco.h"
#include "standard_library.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*****************************************************************************
 * some global vars...
 ****************************************************************************/


int po_version_number = VRSN_NUM; /* Global version number for PJ's use.    */
extern Errcode builtin_err;		  /* External library/floating point error.	*/

static Poco_run_env* porunenv; /* -> run env; valid only when poco pgm running */

static char poco_last_error[512] = "";

void poco_set_error(const char* fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vsnprintf(poco_last_error, sizeof(poco_last_error), fmt, args);
	va_end(args);
}

const char* poco_get_error(void)
{
	return poco_last_error;
}

Poco_lib* poco_active_library(const char* identity)
{
	Poco_lib* library;

	if (porunenv == NULL || identity == NULL)
		return NULL;
	for (library = porunenv->lib; library != NULL; library = library->next) {
		if (library->name != NULL && strcmp(library->name, identity) == 0)
			return library;
	}
	return NULL;
}

PocoPointerRegistry* poco_active_pointer_registry(void)
{
	return porunenv != NULL ? porunenv->pointer_registry : NULL;
}

/*
 * Public embedding API
 *
 * The original compiler interface consumes Poco_lib and Names control blocks.
 * Keep those implementation details here: a host registers public descriptors
 * and each compiled program receives an owned, immutable legacy-compatible
 * snapshot.  This lets registrations remain deterministic while neither the
 * host nor an already compiled program can observe internal control blocks.
 */
typedef enum Poco_registered_library_kind
{
	POCO_REGISTERED_PUBLIC_LIBRARY,
	POCO_REGISTERED_LEGACY_LIBRARY
} Poco_registered_library_kind;

typedef struct Poco_registered_library
{
	struct Poco_registered_library* next;
	Poco_registered_library_kind kind;
	PocoLibrary library;
	PocoBinding* bindings;
	PocoBindingContract* contracts;
	PocoBindingPointerContract** pointer_contracts;
	Poco_lib* legacy_library;
	PocoLibraryRuntimeCleanup runtime_cleanup;
} Poco_registered_library;

typedef struct Poco_program_library
{
	struct Poco_program_library* next;
	Poco_lib library;
	Lib_proto* prototypes;
	PocoLibrary* public_library;
	PocoLibraryRuntimeCleanup runtime_cleanup;
	int initialized;
} Poco_program_library;

struct PocoVm
{
	Names* include_dirs;
	Poco_registered_library* libraries;
	Poco_registered_library* libraries_tail;
	PocoDiagnosticCallback diagnostic_callback;
	void* diagnostic_user_data;
	int verbose;
	PocoModuleHooks module_hooks;
	PocoPointerRegistry* pointer_registry;
	int standard_library_registered;
	int program_count;
	int destroy_requested;
};

struct PocoProgram
{
	PocoVm* vm;
	void* executable;
	Poco_program_library* libraries;
};

static char* poco_api_copy_string(const char* value)
{
	size_t length;
	char* copy;

	if (value == NULL)
		return NULL;
	length = strlen(value) + 1;
	copy = malloc(length);
	if (copy != NULL)
		memcpy(copy, value, length);
	return copy;
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

	if (registered == NULL)
		return;
	if (registered->kind == POCO_REGISTERED_PUBLIC_LIBRARY) {
		if (registered->bindings != NULL) {
			for (index = 0; index < registered->library.binding_count; ++index) {
				free((char*)registered->bindings[index].prototype);
				free(registered->pointer_contracts != NULL ?
					registered->pointer_contracts[index] : NULL);
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
	if (vm == NULL)
		return;
	poco_api_free_names(vm->include_dirs);
	poco_api_free_registered_libraries(vm->libraries);
	poco_pointer_registry_destroy(vm->pointer_registry);
	free(vm);
}

static void poco_api_report(PocoVm* vm, PocoStatus status, const char* source_name,
	long line, int column, const char* message)
{
	PocoDiagnostic diagnostic;

	if (vm == NULL || vm->diagnostic_callback == NULL)
		return;
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

	if (program_library == NULL)
		return Err_poco_internal;
	if (public_library == NULL || public_library->initialize == NULL)
		status = POCO_STATUS_OK;
	else
		status = public_library->initialize(public_library);
	if (status == POCO_STATUS_OK)
		program_library->initialized = 1;
	return (Errcode)status;
}

static void poco_api_library_cleanup(Poco_lib* library)
{
	Poco_program_library* program_library = library != NULL ? library->local_data : NULL;
	PocoLibrary* public_library = program_library != NULL ? program_library->public_library : NULL;

	if (program_library != NULL && program_library->initialized &&
		program_library->runtime_cleanup != NULL)
		program_library->runtime_cleanup(library);
	if (program_library != NULL && program_library->initialized &&
		public_library != NULL && public_library->cleanup != NULL)
		public_library->cleanup(public_library);
	if (program_library != NULL)
		program_library->initialized = 0;
}

static PocoStatus poco_api_append_registered_library(PocoVm* vm,
	Poco_registered_library* registered)
{
	if (vm == NULL || registered == NULL)
		return POCO_STATUS_NULL_REFERENCE;
	if (vm->libraries_tail != NULL)
		vm->libraries_tail->next = registered;
	else
		vm->libraries = registered;
	vm->libraries_tail = registered;
	return POCO_STATUS_OK;
}

static PocoStatus poco_api_register_legacy_library(PocoVm* vm, Poco_lib* library)
{
	Poco_registered_library* registered;

	if (library == NULL || library->name == NULL)
		return POCO_STATUS_INTERNAL_ERROR;
	registered = calloc(1, sizeof(*registered));
	if (registered == NULL)
		return POCO_STATUS_OUT_OF_MEMORY;
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

static PocoStatus poco_api_make_program_libraries(PocoVm* vm,
	Poco_program_library** out_libraries, Poco_lib** out_first_library)
{
	Poco_registered_library* registered;
	Poco_program_library* first = NULL;
	Poco_program_library* tail = NULL;
	Poco_program_library* program_library;
	size_t index;

	if (out_libraries == NULL || out_first_library == NULL)
		return POCO_STATUS_NULL_REFERENCE;
	*out_libraries = NULL;
	*out_first_library = NULL;

	for (registered = vm->libraries; registered != NULL; registered = registered->next) {
		program_library = calloc(1, sizeof(*program_library));
		if (program_library == NULL)
			goto OUT_OF_MEMORY;

		if (registered->kind == POCO_REGISTERED_LEGACY_LIBRARY) {
			program_library->library = *registered->legacy_library;
			program_library->library.next = NULL;
		} else {
			if (registered->library.binding_count != 0) {
				program_library->prototypes = calloc(registered->library.binding_count,
					sizeof(*program_library->prototypes));
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

PocoStatus poco_vm_create(const PocoVmOptions* options, PocoVm** out_vm)
{
	PocoVm* vm;
	PocoStatus status;

	if (out_vm == NULL)
		return POCO_STATUS_NULL_REFERENCE;
	*out_vm = NULL;
	vm = calloc(1, sizeof(*vm));
	if (vm == NULL)
		return POCO_STATUS_OUT_OF_MEMORY;
	vm->pointer_registry = poco_pointer_registry_create();
	if (vm->pointer_registry == NULL) {
		free(vm);
		return POCO_STATUS_OUT_OF_MEMORY;
	}
	if (options != NULL) {
		vm->diagnostic_callback = options->diagnostic_callback;
		vm->diagnostic_user_data = options->diagnostic_user_data;
		vm->verbose = options->verbose;
		if (options->module_hooks != NULL)
			vm->module_hooks = *options->module_hooks;
	}
	status = poco_vm_set_include_paths(vm,
		options != NULL ? options->include_paths : NULL,
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
	if (vm == NULL)
		return;
	vm->destroy_requested = 1;
	if (vm->program_count == 0)
		poco_api_free_vm(vm);
}

PocoStatus poco_vm_register_borrowed_span(PocoVm* vm, void* pointer,
	size_t byte_count, uint32_t permissions)
{
	Errcode status;

	if (vm == NULL)
		return POCO_STATUS_NULL_REFERENCE;
	if (vm->destroy_requested)
		return POCO_STATUS_PARAMETER_RANGE;
	status = poco_pointer_registry_register_borrowed(vm->pointer_registry,
		pointer, byte_count, permissions);
	return (PocoStatus)status;
}

PocoStatus poco_vm_unregister_borrowed_span(PocoVm* vm, const void* pointer)
{
	Errcode status;

	if (vm == NULL)
		return POCO_STATUS_NULL_REFERENCE;
	if (vm->destroy_requested)
		return POCO_STATUS_PARAMETER_RANGE;
	status = poco_pointer_registry_unregister_borrowed(vm->pointer_registry, pointer);
	return (PocoStatus)status;
}

PocoStatus poco_vm_set_include_paths(PocoVm* vm, const char* const* paths, size_t path_count)
{
	Names* first = NULL;
	Names* tail = NULL;
	Names* entry;
	size_t index;

	if (vm == NULL)
		return POCO_STATUS_NULL_REFERENCE;
	if (vm->destroy_requested || (path_count != 0 && paths == NULL))
		return POCO_STATUS_PARAMETER_RANGE;
	/* Poco's legacy preprocessor resolves even the root source through its
	 * include search list.  Keep an empty entry first so an absolute or
	 * caller-relative source name is always usable, then append host paths. */
	entry = calloc(1, sizeof(*entry));
	if (entry == NULL)
		goto OUT_OF_MEMORY;
	entry->name = poco_api_copy_string("");
	if (entry->name == NULL) {
		free(entry);
		goto OUT_OF_MEMORY;
	}
	first = tail = entry;
	for (index = 0; index < path_count; ++index) {
		if (paths[index] == NULL)
			goto INVALID_PATH;
		entry = calloc(1, sizeof(*entry));
		if (entry == NULL)
			goto OUT_OF_MEMORY;
		entry->name = poco_api_copy_string(paths[index]);
		if (entry->name == NULL) {
			free(entry);
			goto OUT_OF_MEMORY;
		}
		if (tail != NULL)
			tail->next = entry;
		else
			first = entry;
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

PocoStatus poco_vm_register_library_with_runtime_cleanup(PocoVm* vm,
	const PocoLibrary* library, PocoLibraryRuntimeCleanup runtime_cleanup)
{
	Poco_registered_library* registered;
	Poco_registered_library* existing;
	size_t index;

	if (vm == NULL || library == NULL || library->identity == NULL)
		return POCO_STATUS_NULL_REFERENCE;
	if (vm->destroy_requested ||
		library->binding_count == 0 || library->bindings == NULL)
		return POCO_STATUS_PARAMETER_RANGE;
	for (existing = vm->libraries; existing != NULL; existing = existing->next) {
		const char* existing_identity = existing->kind == POCO_REGISTERED_PUBLIC_LIBRARY
			? existing->library.identity : existing->legacy_library->name;
		if (strcmp(existing_identity, library->identity) == 0)
			return POCO_STATUS_PARAMETER_RANGE;
	}

	registered = calloc(1, sizeof(*registered));
	if (registered == NULL)
		return POCO_STATUS_OUT_OF_MEMORY;
	registered->kind = POCO_REGISTERED_PUBLIC_LIBRARY;
	registered->runtime_cleanup = runtime_cleanup;
	registered->library = *library;
	registered->library.identity = poco_api_copy_string(library->identity);
	if (registered->library.identity == NULL)
		goto OUT_OF_MEMORY;
	if (library->binding_count != 0) {
		registered->bindings = calloc(library->binding_count, sizeof(*registered->bindings));
		if (registered->bindings == NULL)
			goto OUT_OF_MEMORY;
		registered->contracts = calloc(library->binding_count,
			sizeof(*registered->contracts));
		registered->pointer_contracts = calloc(library->binding_count,
			sizeof(*registered->pointer_contracts));
		if (registered->contracts == NULL || registered->pointer_contracts == NULL)
			goto OUT_OF_MEMORY;
		for (index = 0; index < library->binding_count; ++index) {
			if (library->bindings[index].prototype == NULL || library->bindings[index].function == NULL)
				goto INVALID_BINDING;
			registered->bindings[index] = library->bindings[index];
			registered->bindings[index].prototype =
				poco_api_copy_string(library->bindings[index].prototype);
			if (registered->bindings[index].prototype == NULL)
				goto OUT_OF_MEMORY;
			if (library->bindings[index].contract != NULL) {
				const PocoBindingContract* source = library->bindings[index].contract;
				PocoBindingContract* destination = &registered->contracts[index];

				if (source->pointer_contract_count != 0 &&
					source->pointer_contracts == NULL)
					goto INVALID_BINDING;
				*destination = *source;
				if (source->pointer_contract_count != 0) {
					registered->pointer_contracts[index] = calloc(
						source->pointer_contract_count,
						sizeof(*registered->pointer_contracts[index]));
					if (registered->pointer_contracts[index] == NULL)
						goto OUT_OF_MEMORY;
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

	if (vm == NULL)
		return POCO_STATUS_NULL_REFERENCE;
	if (vm->destroy_requested)
		return POCO_STATUS_PARAMETER_RANGE;
	if (vm->standard_library_registered)
		return POCO_STATUS_OK;

	previous_tail = vm->libraries_tail;
	status = poco_register_standard_library_catalog(vm);
	if (status != POCO_STATUS_OK) {
		Poco_registered_library* first_new = previous_tail != NULL ? previous_tail->next : vm->libraries;
		if (previous_tail != NULL)
			previous_tail->next = NULL;
		else
			vm->libraries = NULL;
		vm->libraries_tail = previous_tail;
		poco_api_free_registered_libraries(first_new);
		return status;
	}
	vm->standard_library_registered = 1;
	return POCO_STATUS_OK;
}

PocoStatus poco_vm_compile_file(PocoVm* vm, const char* source_name, PocoProgram** out_program)
{
	PocoProgram* program;
	Poco_lib* libraries;
	PocoStatus status;
	Errcode compile_status;
	PocoModuleHooks previous_module_hooks;
	char error_source[PATH_SIZE];
	long error_line = 0;
	int error_column = 0;

	if (vm == NULL || source_name == NULL || out_program == NULL)
		return POCO_STATUS_NULL_REFERENCE;
	if (vm->destroy_requested)
		return POCO_STATUS_PARAMETER_RANGE;
	*out_program = NULL;
	error_source[0] = '\0';
	program = calloc(1, sizeof(*program));
	if (program == NULL)
		return POCO_STATUS_OUT_OF_MEMORY;
	status = poco_api_make_program_libraries(vm, &program->libraries, &libraries);
	if (status != POCO_STATUS_OK) {
		free(program);
		return status;
	}
	poco_loader_swap_module_hooks(&vm->module_hooks, &previous_module_hooks);
	compile_status = compile_poco(&program->executable, (char*)source_name, NULL, NULL,
		libraries, error_source, &error_line, &error_column, vm->include_dirs, vm->verbose != 0);
	poco_loader_swap_module_hooks(&previous_module_hooks, NULL);
	if (compile_status != Success) {
		PocoStatus public_status = compile_status == Err_in_err_file
			? POCO_STATUS_REPORTED : (PocoStatus)compile_status;
		poco_api_report(vm, public_status, error_source, error_line,
			error_column, poco_get_error());
		if (program->executable != NULL)
			free_poco(&program->executable);
		poco_api_free_program_libraries(program->libraries);
		free(program);
		return public_status;
	}
	program->vm = vm;
	((Poco_run_env*)program->executable)->pointer_registry = vm->pointer_registry;
	++vm->program_count;
	*out_program = program;
	return POCO_STATUS_OK;
}

typedef struct Poco_api_cancel_context
{
	PocoCancelCallback callback;
	void* user_data;
} Poco_api_cancel_context;

static bool poco_api_cancel(void* context)
{
	Poco_api_cancel_context* cancel_context = context;

	return cancel_context != NULL && cancel_context->callback != NULL &&
		cancel_context->callback(cancel_context->user_data) != 0;
}

PocoStatus poco_vm_run(PocoVm* vm, PocoProgram* program,
	const PocoRunOptions* options, int32_t* out_result)
{
	Errcode run_status;
	long error_line = 0;
	Poco_run_env* run_env;
	Poco_api_cancel_context cancel_context;

	if (vm == NULL || program == NULL)
		return POCO_STATUS_NULL_REFERENCE;
	if (vm->destroy_requested || program->vm != vm)
		return POCO_STATUS_PARAMETER_RANGE;
	cancel_context.callback = options != NULL ? options->cancel_callback : NULL;
	cancel_context.user_data = options != NULL ? options->cancel_user_data : NULL;
	run_status = run_poco(&program->executable,
		options != NULL ? (char*)options->trace_file : NULL,
		cancel_context.callback != NULL ? poco_api_cancel : NULL,
		cancel_context.callback != NULL ? &cancel_context : NULL, &error_line);
	/* The legacy interpreter formats library errors for its trace and returns
	 * Err_in_err_file.  Preserve the precise new FFI boundary status at the
	 * public VM API so a host can distinguish a rejected span from a generic
	 * reported script failure. */
	if (run_status == Err_in_err_file && builtin_err == Err_poco_ffi_bounds)
		run_status = Err_poco_ffi_bounds;
	if (run_status == Success && out_result != NULL) {
		run_env = program->executable;
		*out_result = run_env->result.i;
	}
	if (run_status != Success)
		poco_api_report(vm, (PocoStatus)run_status, NULL, error_line, 0,
			"Poco program execution failed");
	return (PocoStatus)run_status;
}

void poco_program_destroy(PocoProgram* program)
{
	PocoVm* vm;

	if (program == NULL)
		return;
	vm = program->vm;
	if (program->executable != NULL)
		free_poco(&program->executable);
	poco_api_free_program_libraries(program->libraries);
	free(program);
	if (vm != NULL) {
		--vm->program_count;
		if (vm->destroy_requested && vm->program_count == 0)
			poco_api_free_vm(vm);
	}
}



#ifdef DEBUG
void po_show_basic_sizes()
/*****************************************************************************
 *
 ****************************************************************************/
{
	boxf("Symbol.......%5d\n"
		 "Type_info....%5d\n"
		 "Code_buf.....%5d\n"
		 "Exp_frame....%5d\n"
		 "Func_frame...%5d\n"
		 "Poco_frame...%5d\n"
		 "Tstack.......%5d\n"
		 "Token........%5d\n"
		 "Poco_cb......%5d\n",
		 sizeof(Symbol),
		 sizeof(Type_info),
		 sizeof(Code_buf),
		 sizeof(Exp_frame) + HASH_SIZE * sizeof(Symbol*),
		 sizeof(Func_frame),
		 sizeof(Poco_frame),
		 sizeof(Tstack),
		 sizeof(Token),
		 sizeof(Poco_cb));
}
#endif /* DEBUG */

/*****************************************************************************
 * create a file, complain if it fails.
 ****************************************************************************/
static FILE* must_fmake(Poco_cb* pcb, char* fn)
{
	FILE* fp = fopen(fn, "w");

	if (fp == NULL) {
		fprintf(pcb->t.err_file, "Can't make %s\n", fn);
	}
	return fp;
}

/*****************************************************************************
 * create a file unless the name is NULL, die if it fails.
 ****************************************************************************/
static Errcode wanna_make(Poco_cb* pcb, FILE** f, char* fn)
{

	if (fn != NULL)
		if ((*f = must_fmake(pcb, fn)) == NULL)
			return (Err_create);
	return (Success);
}

/*****************************************************************************
 * close a file if it's open, and if it's not a device file.
 ****************************************************************************/
static void gentle_fclose(FILE* f)
{
	if (f != NULL && f != stdout && f != stderr)
		fclose(f);
}

/*****************************************************************************
 * free a poco_run_env, and the associate stack and data areas.
 ****************************************************************************/
static void po_pev_free_data(Poco_run_env* pev)
{
	if (pev != NULL) {
		pj_freez(&pev->data); /* this gets allocated from PJ, freez() */
	}
}

/*****************************************************************************
 * alloc poco_run_env, and stack and data areas. run data init code.
 * this routine should only be called after a successfull compile.
 ****************************************************************************/
static Errcode po_pev_alloc_data(Poco_run_env* pev)
{
	Errcode err;

	if (pev->data_size > 0) {
		if ((pev->data = pj_zalloc(pev->data_size)) == NULL) {
			err = Err_no_memory;
			goto ERR;
		}
	}
	if (pev->stack_size == 0)
		pev->stack_size = POCO_STACKSIZE_DEFAULT;

	err = po_run_ops(pev, pev->fff->code_pt, NULL);
	if (err < Success) {
		goto ERR;
	}

	return Success;

ERR:

	po_pev_free_data(pev);
	return err;
}

/*****************************************************************************
 * find a fuf for a function with a given name.
 * (linear search...slow, slow.  added first-char quick check to help a bit).
 ****************************************************************************/
static Func_frame* find_fuf(Poco_run_env* pev, char* name)
{
	Func_frame* f;

	f = pev->fff;
	while (f != NULL) {
		if (f->name[0] == *name)
			if (po_eqstrcmp(f->name, name) == 0)
				break;
		f = f->next;
	}
	return (f);
}

/*****************************************************************************
 * run a given function from a compiled poco program.
 * (at this point in development, the 'given function' had better be 'main'!)
 ****************************************************************************/
static Errcode run_file(Poco_run_env* pev, char* entry)
{
	Errcode err;
	Func_frame* f;

	if ((f = find_fuf(pev, entry)) == NULL)
		return (Err_no_main);
	if ((err = po_pev_alloc_data(pev)) >= Success)
		err = po_run_ops(pev, f->code_pt, NULL);
	po_pev_free_data(pev);
	return (err);
}

/*****************************************************************************
 * run a given function from a poco program after init'ing the libs.
 ****************************************************************************/
static Errcode lib_run_file(Poco_run_env* pev, char* entry)
{
	Errcode err = Success;

	if ((err = po_init_libs(pev->lib)) >= Success) {
		if ((err = po_init_libs(pev->loaded_libs)) >= Success) {
			err = run_file(pev, entry);
		}
		po_cleanup_libs(pev->loaded_libs);
	}
	po_cleanup_libs(pev->lib);
	return (err);
}

/*****************************************************************************
 * print a list of function names in a poco lib.
 ****************************************************************************/
static Errcode print_one_lib(FILE* f, Lib_proto* lib, int lib_size)
{
	while (--lib_size >= 0) {
		fprintf(f, "%s\n", lib->proto);
		lib += 1;
	}
	return (Success);
}

/*****************************************************************************
 * print the names of all functions in all poco libs.
 ****************************************************************************/
Errcode print_pocolib(char* filename, Poco_lib* lib)
{
	FILE* f;
	Errcode err = Success;

	if ((f = fopen(filename, "w")) != NULL) {
		while (lib != NULL) {
			fprintf(f, "/********* %s library ***********/\n", lib->name);
			if ((err = print_one_lib(f, lib->lib, lib->count)) < Success)
				break;
			lib = lib->next;
		}
	} else {
		err = Err_create;
	}
	fclose(f);
	return (err);
}

/*****************************************************************************
 * find a library or loaded poe module.
 * returns a pointer to its Lib_protos via *plibreturn and count of protos.
 *
 * this function now wears several hats.  if a specific library name is
 * passed, we search the linked list of loaded poe libraries for that name.
 * if the requested library name is "poco$builtin" or "poco$loaded", the
 * first library in the corresponding linked list is returned.	in this case,
 * subsequent calls with a NULL libname pointer will return the next library
 * in the list.
 *
 * if the requested library is not found, or when the end of a list of libs
 * is reached, the return value is Err_not_found, and the returned proto
 * pointer is set to NULL.
 *
 * NOTE!  This routine is part of the POE interface; a pointer to it is
 *		  provided to poe routines in the interface structure.	This routine
 *		  IS NOT intended for use by PJ internally -- it can only return
 *		  valid results while a poco program is currently executing!
 ****************************************************************************/
int po_findpoe(char* libname, Lib_proto** plibreturn)
{
	static Poco_lib* listnext = NULL;
	Poco_lib* ll;

	/*------------------------------------------------------------------------
	 * first make sure we don't get crashed by a naughty caller...
	 *----------------------------------------------------------------------*/

	if (NULL == plibreturn)
		return Err_null_ref; /* defensive programming */

	if (NULL == porunenv)
		goto ERROR_EXIT; /* "can't happen" */

	/*------------------------------------------------------------------------
	 * if the libname pointer is NULL, the caller wants the next library in
	 * the linked list, go return it...
	 *----------------------------------------------------------------------*/

	if (NULL == libname) {
		ll = listnext;
		goto RETURN_LIST_ITEM;
	}

	/*------------------------------------------------------------------------
	 * if the requested name is poco$builtin or poco$loaded, the caller wants
	 * to start a series of calls to walk the corresponding list...
	 *----------------------------------------------------------------------*/

	if (0 == stricmp(libname, "poco$builtin")) {
		ll = porunenv->lib;
		goto RETURN_LIST_ITEM;
	}

	if (0 == stricmp(libname, "poco$loaded")) {
		ll = porunenv->loaded_libs;
		goto RETURN_LIST_ITEM;
	}

	/*------------------------------------------------------------------------
	 * if we get to here, the caller wants a specific library, look for it...
	 *----------------------------------------------------------------------*/

	for (ll = porunenv->loaded_libs; ll != NULL; ll = ll->next)
		if (0 == stricmp(libname, ll->name))
			goto GOOD_EXIT;

ERROR_EXIT:

	*plibreturn = NULL;
	return Err_not_found;

RETURN_LIST_ITEM:

	if (NULL == ll)
		goto ERROR_EXIT; /* return not-found/end-of-list status */

	listnext = ll->next; /* reset list ptr for next call */

GOOD_EXIT:

	*plibreturn = ll->lib;
	return ll->count;
}

/*****************************************************************************
 *
 ****************************************************************************/
Poco_lib* po_open_library(Poco_cb* pcb, char* libname, char* id_string)
{
	Errcode err;
	Poco_lib* ll;

	if (0 == po_eqstrcmp("poco$builtin", libname)) {
		return pcb->builtin_lib;
	}
	else {
		if (pcb->t.verbose) {
		fprintf(stderr, "[poco library] #pragma poco library '%s' encountered\n", libname);
		}
		{
			const char* script_path = NULL;
			if (pcb && pcb->t.file_stack && pcb->t.file_stack->name) {
				script_path = pcb->t.file_stack->name;
			}
			if ((err = pj_load_pocorex(&ll, script_path, libname, id_string, pcb->t.verbose)) < Success) {
			errline(err, "can't load poco lib.");
			return (NULL);
			}
		}
		ll->next = pcb->run.loaded_libs;
		pcb->run.loaded_libs = ll;
		return (ll);
	}
}

/*****************************************************************************
 * get the next line of library proto data, return NULL if at end of lib(s).
 *	this is called from the preprocessor, which is now the single source of
 *	input lines (no more indirect calls to a 'get next line' routine).
 *
 * 01/10/92 - (Ian)
 *			A fix to a major bug:  The builtin libs are linked into a list
 *			by our parent, and when we hit the end of a given library, we
 *			use pl->next to start working the next library in the list.
 *			Loaded libraries aren't pre-linked, they're loaded and
 *			processed one at a time.  But, as we load them (in the routine
 *			above), we link them together so that they make a list we can
 *			walk at the end of the run to free the loaded libs.  The problem
 *			occurred when multiple libraries were loaded in a single poco
 *			program via #pragma.  Upon hitting the second #pragma, we'd
 *			load the lib, and link it to the prior lib we just got done
 *			processing, then we'd start processing protos from it.  When
 *			we hit the end of the protos, the logic below would follow the
 *			link into the prior library (which had already been processed)
 *			and we'd end up with 'function XXXX redefined' errors.  (Yuck!)
 *			To fix this, another test was added:  if pcb->run.loaded_libs
 *			is non-NULL, that means we've already done the builtin libs and
 *			we're now into loaded libs.  In this case, we don't try to
 *			follow the library links, we just return NULL to say we're done
 *			with the current library.  If the pcb->run.loaded_libs pointer
 *			*is* NULL, that means we haven't started on loaded libs yet,
 *			we're still doing the builtins, and so we follow the library
 *			links until we hit the end of the linked list of builtin libs.
 * 05/17/92 (Ian)
 *			We now 'sanity check' the prototypes by ensuring that we don't
 *			get NULL proto string pointers.  If we find one, we die; it
 *			means that POCOLIB.H has gotten out of sync with our builtin
 *			libs code (POCO*.C in the root), or we have a sick POE module.
 ****************************************************************************/
char* po_get_libproto_line(Poco_cb* pcb)
{
	File_stack* fs = pcb->t.file_stack;
	Poco_lib* pl   = fs->source.lib;
	Lib_proto* pp;

	if (fs->line_count >= pl->count) {
		if (NULL != pcb->run.loaded_libs || NULL == (pl = pl->next)) {
			pcb->libfunc = NULL;
			return NULL;
		}
		fs->source.lib = pl;
		fs->line_count = 0;
	}
	pp			 = &pl->lib[fs->line_count++];
	pcb->libfunc = pp->func;
	pcb->libcontract = pp->contract;

	if (pp->proto == NULL) {
		pcb->global_err = Err_poco_internal;
		po_say_fatal(pcb, "NULL prototype string pointer in library %s", pl->name);
		PO_CHECK_ABORT(pcb, NULL);
	}
	return pp->proto;
}

/*****************************************************************************
 * compile a poco program.	entry point to the compiler from PJ.
 ****************************************************************************/
Errcode compile_poco(void** ppexe,		 /* returns executable pexe on Success */
					 char* source_name,	 /* name of source file */
					 char* errors_name,	 /* error file or NULL for stderr */
					 char* dump_name,	 /* disassembly file or NULL for none */
					 Poco_lib* lib,		 /* for built-in function library */
					 char* err_file,	 /* file where error detected */
					 long* err_line,	 /* line where error detected */
					 int* err_char,		 /* character in line where err detected */
					 Names* include_dirs, /* include search path */
					 bool verbose		 /* enable verbose debug output */
)
{
	Poco_cb* pcb;
	Errcode err;
	Poco_run_env* pev;
	File_stack* fs;

	*ppexe = NULL;
	poco_last_error[0] = '\0';

	if (Success != (err = po_init_memory_management(&pcb)))
		return Err_no_memory; /* MUST return immediately if init fails. */

	pcb->compile_aborted = false;
	pcb->compile_err = Success;

	pcb->stack_bottom = ((char*)&ppexe) - MAX_STACK;

	pcb->t.err_file		= stdout;
	pcb->t.include_dirs = include_dirs;
	pcb->t.verbose		= verbose;

	pcb->libfunc	 = NULL;
	pcb->libcontract = NULL;
	pcb->builtin_lib = lib;

	/* Use an anonymous temp file for error output so we don't need
	 * a named temp file path that must be resolved for fopen(). */
	{
		FILE* etmp = tmpfile();
		if (etmp != NULL) {
			pcb->t.err_file = etmp;
		}
		/* else: falls back to stdout */
	}

	if (dump_name != NULL) {
		pcb->po_dump_file = fopen(dump_name, "w");
	}

	err = po_compile_file(pcb, source_name);
	if (err != Success || pcb->compile_aborted) {
		/* Read error output from the tmpfile into poco_last_error */
		if (pcb->t.err_file != NULL && pcb->t.err_file != stdout && pcb->t.err_file != stderr) {
			fflush(pcb->t.err_file);
			rewind(pcb->t.err_file);
			size_t n = fread(poco_last_error, 1, sizeof(poco_last_error) - 1, pcb->t.err_file);
			poco_last_error[n] = '\0';
		}
		err = Err_in_err_file; /* we reported it in error file */
		goto OUT;
	}

	pev = pj_zalloc((long)sizeof(*pev));
	if (pev == NULL) {
		err = Err_no_memory;
		goto OUT;
	}

	pcb->run.lib = lib;
	*pev		 = pcb->run;
	pev->compile_pcb = pcb;
	*ppexe		 = pev;


OUT:
	gentle_fclose(pcb->po_dump_file);
	gentle_fclose(pcb->t.err_file);

	/* A successfully compiled program retains pcb as its owner for the
	 * compiler/runtime allocation arena.  free_poco() releases that arena after
	 * execution; releasing it here leaves a dangling compile_pcb and causes a
	 * second free during program destruction. */
	if (err != Success) {
		/*
		 * let caller know where the error was
		 *	 if no files are open (eg, error was unexpected EOF) we say that.
		 *	 otherwise the error line number comes from the global error line
		 *	 number that is set by the error reporter in the parser or preprocessor.
		 *	 if the error happened in a library, and the library was included from
		 *	 a file, we report the filename and line number of the #pragma
		 *	 poco library statement that included the library.
		 */

		*err_char = 0;
		fs		  = pcb->t.file_stack;
		if (fs == NULL || fs->line_count == 0) {
			*err_line = 0;
			strcpy(err_file, "<no files open>");
		} else {
			if ((fs->flags & FSF_ISLIB) && fs->pred != NULL) {
				*err_line = fs->pred->line_count;
				strcpy(err_file, fs->pred->name);
			} else {
				*err_line = pcb->error_line_number;
				*err_char = pcb->error_char_number;
				strcpy(err_file, fs->name);
			}
		}

		/*
		 * close all files.
		 *	 changed this to a call to po_free_pp() - it contains the loop
		 *	 to close all the files.
		 */
		po_free_pp(pcb);

		/*
		 * free all memory allocated since the compile started...
		 */
		po_free_all_memory(pcb);

		/*
		 * free any libraries from #pragma poco library "xxx"
		 */
		pj_free_pocorexes(&pcb->run.loaded_libs);

	} /* end of post-error cleanup handling */

	/* kiki addition: libffi integration */
	if (err == Success) {
		err = po_ffi_build_structures(pev);
		if (err != Success)
			free_poco(ppexe);
	}

	return err;
}

/*****************************************************************************
 * run a poco program compiled earlier using compile_poco. exe entry from PJ.
 ****************************************************************************/
Errcode run_poco(void** ppexe,
				 char* trace_file,
				 bool (*check_abort)(void*),
				 void* check_abort_data,
				 long* err_line)
{

	Errcode run_err;

	if ((porunenv = *ppexe) == NULL)
		return (Err_not_found);

	porunenv->enable_debug_trace  = true;
	porunenv->check_abort		  = check_abort;
	porunenv->check_abort_data	  = check_abort_data;
	porunenv->trace_file		  = trace_file;
	porunenv->err_line			  = err_line;

	po_ffi_variadic_types_reset(&porunenv->variadic);

	run_err = lib_run_file(porunenv, "main");
	porunenv = NULL;

	return run_err;
}

/*****************************************************************************
 * free runtime resources used by a poco program.
 ****************************************************************************/
void free_poco(void** ppexe)
{
	Poco_run_env* pp;
	Poco_cb* compile_pcb = NULL;

	if ((pp = *ppexe) != NULL) {
		if (porunenv == pp)
			porunenv = NULL;
		compile_pcb = (Poco_cb*)pp->compile_pcb;
		po_pev_free_data(pp);
		po_free_run_env(pp);
		pj_free_pocorexes(&pp->loaded_libs);
		pj_free(pp); /* this is allocated from PJ, free back to PJ. */
		*ppexe = NULL;
	}
	if (compile_pcb != NULL)
		po_free_all_memory(compile_pcb);
}

/*****************************************************************************
 * return pointer to name of function associated with a given fuf.
 ****************************************************************************/
char* po_fuf_name(void* fuf)
{
	return (((Func_frame*)fuf)->name);
}

/*****************************************************************************
 * return pointer to code buffer associated with a given fuf.
 ****************************************************************************/
void* po_fuf_code(void* fuf)
{
	return (((Func_frame*)fuf)->code_pt);
}

/*****************************************************************************
 * used by PJ when run as 'PJ filename.POC' and an error occurs.
 ****************************************************************************/
Errcode po_file_to_stdout(char* name)
{
	FILE* f;
	int c;

	f = fopen(name, "r");
	if (f == NULL) {
		return Err_create;
	}
	while ((c = fgetc(f)) != EOF) {
		fputc(c, stdout);
	}
	fputc('\n', stdout);
	fclose(f);
	return Success;
}
