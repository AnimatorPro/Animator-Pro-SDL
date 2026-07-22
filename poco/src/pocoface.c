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
#include "activation.h"
#include "poco_hash.h"
#include "program_internal.h"
#include "standard_library.h"

#include <ctype.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

/*****************************************************************************
 * some global vars...
 ****************************************************************************/


int po_version_number = VRSN_NUM; /* Global version number for PJ's use.    */

static Errcode po_pev_alloc_data(PocoActivation* activation);
static const Func_frame* find_fuf(const PocoActivation* activation, const char* name);
static Errcode po_activation_run_entry_values(PocoActivation* activation, const char* entry,
											  const PocoCallbackValue* values, size_t value_count,
											  Pt_num* result);

#ifdef _WIN32
typedef CRITICAL_SECTION Poco_diagnostic_lock;
#else
typedef pthread_mutex_t Poco_diagnostic_lock;
#endif

static void* poco_diagnostic_lock_create(void)
{
	Poco_diagnostic_lock* lock = malloc(sizeof(*lock));

	if (lock == NULL) {
		return NULL;
	}
#ifdef _WIN32
	InitializeCriticalSection(lock);
#else
	if (pthread_mutex_init(lock, NULL) != 0) {
		free(lock);
		return NULL;
	}
#endif
	return lock;
}

static void poco_diagnostic_lock_destroy(void* opaque_lock)
{
	Poco_diagnostic_lock* lock = opaque_lock;

	if (lock == NULL) {
		return;
	}
#ifdef _WIN32
	DeleteCriticalSection(lock);
#else
	pthread_mutex_destroy(lock);
#endif
	free(lock);
}

static void poco_diagnostic_lock(PocoVm* vm)
{
	Poco_diagnostic_lock* lock = vm != NULL ? vm->diagnostic_lock : NULL;

	if (lock == NULL) {
		return;
	}
#ifdef _WIN32
	EnterCriticalSection(lock);
#else
	pthread_mutex_lock(lock);
#endif
}

static void poco_diagnostic_unlock(PocoVm* vm)
{
	Poco_diagnostic_lock* lock = vm != NULL ? vm->diagnostic_lock : NULL;

	if (lock == NULL) {
		return;
	}
#ifdef _WIN32
	LeaveCriticalSection(lock);
#else
	pthread_mutex_unlock(lock);
#endif
}

/*
 * Copy a run's private diagnostic message into the shared program VM so a host
 * that inspects the program after the activation returns sees the last error.
 */
static void poco_activation_publish_last_error(PocoActivation* activation)
{
	PocoVm* shared;

	if (activation == NULL || activation->vm == NULL || activation->program == NULL) {
		return;
	}
	shared = activation->program->vm;
	if (shared == NULL || activation->vm->last_error[0] == '\0') {
		return;
	}
	poco_diagnostic_lock(shared);
	memcpy(shared->last_error, activation->vm->last_error, sizeof(shared->last_error));
	poco_diagnostic_unlock(shared);
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

typedef struct PocoCallPointerRegistration {
	void* pointer;
	size_t byte_count;
	uint32_t permissions;
} PocoCallPointerRegistration;

struct PocoCall {
	PocoActivation* activation;
	const Func_frame* function;
	PocoCallbackValue* values;
	PocoCallPointerRegistration* pointer_registrations;
	size_t value_count;
	size_t value_capacity;
	size_t registered_pointer_count;
	size_t registered_pointer_capacity;
	int invoked;
};

Errcode compile_poco_with_vm(PocoVm* vm, void** ppexe, char* source_name, char* errors_name,
							 char* dump_name, Poco_lib* lib, char* err_file, long* err_line,
							 int* err_char, Names* include_dirs, bool verbose);

void poco_set_error(PocoVm* vm, const char* fmt, ...)
{
	va_list args;

	if (vm == NULL) {
		return;
	}
	poco_diagnostic_lock(vm);
	va_start(args, fmt);
	vsnprintf(vm->last_error, sizeof(vm->last_error), fmt, args);
	va_end(args);
	poco_diagnostic_unlock(vm);
}

const char* poco_get_last_error(PocoVm* vm)
{
	return vm != NULL ? vm->last_error : "";
}

Errcode* poco_vm_builtin_error(PocoVm* vm)
{
	return vm != NULL && vm->activation != NULL ? &vm->activation->builtin_error : NULL;
}

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

static char* poco_api_copy_string(const char* value)
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

static char* poco_api_source_directory(const char* source_name)
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
	poco_diagnostic_lock_destroy(vm->diagnostic_lock);
	free(vm);
}

static void poco_api_report(PocoVm* vm, PocoStatus status, const char* source_name, long line,
							int column, const char* message)
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
	vm->diagnostic_lock = poco_diagnostic_lock_create();
	if (vm->diagnostic_lock == NULL) {
		free(vm);
		return POCO_STATUS_OUT_OF_MEMORY;
	}
	vm->pointer_registry = poco_pointer_registry_create();
	if (vm->pointer_registry == NULL) {
		poco_diagnostic_lock_destroy(vm->diagnostic_lock);
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
		entry->name = poco_api_copy_string(paths[index]);
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
	entry->name = poco_api_copy_string(path);
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
	registered->library.identity = poco_api_copy_string(library->identity);
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
				poco_api_copy_string(library->bindings[index].prototype);
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

static PocoStatus poco_vm_compile_sources(PocoVm* vm, const char* const* source_names,
										  const char* const* physical_source_paths,
										  const char* const* sources, const size_t* source_lengths,
										  Names* const* include_dirs,
										  const size_t* const* use_indices,
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
			poco_api_report(vm, POCO_STATUS_PARAMETER_RANGE, source_names[source_index], 0, 0,
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
		program->sources[source_index].name = poco_api_copy_string(stored_name);
		program->sources[source_index].path =
			physical_source_paths[source_index] != NULL
				? poco_api_copy_string(physical_source_paths[source_index])
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
		poco_api_report(vm, public_status, error_source, error_line, error_column,
						poco_get_last_error(vm));
		if (program->executable != NULL) {
			free_poco(&program->executable);
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
			free_poco(&program->executable);
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

	return poco_vm_compile_sources(vm, source_names, physical_source_paths, sources, source_lengths,
								   source_include_dirs, NULL, NULL, 1, out_program);
}

PocoStatus poco_vm_compile_buffer(PocoVm* vm, const char* source_name, const char* source,
								  size_t source_length, PocoProgram** out_program)
{
	return poco_vm_compile_source(vm, source_name, NULL, source, source_length,
								  vm != NULL ? vm->include_dirs : NULL, out_program);
}

static PocoStatus poco_api_read_source_file(PocoVm* vm, const char* source_name, char** out_source,
											size_t* out_source_length)
{
	FILE* source_file;
	char* source;
	long file_length;
	size_t source_length;
	PocoStatus status;

	if (vm == NULL || source_name == NULL || out_source == NULL || out_source_length == NULL) {
		return POCO_STATUS_NULL_REFERENCE;
	}
	*out_source = NULL;
	*out_source_length = 0;

	source_file = fopen(source_name, "rb");
	if (source_file == NULL) {
		status = POCO_STATUS_NO_FILE;
		poco_set_error(vm, "Cannot open source file '%s'", source_name);
		poco_api_report(vm, status, source_name, 0, 0, poco_get_last_error(vm));
		return status;
	}
	if (fseek(source_file, 0, SEEK_END) != 0 || (file_length = ftell(source_file)) < 0 ||
		fseek(source_file, 0, SEEK_SET) != 0) {
		fclose(source_file);
		status = POCO_STATUS_SEEK_FAILED;
		poco_set_error(vm, "Cannot determine source file size for '%s'", source_name);
		poco_api_report(vm, status, source_name, 0, 0, poco_get_last_error(vm));
		return status;
	}
	source_length = (size_t)file_length;
	if ((long)source_length != file_length) {
		fclose(source_file);
		status = POCO_STATUS_OVERFLOW;
		poco_set_error(vm, "Source file '%s' is too large", source_name);
		poco_api_report(vm, status, source_name, 0, 0, poco_get_last_error(vm));
		return status;
	}
	source = malloc(source_length > 0 ? source_length : 1);
	if (source == NULL) {
		fclose(source_file);
		return POCO_STATUS_OUT_OF_MEMORY;
	}
	if (source_length > 0 && fread(source, 1, source_length, source_file) != source_length) {
		free(source);
		fclose(source_file);
		status = POCO_STATUS_READ_FAILED;
		poco_set_error(vm, "Cannot read source file '%s'", source_name);
		poco_api_report(vm, status, source_name, 0, 0, poco_get_last_error(vm));
		return status;
	}
	fclose(source_file);
	*out_source = source;
	*out_source_length = source_length;
	return POCO_STATUS_OK;
}

typedef struct Poco_use_source {
	char* path;
	char* source_name;
	char* source;
	size_t source_length;
	size_t* uses;
	size_t use_count;
	size_t use_capacity;
	size_t output_index;
	int visit_state;
} Poco_use_source;

typedef struct Poco_use_graph {
	PocoVm* vm;
	Poco_use_source* sources;
	size_t source_count;
	size_t source_capacity;
	size_t* order;
	size_t order_count;
	size_t order_capacity;
	PocoStatus status;
} Poco_use_graph;

typedef struct Poco_use_scan_context {
	Poco_use_graph* graph;
	size_t source_index;
} Poco_use_scan_context;

static char* poco_api_canonical_source_path(const char* path)
{
	char resolved[PATH_SIZE];

#ifdef _WIN32
	if (_fullpath(resolved, path, sizeof(resolved)) == NULL) {
		return NULL;
	}
#else
	if (realpath(path, resolved) == NULL) {
		return NULL;
	}
#endif
	return poco_api_copy_string(resolved);
}

static char* poco_api_resolve_used_source(Poco_use_graph* graph, const char* using_path,
										  const char* requested)
{
	char candidate[PATH_SIZE];
	char* directory;
	char* canonical;
	Names* include_dir;
	size_t requested_length = strlen(requested);
	bool absolute =
		requested[0] == '/'
#ifdef _WIN32
		|| (requested_length > 2 && isalpha((unsigned char)requested[0]) && requested[1] == ':')
#endif
		;

	if (absolute) {
		return poco_api_canonical_source_path(requested);
	}
	directory = poco_api_source_directory(using_path);
	if (directory == NULL) {
		graph->status = POCO_STATUS_OUT_OF_MEMORY;
		return NULL;
	}
	if (strlen(directory) + requested_length + 1 <= sizeof(candidate)) {
		snprintf(candidate, sizeof(candidate), "%s%s", directory, requested);
		canonical = poco_api_canonical_source_path(candidate);
		if (canonical != NULL) {
			free(directory);
			return canonical;
		}
	}
	free(directory);
	for (include_dir = graph->vm->include_dirs; include_dir != NULL;
		 include_dir = include_dir->next) {
		if (strlen(include_dir->name) + requested_length + 1 > sizeof(candidate)) {
			continue;
		}
		snprintf(candidate, sizeof(candidate), "%s%s", include_dir->name, requested);
		canonical = poco_api_canonical_source_path(candidate);
		if (canonical != NULL) {
			return canonical;
		}
	}
	return NULL;
}

static size_t poco_api_use_graph_find(const Poco_use_graph* graph, const char* path)
{
	size_t index;

	for (index = 0; index < graph->source_count; ++index) {
		if (strcmp(graph->sources[index].path, path) == 0) {
			return index;
		}
	}
	return SIZE_MAX;
}

static bool poco_api_use_graph_append(size_t** values, size_t* count, size_t* capacity,
									  size_t value)
{
	size_t* grown;

	if (*count == *capacity) {
		size_t new_capacity = *capacity == 0 ? 4 : *capacity * 2;
		grown = realloc(*values, new_capacity * sizeof(*grown));
		if (grown == NULL) {
			return false;
		}
		*values = grown;
		*capacity = new_capacity;
	}
	(*values)[(*count)++] = value;
	return true;
}

static bool poco_api_visit_used_source(Poco_use_graph* graph, const char* canonical_path,
									   size_t* out_index);

static bool poco_api_collect_use(void* opaque, const char* requested, size_t line_number)
{
	Poco_use_scan_context* context = opaque;
	Poco_use_graph* graph = context->graph;
	const char* using_path = graph->sources[context->source_index].path;
	char* canonical = poco_api_resolve_used_source(graph, using_path, requested);
	size_t used_index;
	size_t index;

	if (canonical == NULL) {
		if (graph->status == POCO_STATUS_OK) {
			graph->status = POCO_STATUS_REPORTED;
			poco_set_error(graph->vm, "Cannot resolve used source '%s' from %s:%zu", requested,
						   using_path, line_number);
		}
		return false;
	}
	if (!poco_api_visit_used_source(graph, canonical, &used_index)) {
		free(canonical);
		return false;
	}
	free(canonical);
	for (index = 0; index < graph->sources[context->source_index].use_count; ++index) {
		if (graph->sources[context->source_index].uses[index] == used_index) {
			return true;
		}
	}
	if (!poco_api_use_graph_append(&graph->sources[context->source_index].uses,
								   &graph->sources[context->source_index].use_count,
								   &graph->sources[context->source_index].use_capacity,
								   used_index)) {
		graph->status = POCO_STATUS_OUT_OF_MEMORY;
		return false;
	}
	return true;
}

static bool poco_api_visit_used_source(Poco_use_graph* graph, const char* canonical_path,
									   size_t* out_index)
{
	size_t index = poco_api_use_graph_find(graph, canonical_path);
	Poco_use_source* source;
	Poco_use_scan_context scan_context;
	char scan_error[256] = "";

	if (index != SIZE_MAX) {
		if (graph->sources[index].visit_state == 1) {
			graph->status = POCO_STATUS_REPORTED;
			poco_set_error(graph->vm, "#pragma poco use cycle detected at '%s'", canonical_path);
			return false;
		}
		*out_index = index;
		return true;
	}
	if (graph->source_count == graph->source_capacity) {
		size_t new_capacity = graph->source_capacity == 0 ? 8 : graph->source_capacity * 2;
		Poco_use_source* grown = realloc(graph->sources, new_capacity * sizeof(*graph->sources));
		if (grown == NULL) {
			graph->status = POCO_STATUS_OUT_OF_MEMORY;
			return false;
		}
		memset(grown + graph->source_capacity, 0,
			   (new_capacity - graph->source_capacity) * sizeof(*grown));
		graph->sources = grown;
		graph->source_capacity = new_capacity;
	}
	index = graph->source_count++;
	source = &graph->sources[index];
	source->path = poco_api_copy_string(canonical_path);
	source->source_name = poco_api_copy_string(canonical_path);
	if (source->path == NULL || source->source_name == NULL) {
		graph->status = POCO_STATUS_OUT_OF_MEMORY;
		return false;
	}
	graph->status = poco_api_read_source_file(graph->vm, canonical_path, &source->source,
											  &source->source_length);
	if (graph->status != POCO_STATUS_OK) {
		return false;
	}
	source->visit_state = 1;
	scan_context.graph = graph;
	scan_context.source_index = index;
	if (!po_pp_scan_uses(source->source, source->source_length, poco_api_collect_use, &scan_context,
						 scan_error, sizeof(scan_error))) {
		if (graph->status == POCO_STATUS_OK) {
			graph->status = POCO_STATUS_REPORTED;
			poco_set_error(graph->vm, "%s in %s", scan_error, canonical_path);
		}
		return false;
	}
	/* Recursive discovery may have grown the node array. */
	source = &graph->sources[index];
	source->visit_state = 2;
	if (!poco_api_use_graph_append(&graph->order, &graph->order_count, &graph->order_capacity,
								   index)) {
		graph->status = POCO_STATUS_OUT_OF_MEMORY;
		return false;
	}
	*out_index = index;
	return true;
}

static void poco_api_free_use_graph(Poco_use_graph* graph)
{
	size_t index;

	for (index = 0; index < graph->source_count; ++index) {
		free(graph->sources[index].uses);
		free(graph->sources[index].source);
		free(graph->sources[index].source_name);
		free(graph->sources[index].path);
	}
	free(graph->sources);
	free(graph->order);
}

PocoStatus poco_vm_compile_files(PocoVm* vm, const char* const* source_names, size_t source_count,
								 PocoProgram** out_program)
{
	Poco_use_graph graph = {0};
	const char** expanded_names = NULL;
	const char** physical_source_paths = NULL;
	const char** sources = NULL;
	size_t* source_lengths = NULL;
	Names* source_directory_entries = NULL;
	Names** include_dirs = NULL;
	size_t** use_indices = NULL;
	size_t* use_counts = NULL;
	size_t expanded_count = 0;
	size_t primary_node_index = SIZE_MAX;
	size_t source_index;
	PocoStatus status = POCO_STATUS_OK;

	if (vm == NULL || source_names == NULL || out_program == NULL) {
		return POCO_STATUS_NULL_REFERENCE;
	}
	*out_program = NULL;
	if (vm->destroy_requested || source_count == 0) {
		return POCO_STATUS_PARAMETER_RANGE;
	}
	graph.vm = vm;
	graph.status = POCO_STATUS_OK;
	for (source_index = 0; source_index < source_count; ++source_index) {
		char* canonical;
		size_t node_index;

		if (source_names[source_index] == NULL) {
			status = POCO_STATUS_NULL_REFERENCE;
			goto OUT;
		}
		canonical = poco_api_canonical_source_path(source_names[source_index]);
		if (canonical == NULL) {
			status = POCO_STATUS_NO_FILE;
			poco_set_error(vm, "Cannot open source file '%s'", source_names[source_index]);
			goto OUT;
		}
		if (!poco_api_visit_used_source(&graph, canonical, &node_index)) {
			free(canonical);
			status = graph.status;
			goto OUT;
		}
		if (source_index == 0) {
			primary_node_index = node_index;
		}
		if (strcmp(graph.sources[node_index].source_name, source_names[source_index]) != 0) {
			char* explicit_name = poco_api_copy_string(source_names[source_index]);
			if (explicit_name == NULL) {
				free(canonical);
				status = POCO_STATUS_OUT_OF_MEMORY;
				goto OUT;
			}
			free(graph.sources[node_index].source_name);
			graph.sources[node_index].source_name = explicit_name;
		}
		free(canonical);
	}
	expanded_count = graph.order_count;
	expanded_names = calloc(expanded_count, sizeof(*expanded_names));
	physical_source_paths = calloc(expanded_count, sizeof(*physical_source_paths));
	sources = calloc(expanded_count, sizeof(*sources));
	source_lengths = calloc(expanded_count, sizeof(*source_lengths));
	source_directory_entries = calloc(expanded_count, sizeof(*source_directory_entries));
	include_dirs = calloc(expanded_count, sizeof(*include_dirs));
	use_indices = calloc(expanded_count, sizeof(*use_indices));
	use_counts = calloc(expanded_count, sizeof(*use_counts));
	if (expanded_names == NULL || physical_source_paths == NULL || sources == NULL ||
		source_lengths == NULL || source_directory_entries == NULL || include_dirs == NULL ||
		use_indices == NULL || use_counts == NULL) {
		status = POCO_STATUS_OUT_OF_MEMORY;
		goto OUT;
	}

	for (source_index = 0; source_index < expanded_count; ++source_index) {
		graph.sources[graph.order[source_index]].output_index = source_index;
	}
	for (source_index = 0; source_index < expanded_count; ++source_index) {
		Poco_use_source* graph_source = &graph.sources[graph.order[source_index]];
		char* source_directory;
		size_t use_index;

		expanded_names[source_index] = graph_source->source_name;
		physical_source_paths[source_index] = graph_source->path;
		sources[source_index] = graph_source->source;
		source_lengths[source_index] = graph_source->source_length;
		use_counts[source_index] = graph_source->use_count;
		if (graph_source->use_count != 0) {
			use_indices[source_index] = malloc(graph_source->use_count * sizeof(size_t));
			if (use_indices[source_index] == NULL) {
				status = POCO_STATUS_OUT_OF_MEMORY;
				goto OUT;
			}
			for (use_index = 0; use_index < graph_source->use_count; ++use_index) {
				use_indices[source_index][use_index] =
					graph.sources[graph_source->uses[use_index]].output_index;
			}
		}

		/* Each file gets its own leading include directory; no unit inherits the
		 * filesystem scope of a neighbor in the ordered compile. */
		source_directory = poco_api_source_directory(graph_source->source_name);
		if (source_directory == NULL) {
			status = POCO_STATUS_OUT_OF_MEMORY;
			goto OUT;
		}
		source_directory_entries[source_index].next = vm->include_dirs;
		source_directory_entries[source_index].name = source_directory;
		include_dirs[source_index] = &source_directory_entries[source_index];
	}
	status = poco_vm_compile_sources(
		vm, expanded_names, physical_source_paths, sources, source_lengths, include_dirs,
		(const size_t* const*)use_indices, use_counts, expanded_count, out_program);
	if (status == POCO_STATUS_OK && primary_node_index != SIZE_MAX &&
		graph.sources[primary_node_index].output_index != 0) {
		const char* primary_name = graph.sources[primary_node_index].source_name;
		const char* separator = strrchr(primary_name, '/');
		const char* alternate_separator = strrchr(primary_name, '\\');
		const char* basename;
		char* stored_name;
		char* stored_path;

		if (alternate_separator != NULL && (separator == NULL || alternate_separator > separator)) {
			separator = alternate_separator;
		}
		basename = separator != NULL && separator[1] != '\0' ? separator + 1 : primary_name;
		stored_name = poco_api_copy_string(basename);
		stored_path = poco_api_copy_string(primary_name);
		if (stored_name == NULL || stored_path == NULL) {
			free(stored_name);
			free(stored_path);
			poco_program_destroy(*out_program);
			*out_program = NULL;
			status = POCO_STATUS_OUT_OF_MEMORY;
			goto OUT;
		}
		free((*out_program)->source_name);
		free((*out_program)->source_path);
		(*out_program)->source_name = stored_name;
		(*out_program)->source_path = stored_path;
		(*out_program)->primary_source_index = graph.sources[primary_node_index].output_index;
		po_blake3_hash(graph.sources[primary_node_index].source,
					   graph.sources[primary_node_index].source_length,
					   (*out_program)->source_hash);
	}

OUT:
	for (source_index = 0; source_index < expanded_count; ++source_index) {
		free(source_directory_entries != NULL ? source_directory_entries[source_index].name : NULL);
		free(use_indices != NULL ? use_indices[source_index] : NULL);
	}
	free(use_counts);
	free(use_indices);
	free(include_dirs);
	free(source_directory_entries);
	free(source_lengths);
	free(sources);
	free(physical_source_paths);
	free(expanded_names);
	poco_api_free_use_graph(&graph);
	return status;
}

PocoStatus poco_vm_compile_file(PocoVm* vm, const char* source_name, PocoProgram** out_program)
{
	const char* source_names[] = {source_name};

	if (source_name == NULL) {
		return POCO_STATUS_NULL_REFERENCE;
	}
	return poco_vm_compile_files(vm, source_names, 1, out_program);
}

typedef struct Poco_api_cancel_context {
	PocoCancelCallback callback;
	void* user_data;
} Poco_api_cancel_context;

static bool poco_api_cancel(void* context)
{
	Poco_api_cancel_context* cancel_context = context;

	return cancel_context != NULL && cancel_context->callback != NULL &&
		   cancel_context->callback(cancel_context->user_data) != 0;
}

static const Symbol* poco_activation_find_global(const PocoActivation* activation, const char* name)
{
	const Func_frame* frame;
	const Symbol* symbol;

	if (activation == NULL || activation->code == NULL || activation->code->functions == NULL ||
		name == NULL) {
		return NULL;
	}
	for (frame = activation->code->functions; frame != NULL; frame = frame->next) {
		if (frame->got_code) {
			continue;
		}
		for (symbol = frame->parameters; symbol != NULL; symbol = symbol->link) {
			if (symbol->tok_type == PTOK_VAR && symbol->storage_scope == SCOPE_GLOBAL &&
				(symbol->ti->flags & TFL_EXTERN) == 0 && strcmp(symbol->name, name) == 0) {
				return symbol;
			}
		}
	}
	return NULL;
}

static void* poco_activation_global_storage(PocoActivation* activation, const Symbol* symbol,
											size_t value_size)
{
	long offset;

	if (activation == NULL || symbol == NULL || activation->data == NULL ||
		activation->data_size < 0 || symbol->symval.doff > 0 ||
		(long)symbol->symval.doff < -activation->data_size) {
		return NULL;
	}
	offset = activation->data_size + (long)symbol->symval.doff;
	if (value_size > (size_t)(activation->data_size - offset)) {
		return NULL;
	}
	return activation->data + offset;
}

static int poco_popot_is_bounded(Popot value)
{
	uintptr_t current;
	uintptr_t minimum;
	uintptr_t maximum;

	if (value.pt == NULL) {
		return 1;
	}
	if (value.min == NULL || value.max == NULL) {
		return 0;
	}
	current = (uintptr_t)value.pt;
	minimum = (uintptr_t)value.min;
	maximum = (uintptr_t)value.max;
	return minimum <= current && current <= maximum;
}

static PocoCallbackValue poco_invalid_callback_value(void)
{
	PocoCallbackValue value = {POCO_CALLBACK_VALUE_INVALID, {0}};

	return value;
}

PocoStatus poco_activation_acquire(PocoProgram* program, PocoActivation** out_activation)
{
	Errcode status;
	PocoVm* vm;

	if (program == NULL || out_activation == NULL) {
		return POCO_STATUS_NULL_REFERENCE;
	}
	*out_activation = NULL;
	vm = program->vm;
	if (vm == NULL || vm->destroy_requested) {
		return POCO_STATUS_PARAMETER_RANGE;
	}
	status = poco_activation_create(program, &program->code, vm, out_activation);
	return (PocoStatus)status;
}

PocoStatus poco_activation_reset(PocoActivation* activation)
{
	PocoVm* vm;

	if (activation == NULL) {
		return POCO_STATUS_NULL_REFERENCE;
	}
	if (activation->program == NULL) {
		return POCO_STATUS_PARAMETER_RANGE;
	}
	vm = activation->program->vm;
	if (vm == NULL || vm->destroy_requested) {
		return POCO_STATUS_PARAMETER_RANGE;
	}
	return (PocoStatus)poco_activation_reset_state(activation);
}

PocoStatus poco_activation_init(PocoActivation* activation)
{
	Errcode init_status;
	long error_line = 0;
	PocoVm* vm;

	if (activation == NULL) {
		return POCO_STATUS_NULL_REFERENCE;
	}
	if (activation->program == NULL || activation->program->vm == NULL) {
		return POCO_STATUS_PARAMETER_RANGE;
	}
	vm = activation->program->vm;
	if (vm->destroy_requested || activation->needs_reset) {
		return POCO_STATUS_PARAMETER_RANGE;
	}
	activation->err_line = &error_line;
	init_status = po_pev_alloc_data(activation);
	activation->err_line = NULL;
	poco_activation_publish_last_error(activation);
	if (init_status != Success) {
		poco_api_report(vm, (PocoStatus)init_status, NULL, error_line, 0,
						"Poco global initialization failed");
	}
	return (PocoStatus)init_status;
}

PocoStatus poco_activation_set_global(PocoActivation* activation, const char* name,
									  PocoCallbackValue value)
{
	const Symbol* symbol;
	const Type_info* type;
	void* storage;

	if (activation == NULL || name == NULL) {
		return POCO_STATUS_NULL_REFERENCE;
	}
	if (activation->program == NULL || activation->program->vm == NULL) {
		return POCO_STATUS_PARAMETER_RANGE;
	}
	symbol = poco_activation_find_global(activation, name);
	if (symbol == NULL) {
		return POCO_STATUS_NOT_FOUND;
	}
	type = symbol->ti;
	if (type == NULL || po_is_array((Type_info*)type)) {
		return POCO_STATUS_PARAMETER_RANGE;
	}
	if (po_is_pointer((Type_info*)type)) {
		Popot converted;

		if (value.kind != POCO_CALLBACK_VALUE_POPOT ||
			!poco_popot_is_bounded(value.value.popot_value)) {
			return POCO_STATUS_PARAMETER_RANGE;
		}
		storage = poco_activation_global_storage(activation, symbol, sizeof(Popot));
		if (storage == NULL) {
			return POCO_STATUS_PARAMETER_RANGE;
		}
		converted = value.value.popot_value;
		if (converted.pt == NULL) {
			converted.min = NULL;
			converted.max = NULL;
		}
		memcpy(storage, &converted, sizeof(converted));
		return POCO_STATUS_OK;
	}
	if (type->comp_count != 1) {
		return POCO_STATUS_PARAMETER_RANGE;
	}
	switch (type->comp[0]) {
		case TYPE_CHAR: {
			char converted;
			if (value.kind != POCO_CALLBACK_VALUE_INT || value.value.int_value < CHAR_MIN ||
				value.value.int_value > CHAR_MAX) {
				return POCO_STATUS_PARAMETER_RANGE;
			}
			converted = (char)value.value.int_value;
			storage = poco_activation_global_storage(activation, symbol, sizeof(converted));
			if (storage != NULL) {
				memcpy(storage, &converted, sizeof(converted));
			}
			break;
		}
		case TYPE_SHORT: {
			short converted;
			if (value.kind != POCO_CALLBACK_VALUE_INT || value.value.int_value < SHRT_MIN ||
				value.value.int_value > SHRT_MAX) {
				return POCO_STATUS_PARAMETER_RANGE;
			}
			converted = (short)value.value.int_value;
			storage = poco_activation_global_storage(activation, symbol, sizeof(converted));
			if (storage != NULL) {
				memcpy(storage, &converted, sizeof(converted));
			}
			break;
		}
		case TYPE_INT:
			if (value.kind != POCO_CALLBACK_VALUE_INT) {
				return POCO_STATUS_PARAMETER_RANGE;
			}
			storage =
				poco_activation_global_storage(activation, symbol, sizeof(value.value.int_value));
			if (storage != NULL) {
				memcpy(storage, &value.value.int_value, sizeof(value.value.int_value));
			}
			break;
		case TYPE_LONG:
			if (value.kind != POCO_CALLBACK_VALUE_LONG) {
				return POCO_STATUS_PARAMETER_RANGE;
			}
			storage =
				poco_activation_global_storage(activation, symbol, sizeof(value.value.long_value));
			if (storage != NULL) {
				memcpy(storage, &value.value.long_value, sizeof(value.value.long_value));
			}
			break;
		case TYPE_FLOAT: {
			float converted;
			if (value.kind != POCO_CALLBACK_VALUE_DOUBLE || value.value.double_value < -FLT_MAX ||
				value.value.double_value > FLT_MAX) {
				return POCO_STATUS_PARAMETER_RANGE;
			}
			converted = (float)value.value.double_value;
			storage = poco_activation_global_storage(activation, symbol, sizeof(converted));
			if (storage != NULL) {
				memcpy(storage, &converted, sizeof(converted));
			}
			break;
		}
		case TYPE_DOUBLE:
			if (value.kind != POCO_CALLBACK_VALUE_DOUBLE) {
				return POCO_STATUS_PARAMETER_RANGE;
			}
			storage = poco_activation_global_storage(activation, symbol,
													 sizeof(value.value.double_value));
			if (storage != NULL) {
				memcpy(storage, &value.value.double_value, sizeof(value.value.double_value));
			}
			break;
		default:
			return POCO_STATUS_PARAMETER_RANGE;
	}
	return storage != NULL ? POCO_STATUS_OK : POCO_STATUS_PARAMETER_RANGE;
}

PocoCallbackValue poco_activation_get_global(PocoActivation* activation, const char* name)
{
	PocoCallbackValue value = poco_invalid_callback_value();
	const Symbol* symbol;
	const Type_info* type;
	const void* storage;

	if (activation == NULL || name == NULL || activation->program == NULL ||
		activation->program->vm == NULL) {
		return value;
	}
	symbol = poco_activation_find_global(activation, name);
	if (symbol == NULL || symbol->ti == NULL || po_is_array(symbol->ti)) {
		return value;
	}
	type = symbol->ti;
	if (po_is_pointer((Type_info*)type)) {
		storage =
			poco_activation_global_storage(activation, symbol, sizeof(value.value.popot_value));
		if (storage != NULL) {
			memcpy(&value.value.popot_value, storage, sizeof(value.value.popot_value));
			if (poco_popot_is_bounded(value.value.popot_value)) {
				if (value.value.popot_value.pt == NULL) {
					value.value.popot_value.min = NULL;
					value.value.popot_value.max = NULL;
				}
				value.kind = POCO_CALLBACK_VALUE_POPOT;
			} else {
				value = poco_invalid_callback_value();
			}
		}
		return value;
	}
	if (type->comp_count != 1) {
		return value;
	}
	switch (type->comp[0]) {
		case TYPE_CHAR: {
			char stored;
			storage = poco_activation_global_storage(activation, symbol, sizeof(stored));
			if (storage != NULL) {
				memcpy(&stored, storage, sizeof(stored));
				value.kind = POCO_CALLBACK_VALUE_INT;
				value.value.int_value = stored;
			}
			break;
		}
		case TYPE_SHORT: {
			short stored;
			storage = poco_activation_global_storage(activation, symbol, sizeof(stored));
			if (storage != NULL) {
				memcpy(&stored, storage, sizeof(stored));
				value.kind = POCO_CALLBACK_VALUE_INT;
				value.value.int_value = stored;
			}
			break;
		}
		case TYPE_INT:
			storage =
				poco_activation_global_storage(activation, symbol, sizeof(value.value.int_value));
			if (storage != NULL) {
				value.kind = POCO_CALLBACK_VALUE_INT;
				memcpy(&value.value.int_value, storage, sizeof(value.value.int_value));
			}
			break;
		case TYPE_LONG:
			storage =
				poco_activation_global_storage(activation, symbol, sizeof(value.value.long_value));
			if (storage != NULL) {
				value.kind = POCO_CALLBACK_VALUE_LONG;
				memcpy(&value.value.long_value, storage, sizeof(value.value.long_value));
			}
			break;
		case TYPE_FLOAT: {
			float stored;
			storage = poco_activation_global_storage(activation, symbol, sizeof(stored));
			if (storage != NULL) {
				memcpy(&stored, storage, sizeof(stored));
				value.kind = POCO_CALLBACK_VALUE_DOUBLE;
				value.value.double_value = stored;
			}
			break;
		}
		case TYPE_DOUBLE:
			storage = poco_activation_global_storage(activation, symbol,
													 sizeof(value.value.double_value));
			if (storage != NULL) {
				value.kind = POCO_CALLBACK_VALUE_DOUBLE;
				memcpy(&value.value.double_value, storage, sizeof(value.value.double_value));
			}
			break;
		default:
			break;
	}
	return value;
}

static int poco_type_is_exact(const Type_info* type, TypeComp component)
{
	return type != NULL && type->comp_count == 1 && type->comp[0] == component;
}

static int poco_type_is_char_double_pointer(const Type_info* type)
{
	return type != NULL && type->comp_count == 3 && type->comp[0] == TYPE_CHAR &&
		   type->comp[1] == TYPE_POINTER && type->comp[2] == TYPE_POINTER;
}

PocoStatus poco_activation_run_main(PocoActivation* activation, int argc, char** argv,
									int32_t* out_result)
{
	PocoCallbackValue arguments[2];
	const Func_frame* main_frame;
	const Symbol* parameter;
	PocoStatus status;
	Pt_num result = {0};
	Popot marshaled_argv = {NULL, NULL, NULL};
	long error_line = 0;
	int returns_int;

	if (activation == NULL) {
		return POCO_STATUS_NULL_REFERENCE;
	}
	if (activation->program == NULL || activation->program->vm == NULL ||
		activation->program->vm->destroy_requested || activation->needs_reset) {
		return POCO_STATUS_PARAMETER_RANGE;
	}
	main_frame = find_fuf(activation, "main");
	if (main_frame == NULL) {
		return (PocoStatus)Err_no_main;
	}
	returns_int = poco_type_is_exact(main_frame->return_type, TYPE_INT);
	if (!returns_int && !poco_type_is_exact(main_frame->return_type, TYPE_VOID)) {
		poco_api_report(activation->program->vm, POCO_STATUS_PARAMETER_RANGE, NULL, 0, 0,
						"main must return int or void");
		return POCO_STATUS_PARAMETER_RANGE;
	}
	if (main_frame->pcount == 0 && main_frame->parameters == NULL) {
		activation->err_line = &error_line;
		activation->needs_reset = 1;
		status = (PocoStatus)po_activation_run_entry_values(activation, "main", NULL, 0, &result);
	} else {
		parameter = main_frame->parameters;
		if (main_frame->pcount != 2 || parameter == NULL ||
			!poco_type_is_exact(parameter->ti, TYPE_INT) || parameter->link == NULL ||
			!poco_type_is_char_double_pointer(parameter->link->ti) ||
			parameter->link->link != NULL) {
			poco_api_report(activation->program->vm, POCO_STATUS_PARAMETER_RANGE, NULL, 0, 0,
							"main parameters must be empty or exactly (int, char **)");
			return POCO_STATUS_PARAMETER_RANGE;
		}
		status =
			(PocoStatus)poco_activation_marshal_main_argv(activation, argc, argv, &marshaled_argv);
		if (status != POCO_STATUS_OK) {
			return status;
		}
		arguments[0].kind = POCO_CALLBACK_VALUE_INT;
		arguments[0].value.int_value = argc;
		arguments[1].kind = POCO_CALLBACK_VALUE_POPOT;
		arguments[1].value.popot_value = marshaled_argv;
		activation->err_line = &error_line;
		activation->needs_reset = 1;
		status =
			(PocoStatus)po_activation_run_entry_values(activation, "main", arguments, 2, &result);
	}
	activation->err_line = NULL;
	poco_activation_release_main_argv(activation);
	poco_activation_publish_last_error(activation);
	if (status != POCO_STATUS_OK) {
		poco_api_report(activation->program->vm, status, NULL, error_line, 0,
						"Poco main execution failed");
		return status;
	}
	if (out_result != NULL) {
		*out_result = returns_int ? result.i : 0;
	}
	return POCO_STATUS_OK;
}

PocoStatus poco_call_begin(PocoActivation* activation, const char* name, PocoCall** out_call)
{
	const Func_frame* function;
	PocoCall* call;

	if (out_call == NULL) {
		return POCO_STATUS_NULL_REFERENCE;
	}
	*out_call = NULL;
	if (activation == NULL || name == NULL) {
		return POCO_STATUS_NULL_REFERENCE;
	}
	if (activation->program == NULL || activation->program->vm == NULL ||
		activation->program->vm->destroy_requested) {
		return POCO_STATUS_PARAMETER_RANGE;
	}
	function = find_fuf(activation, name);
	if (function == NULL || function->code_pt == NULL || !function->got_code) {
		return POCO_STATUS_NOT_FOUND;
	}
	call = calloc(1, sizeof(*call));
	if (call == NULL) {
		return POCO_STATUS_OUT_OF_MEMORY;
	}
	call->activation = activation;
	call->function = function;
	*out_call = call;
	return POCO_STATUS_OK;
}

static PocoStatus poco_call_push_value(PocoCall* call, PocoCallbackValue value)
{
	PocoCallbackValue* grown;
	size_t capacity;

	if (call == NULL) {
		return POCO_STATUS_NULL_REFERENCE;
	}
	if (call->invoked) {
		return POCO_STATUS_PARAMETER_RANGE;
	}
	if (call->value_count == call->value_capacity) {
		capacity = call->value_capacity != 0 ? call->value_capacity * 2 : 4;
		if (capacity < call->value_capacity || capacity > SIZE_MAX / sizeof(*grown)) {
			return POCO_STATUS_OUT_OF_MEMORY;
		}
		grown = realloc(call->values, capacity * sizeof(*grown));
		if (grown == NULL) {
			return POCO_STATUS_OUT_OF_MEMORY;
		}
		call->values = grown;
		call->value_capacity = capacity;
	}
	call->values[call->value_count++] = value;
	return POCO_STATUS_OK;
}

PocoStatus poco_call_push_int(PocoCall* call, int value)
{
	PocoCallbackValue argument = {POCO_CALLBACK_VALUE_INT, {.int_value = value}};

	return poco_call_push_value(call, argument);
}

PocoStatus poco_call_push_long(PocoCall* call, long value)
{
	PocoCallbackValue argument = {POCO_CALLBACK_VALUE_LONG, {.long_value = value}};

	return poco_call_push_value(call, argument);
}

PocoStatus poco_call_push_double(PocoCall* call, double value)
{
	PocoCallbackValue argument = {POCO_CALLBACK_VALUE_DOUBLE, {.double_value = value}};

	return poco_call_push_value(call, argument);
}

static PocoStatus poco_call_reserve_pointer_registration(PocoCall* call)
{
	PocoCallPointerRegistration* grown;
	size_t capacity;

	if (call->registered_pointer_count < call->registered_pointer_capacity) {
		return POCO_STATUS_OK;
	}
	capacity = call->registered_pointer_capacity != 0 ? call->registered_pointer_capacity * 2 : 2;
	if (capacity < call->registered_pointer_capacity || capacity > SIZE_MAX / sizeof(*grown)) {
		return POCO_STATUS_OUT_OF_MEMORY;
	}
	grown = realloc(call->pointer_registrations, capacity * sizeof(*grown));
	if (grown == NULL) {
		return POCO_STATUS_OUT_OF_MEMORY;
	}
	call->pointer_registrations = grown;
	call->registered_pointer_capacity = capacity;
	return POCO_STATUS_OK;
}

static void poco_call_release_pointer_registrations(PocoCall* call)
{
	PocoPointerRegistry* registry;

	if (call == NULL || call->activation == NULL) {
		return;
	}
	registry = call->activation->pointer_registry;
	while (call->registered_pointer_count != 0) {
		void* pointer = call->pointer_registrations[--call->registered_pointer_count].pointer;

		(void)poco_pointer_registry_unregister_borrowed(registry, pointer);
	}
}

PocoStatus poco_call_push_pointer(PocoCall* call, void* pointer, size_t byte_count,
								  PocoPointerPermission permissions)
{
	const uint32_t valid_permissions = POCO_POINTER_PERMISSION_READ | POCO_POINTER_PERMISSION_WRITE;
	PocoCallbackValue argument = {0};
	PocoPointerRegistry* registry;
	uint32_t permission_bits = (uint32_t)permissions;
	uintptr_t start;
	uintptr_t end;
	PocoStatus status;
	size_t index;
	int registered = 0;

	if (call == NULL) {
		return POCO_STATUS_NULL_REFERENCE;
	}
	argument.kind = POCO_CALLBACK_VALUE_POPOT;
	if (call->invoked || call->activation == NULL || call->activation->program == NULL ||
		call->activation->program->vm == NULL || call->activation->program->vm->destroy_requested) {
		return POCO_STATUS_PARAMETER_RANGE;
	}
	start = (uintptr_t)pointer;
	if (pointer == NULL || byte_count == 0 || byte_count - 1 > UINTPTR_MAX - start ||
		(permission_bits & valid_permissions) == 0 || (permission_bits & ~valid_permissions) != 0) {
		return POCO_STATUS_PARAMETER_RANGE;
	}
	end = start + byte_count - 1;
	registry = call->activation->pointer_registry;
	for (index = 0; index < call->registered_pointer_count; ++index) {
		const PocoCallPointerRegistration* existing = &call->pointer_registrations[index];
		uintptr_t existing_start = (uintptr_t)existing->pointer;
		uintptr_t existing_end = existing_start + existing->byte_count - 1;

		if (existing_start == start) {
			if (existing->byte_count != byte_count || existing->permissions != permission_bits) {
				return POCO_STATUS_PARAMETER_RANGE;
			}
			break;
		}
		if (start <= existing_end && existing_start <= end &&
			existing->permissions != permission_bits) {
			return POCO_STATUS_PARAMETER_RANGE;
		}
	}
	if (index == call->registered_pointer_count) {
		status = poco_call_reserve_pointer_registration(call);
		if (status != POCO_STATUS_OK) {
			return status;
		}
		status = (PocoStatus)poco_pointer_registry_register_borrowed(registry, pointer, byte_count,
																	 permission_bits);
		if (status != POCO_STATUS_OK) {
			return status;
		}
		call->pointer_registrations[call->registered_pointer_count].pointer = pointer;
		call->pointer_registrations[call->registered_pointer_count].byte_count = byte_count;
		call->pointer_registrations[call->registered_pointer_count].permissions = permission_bits;
		++call->registered_pointer_count;
		registered = 1;
	}
	argument.value.popot_value.pt = pointer;
	argument.value.popot_value.min = pointer;
	argument.value.popot_value.max = (void*)end;
	status = poco_call_push_value(call, argument);
	if (status != POCO_STATUS_OK && registered) {
		--call->registered_pointer_count;
		(void)poco_pointer_registry_unregister_borrowed(registry, pointer);
	}
	return status;
}

static PocoStatus poco_call_coerce_value(PocoCallbackValue input, const Type_info* type,
										 PocoCallbackValue* output)
{
	if (type == NULL || output == NULL) {
		return POCO_STATUS_PARAMETER_RANGE;
	}
	*output = poco_invalid_callback_value();
	switch (type->ido_type) {
		case IDO_INT:
			output->kind = POCO_CALLBACK_VALUE_INT;
			switch (input.kind) {
				case POCO_CALLBACK_VALUE_INT:
					output->value.int_value = input.value.int_value;
					return POCO_STATUS_OK;
				case POCO_CALLBACK_VALUE_LONG:
					output->value.int_value = (int)input.value.long_value;
					return POCO_STATUS_OK;
				case POCO_CALLBACK_VALUE_DOUBLE:
					if (!isfinite(input.value.double_value) ||
						(long double)input.value.double_value < (long double)INT_MIN ||
						(long double)input.value.double_value > (long double)INT_MAX) {
						return POCO_STATUS_PARAMETER_RANGE;
					}
					output->value.int_value = (int)input.value.double_value;
					return POCO_STATUS_OK;
				default:
					return POCO_STATUS_PARAMETER_RANGE;
			}
		case IDO_LONG:
			output->kind = POCO_CALLBACK_VALUE_LONG;
			switch (input.kind) {
				case POCO_CALLBACK_VALUE_INT:
					output->value.long_value = input.value.int_value;
					return POCO_STATUS_OK;
				case POCO_CALLBACK_VALUE_LONG:
					output->value.long_value = input.value.long_value;
					return POCO_STATUS_OK;
				case POCO_CALLBACK_VALUE_DOUBLE:
					if (!isfinite(input.value.double_value) ||
						(long double)input.value.double_value < (long double)LONG_MIN ||
						(long double)input.value.double_value > (long double)LONG_MAX) {
						return POCO_STATUS_PARAMETER_RANGE;
					}
					output->value.long_value = (long)input.value.double_value;
					return POCO_STATUS_OK;
				default:
					return POCO_STATUS_PARAMETER_RANGE;
			}
		case IDO_DOUBLE:
			output->kind = POCO_CALLBACK_VALUE_DOUBLE;
			switch (input.kind) {
				case POCO_CALLBACK_VALUE_INT:
					output->value.double_value = input.value.int_value;
					return POCO_STATUS_OK;
				case POCO_CALLBACK_VALUE_LONG:
					output->value.double_value = (double)input.value.long_value;
					return POCO_STATUS_OK;
				case POCO_CALLBACK_VALUE_DOUBLE:
					output->value.double_value = input.value.double_value;
					return POCO_STATUS_OK;
				default:
					return POCO_STATUS_PARAMETER_RANGE;
			}
		case IDO_POINTER:
			if (input.kind != POCO_CALLBACK_VALUE_POPOT) {
				return POCO_STATUS_PARAMETER_RANGE;
			}
			*output = input;
			return POCO_STATUS_OK;
		default:
			return POCO_STATUS_PARAMETER_RANGE;
	}
}

static PocoCallbackValue poco_call_result_value(const Type_info* type, Pt_num result)
{
	PocoCallbackValue value = poco_invalid_callback_value();

	if (type == NULL) {
		return value;
	}
	switch (type->ido_type) {
		case IDO_INT:
			value.kind = POCO_CALLBACK_VALUE_INT;
			value.value.int_value = result.i;
			break;
		case IDO_LONG:
			value.kind = POCO_CALLBACK_VALUE_LONG;
			value.value.long_value = result.l;
			break;
		case IDO_DOUBLE:
			value.kind = POCO_CALLBACK_VALUE_DOUBLE;
			value.value.double_value = result.d;
			break;
		case IDO_POINTER:
			value.kind = POCO_CALLBACK_VALUE_POPOT;
			value.value.popot_value = result.ppt;
			break;
		default:
			break;
	}
	return value;
}

PocoStatus poco_call_invoke(PocoCall* call, PocoCallbackValue* out_result)
{
	PocoCallbackValue* coerced = NULL;
	const Symbol* parameter;
	PocoStatus status = POCO_STATUS_OK;
	Pt_num result = {0};
	long error_line = 0;
	long* previous_err_line;
	size_t index;

	if (out_result != NULL) {
		*out_result = poco_invalid_callback_value();
	}
	if (call == NULL) {
		return POCO_STATUS_NULL_REFERENCE;
	}
	if (call->invoked) {
		return POCO_STATUS_PARAMETER_RANGE;
	}
	call->invoked = 1;
	if (call->activation == NULL || call->activation->program == NULL ||
		call->activation->program->vm == NULL || call->activation->program->vm->destroy_requested ||
		call->function == NULL || call->function->pcount < 0 ||
		call->value_count != (size_t)call->function->pcount) {
		status = POCO_STATUS_PARAMETER_RANGE;
		goto OUT;
	}
	if (call->value_count != 0) {
		coerced = malloc(call->value_count * sizeof(*coerced));
		if (coerced == NULL) {
			status = POCO_STATUS_OUT_OF_MEMORY;
			goto OUT;
		}
	}
	parameter = call->function->parameters;
	for (index = 0; index < call->value_count; ++index) {
		if (parameter == NULL) {
			status = POCO_STATUS_PARAMETER_RANGE;
			goto OUT;
		}
		status = poco_call_coerce_value(call->values[index], parameter->ti, &coerced[index]);
		if (status != POCO_STATUS_OK) {
			goto OUT;
		}
		parameter = parameter->link;
	}
	if (parameter != NULL) {
		status = POCO_STATUS_PARAMETER_RANGE;
		goto OUT;
	}
	previous_err_line = call->activation->err_line;
	call->activation->err_line = &error_line;
	status = (PocoStatus)po_activation_run_entry_values(call->activation, call->function->name,
														coerced, call->value_count, &result);
	call->activation->err_line = previous_err_line;
	if (status == Err_in_err_file && call->activation->builtin_error == Err_poco_ffi_bounds) {
		status = POCO_STATUS_FFI_BOUNDS;
	}
	poco_activation_publish_last_error(call->activation);
	if (status != POCO_STATUS_OK) {
		poco_api_report(call->activation->program->vm, status, NULL, error_line, 0,
						"Poco function call failed");
		goto OUT;
	}
	if (out_result != NULL) {
		*out_result = poco_call_result_value(call->function->return_type, result);
	}

OUT:
	poco_call_release_pointer_registrations(call);
	free(coerced);
	return status;
}

void poco_call_end(PocoCall* call)
{
	if (call == NULL) {
		return;
	}
	poco_call_release_pointer_registrations(call);
	free(call->pointer_registrations);
	free(call->values);
	free(call);
}

PocoStatus poco_activation_run(PocoActivation* activation, const PocoRunOptions* options,
							   int32_t* out_result)
{
	Errcode run_status;
	long error_line = 0;
	PocoVm* vm;
	Poco_api_cancel_context cancel_context;

	if (activation == NULL) {
		return POCO_STATUS_NULL_REFERENCE;
	}
	if (activation->program == NULL || activation->program->vm == NULL) {
		return POCO_STATUS_PARAMETER_RANGE;
	}
	vm = activation->program->vm;
	if (vm->destroy_requested || activation->needs_reset) {
		return POCO_STATUS_PARAMETER_RANGE;
	}
	cancel_context.callback = options != NULL ? options->cancel_callback : NULL;
	cancel_context.user_data = options != NULL ? options->cancel_user_data : NULL;
	activation->enable_debug_trace = true;
	activation->check_abort = cancel_context.callback != NULL ? poco_api_cancel : NULL;
	activation->check_abort_data = cancel_context.callback != NULL ? &cancel_context : NULL;
	activation->trace_file = options != NULL ? options->trace_file : NULL;
	activation->err_line = &error_line;
	run_status = po_pev_alloc_data(activation);
	activation->needs_reset = 1;
	if (run_status == Success) {
		run_status = po_activation_run_entry(activation, "main");
	}
	/* The legacy interpreter formats library errors for its trace and returns
	 * Err_in_err_file.  Preserve the precise new FFI boundary status at the
	 * public VM API so a host can distinguish a rejected span from a generic
	 * reported script failure. */
	if (run_status == Err_in_err_file && activation->builtin_error == Err_poco_ffi_bounds) {
		run_status = Err_poco_ffi_bounds;
	}
	if (run_status == Success && out_result != NULL) {
		*out_result = activation->result.i;
	}
	poco_activation_publish_last_error(activation);
	if (run_status != Success) {
		poco_api_report(vm, (PocoStatus)run_status, NULL, error_line, 0,
						"Poco program execution failed");
	}
	return (PocoStatus)run_status;
}

void poco_activation_release(PocoActivation* activation)
{
	poco_activation_destroy(activation);
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
		free_poco(&program->executable);
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


#ifdef POCO_DEBUG_DUMP
/*****************************************************************************
 *
 ****************************************************************************/
void po_show_basic_sizes()
{
	fprintf(stderr,
			"Symbol.......%5zu\n"
			"Type_info....%5zu\n"
			"Code_buf.....%5zu\n"
			"Exp_frame....%5zu\n"
			"Func_frame...%5zu\n"
			"Poco_frame...%5zu\n"
			"Tstack.......%5zu\n"
			"Token........%5zu\n"
			"Poco_cb......%5zu\n",
			sizeof(Symbol), sizeof(Type_info), sizeof(Code_buf),
			sizeof(Exp_frame) + HASH_SIZE * sizeof(Symbol*), sizeof(Func_frame), sizeof(Poco_frame),
			sizeof(Tstack), sizeof(Token), sizeof(Poco_cb));
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
	if (fn != NULL) {
		if ((*f = must_fmake(pcb, fn)) == NULL) {
			return (Err_create);
		}
	}
	return (Success);
}

/*****************************************************************************
 * close a file if it's open, and if it's not a device file.
 ****************************************************************************/
static void gentle_fclose(FILE* f)
{
	if (f != NULL && f != stdout && f != stderr) {
		fclose(f);
	}
}

/*****************************************************************************
 * free a poco_run_env, and the associate stack and data areas.
 ****************************************************************************/
static void po_pev_free_data(PocoActivation* activation)
{
	if (activation != NULL && activation->data != NULL) {
		memset(activation->data, 0, (size_t)activation->code->data_size);
	}
}

static void po_activation_cleanup_libs(PocoActivation* activation)
{
	if (activation == NULL || !activation->libraries_initialized) {
		return;
	}
	po_cleanup_libs(activation->loaded_libraries);
	po_cleanup_libs(activation->builtin_libraries);
	activation->libraries_initialized = 0;
}

static Errcode po_activation_init_libs(PocoActivation* activation)
{
	Errcode err;

	if ((err = po_init_libs(activation->builtin_libraries)) < Success) {
		po_cleanup_libs(activation->builtin_libraries);
		return err;
	}
	if ((err = po_init_libs(activation->loaded_libraries)) < Success) {
		po_cleanup_libs(activation->loaded_libraries);
		po_cleanup_libs(activation->builtin_libraries);
		return err;
	}
	activation->libraries_initialized = 1;
	return Success;
}

/*****************************************************************************
 * alloc poco_run_env, and stack and data areas. run data init code.
 * this routine should only be called after a successfull compile.
 ****************************************************************************/
static Errcode po_pev_alloc_data(PocoActivation* activation)
{
	Errcode err;
	const Func_frame* frame;
	size_t unit_index;
	size_t unit_count = 0;

	po_activation_cleanup_libs(activation);
	activation->initialized = 0;
	po_pev_free_data(activation);
	if ((err = po_activation_init_libs(activation)) < Success) {
		return err;
	}
	for (frame = activation->code->functions; frame != NULL; frame = frame->next) {
		if (!frame->got_code && frame->unit_index >= unit_count) {
			unit_count = frame->unit_index + 1;
		}
	}
	for (unit_index = 0; unit_index < unit_count; ++unit_index) {
		for (frame = activation->code->functions; frame != NULL; frame = frame->next) {
			if (!frame->got_code && frame->unit_index == unit_index) {
				err = po_run_ops(activation, frame->code_pt, NULL);
				if (err < Success) {
					goto ERR;
				}
				break;
			}
		}
	}
	activation->initialized = 1;

	return Success;

ERR:
	po_activation_cleanup_libs(activation);
	po_pev_free_data(activation);
	return err;
}

/*****************************************************************************
 * find a fuf for a function with a given name.
 * (linear search...slow, slow.  added first-char quick check to help a bit).
 ****************************************************************************/
static const Func_frame* find_fuf(const PocoActivation* activation, const char* name)
{
	const Func_frame* f;

	f = activation->code->functions;
	while (f != NULL) {
		if (f->name[0] == *name) {
			if (po_eqstrcmp(f->name, name) == 0) {
				break;
			}
		}
		f = f->next;
	}
	return (f);
}

/*****************************************************************************
 * run a given function from a compiled poco program.
 * (at this point in development, the 'given function' had better be 'main'!)
 ****************************************************************************/
static Errcode run_file(PocoActivation* activation, const char* entry)
{
	const Func_frame* f;

	if ((f = find_fuf(activation, entry)) == NULL) {
		return (Err_no_main);
	}
	return po_run_ops(activation, f->code_pt, NULL);
}

static Errcode run_file_values(PocoActivation* activation, const char* entry,
							   const PocoCallbackValue* values, size_t value_count, Pt_num* result)
{
	const Func_frame* f;

	if ((f = find_fuf(activation, entry)) == NULL) {
		return Err_no_main;
	}
	return po_run_ops_values(activation, f->code_pt, result, values, value_count);
}

/*****************************************************************************
 * run a given function from a poco program after init'ing the libs.
 ****************************************************************************/
Errcode po_activation_run_entry(PocoActivation* activation, const char* entry)
{
	Errcode err;

	if (!activation->libraries_initialized) {
		if ((err = po_activation_init_libs(activation)) < Success) {
			return err;
		}
	}
	err = run_file(activation, entry);
	po_activation_cleanup_libs(activation);
	return (err);
}

static Errcode po_activation_run_entry_values(PocoActivation* activation, const char* entry,
											  const PocoCallbackValue* values, size_t value_count,
											  Pt_num* result)
{
	Errcode err;
	int top_level = activation->run_depth == 0;

	if (!activation->libraries_initialized) {
		if ((err = po_activation_init_libs(activation)) < Success) {
			return err;
		}
	}
	err = run_file_values(activation, entry, values, value_count, result);
	if (top_level) {
		po_activation_cleanup_libs(activation);
	}
	return err;
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
			if ((err = print_one_lib(f, lib->lib, lib->count)) < Success) {
				break;
			}
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
static int poco_findpoe_in_vm(PocoVm* vm, char* libname, Lib_proto** plibreturn)
{
	PocoActivation* activation = vm != NULL ? vm->activation : NULL;
	Poco_lib* ll;

	/*------------------------------------------------------------------------
	 * first make sure we don't get crashed by a naughty caller...
	 *----------------------------------------------------------------------*/

	if (NULL == plibreturn) {
		return Err_null_ref; /* defensive programming */
	}

	if (NULL == activation) {
		goto ERROR_EXIT; /* "can't happen" */
	}

	/*------------------------------------------------------------------------
	 * if the libname pointer is NULL, the caller wants the next library in
	 * the linked list, go return it...
	 *----------------------------------------------------------------------*/

	if (NULL == libname) {
		ll = activation->findpoe_next;
		goto RETURN_LIST_ITEM;
	}

	/*------------------------------------------------------------------------
	 * if the requested name is poco$builtin or poco$loaded, the caller wants
	 * to start a series of calls to walk the corresponding list...
	 *----------------------------------------------------------------------*/

	if (0 == stricmp(libname, "poco$builtin")) {
		ll = activation->builtin_libraries;
		goto RETURN_LIST_ITEM;
	}

	if (0 == stricmp(libname, "poco$loaded")) {
		ll = activation->loaded_libraries;
		goto RETURN_LIST_ITEM;
	}

	/*------------------------------------------------------------------------
	 * if we get to here, the caller wants a specific library, look for it...
	 *----------------------------------------------------------------------*/

	for (ll = activation->loaded_libraries; ll != NULL; ll = ll->next) {
		if (0 == stricmp(libname, ll->name)) {
			goto GOOD_EXIT;
		}
	}

ERROR_EXIT:

	*plibreturn = NULL;
	return Err_not_found;

RETURN_LIST_ITEM:

	if (NULL == ll) {
		goto ERROR_EXIT; /* return not-found/end-of-list status */
	}

	activation->findpoe_next = ll->next; /* reset list ptr for next call */

GOOD_EXIT:

	*plibreturn = ll->lib;
	return ll->count;
}

int po_findpoe(PocoVm* vm, char* libname, Lib_proto** plibreturn)
{
	return poco_findpoe_in_vm(vm, libname, plibreturn);
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
	} else {
		if (pcb->t.verbose) {
			fprintf(stderr, "[poco library] #pragma poco library '%s' encountered\n", libname);
		}
		{
			if ((err = pj_load_pocorex(&ll, pcb->t.script_path, libname, id_string,
									   pcb->t.library_dirs, pcb->t.verbose,
									   poco_vm_module_hooks(pcb->run.vm))) < Success) {
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
	Poco_lib* pl = fs->source.lib;
	Lib_proto* pp;

	if (fs->line_count >= pl->count) {
		if (NULL != pcb->run.loaded_libs || NULL == (pl = pl->next)) {
			pcb->libfunc = NULL;
			return NULL;
		}
		fs->source.lib = pl;
		fs->line_count = 0;
	}
	pp = &pl->lib[fs->line_count++];
	pcb->libfunc = pp->func;
	pcb->libcontract = pp->contract;
	pcb->libflags = pp->flags;

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
static Errcode compile_poco_sources_with_vm(
	PocoVm* vm, void** ppexe, const char* const* source_names,
	const char* const* physical_source_paths, const char* const* sources,
	const size_t* source_lengths, Names* const* include_dirs, const size_t* const* use_indices,
	const size_t* use_counts, size_t source_count, char* errors_name, char* dump_name,
	Poco_lib* lib, char* err_file, size_t err_file_capacity, long* err_line, int* err_char,
	bool verbose)
{
	Poco_cb* pcb;
	Errcode err;
	Poco_run_env* pev;
	File_stack* fs;
	const char* current_source_name = source_names[0];
	size_t source_index;

	*ppexe = NULL;
	if (vm != NULL) {
		vm->last_error[0] = '\0';
	}

	if (Success != (err = po_init_memory_management(&pcb))) {
		return Err_no_memory; /* MUST return immediately if init fails. */
	}

	pcb->run.vm = vm;
	pcb->compile_aborted = false;
	pcb->compile_err = Success;

	pcb->stack_bottom = ((char*)&ppexe) - MAX_STACK;

	pcb->t.err_file = stdout;
	pcb->t.library_dirs = vm != NULL ? vm->library_dirs : NULL;
	pcb->t.verbose = verbose;

	pcb->libfunc = NULL;
	pcb->libcontract = NULL;
	pcb->libflags = 0;
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

	for (source_index = 0; source_index < source_count; ++source_index) {
		current_source_name = source_names[source_index];
		pcb->current_unit_index = source_index;
		pcb->current_use_indices = use_indices != NULL ? use_indices[source_index] : NULL;
		pcb->current_use_count = use_counts != NULL ? use_counts[source_index] : 0;
		pcb->t.include_dirs = include_dirs != NULL ? include_dirs[source_index] : NULL;
		pcb->t.script_path =
			sources[source_index] != NULL
				? (physical_source_paths != NULL ? physical_source_paths[source_index] : NULL)
				: source_names[source_index];
		err = sources[source_index] != NULL
				  ? po_compile_buffer(pcb, (char*)source_names[source_index], sources[source_index],
									  source_lengths[source_index])
				  : po_compile_file(pcb, (char*)source_names[source_index]);
		if (err != Success || pcb->compile_aborted) {
			break;
		}
	}
	if (err == Success && !pcb->compile_aborted && !po_link_compiled_units(pcb)) {
		err = pcb->compile_err < Success ? pcb->compile_err : Err_syntax;
	}
	/* include_dirs is caller-owned and may be a transient file-directory
	 * entry.  It is compile-only state, so do not retain a dangling pointer in
	 * the successful program's allocation owner. */
	pcb->t.include_dirs = NULL;
	pcb->t.library_dirs = NULL;
	pcb->t.script_path = NULL;
	if (err != Success || pcb->compile_aborted) {
		/* Read error output into the owning VM's diagnostic buffer. */
		if (vm != NULL && pcb->t.err_file != NULL && pcb->t.err_file != stdout &&
			pcb->t.err_file != stderr) {
			fflush(pcb->t.err_file);
			rewind(pcb->t.err_file);
			size_t n = fread(vm->last_error, 1, sizeof(vm->last_error) - 1, pcb->t.err_file);
			vm->last_error[n] = '\0';
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
	*pev = pcb->run;
	pev->compile_pcb = pcb;
	*ppexe = pev;


OUT:
	gentle_fclose(pcb->po_dump_file);
	gentle_fclose(pcb->t.err_file);

	/* A successfully compiled program retains pcb as its owner for the
	 * compiler/runtime allocation arena.  free_poco() releases that arena after
	 * execution; releasing it here leaves a dangling compile_pcb and causes a
	 * second free during program destruction. */
	if (err != Success) {
		*err_char = 0;
		fs = pcb->t.file_stack;
		if (fs == NULL || fs->line_count == 0) {
			*err_line = pcb->error_line_number;
			*err_char = pcb->error_char_number;
			snprintf(err_file, err_file_capacity, "%s", current_source_name);
		} else {
			if ((fs->flags & FSF_ISLIB) && fs->pred != NULL) {
				*err_line = fs->pred->line_count;
				snprintf(err_file, err_file_capacity, "%s", fs->pred->name);
			} else {
				*err_line = pcb->error_line_number;
				*err_char = pcb->error_char_number;
				snprintf(err_file, err_file_capacity, "%s", fs->name);
			}
		}

		po_free_pp(pcb);
		pj_free_pocorexes(&pcb->run.loaded_libs);
		po_free_all_memory(pcb);
	}

	if (err == Success) {
		err = po_ffi_build_structures(pev);
		if (err != Success) {
			free_poco(ppexe);
		}
	}

	(void)errors_name;
	return err;
}

static Errcode compile_poco_source_with_vm(PocoVm* vm, void** ppexe, char* source_name,
										   const char* physical_source_path, const char* source,
										   size_t source_length, bool from_buffer,
										   char* errors_name, char* dump_name, Poco_lib* lib,
										   char* err_file, size_t err_file_capacity, long* err_line,
										   int* err_char, Names* include_dirs, bool verbose)
{
	const char* source_names[] = {source_name};
	const char* physical_source_paths[] = {physical_source_path};
	const char* sources[] = {from_buffer ? source : NULL};
	size_t source_lengths[] = {source_length};
	Names* source_include_dirs[] = {include_dirs};

	return compile_poco_sources_with_vm(vm, ppexe, source_names, physical_source_paths, sources,
										source_lengths, source_include_dirs, NULL, NULL, 1,
										errors_name, dump_name, lib, err_file, err_file_capacity,
										err_line, err_char, verbose);
}

Errcode compile_poco_with_vm(PocoVm* vm, void** ppexe, char* source_name, char* errors_name,
							 char* dump_name, Poco_lib* lib, char* err_file, long* err_line,
							 int* err_char, Names* include_dirs, bool verbose)
{
	return compile_poco_source_with_vm(vm, ppexe, source_name, source_name, NULL, 0, false,
									   errors_name, dump_name, lib, err_file, PATH_SIZE, err_line,
									   err_char, include_dirs, verbose);
}

Errcode compile_poco_buffer_with_vm(PocoVm* vm, void** ppexe, char* source_name,
									const char* physical_source_path, const char* source,
									size_t source_length, char* dump_name, Poco_lib* lib,
									char* err_file, size_t err_file_capacity, long* err_line,
									int* err_char, Names* include_dirs, bool verbose)
{
	return compile_poco_source_with_vm(
		vm, ppexe, source_name, physical_source_path, source, source_length, true, NULL, dump_name,
		lib, err_file, err_file_capacity, err_line, err_char, include_dirs, verbose);
}

Errcode compile_poco_files_with_vm(PocoVm* vm, void** ppexe, const char* const* source_names,
								   const char* const* physical_source_paths,
								   const char* const* sources, const size_t* source_lengths,
								   Names* const* include_dirs, const size_t* const* use_indices,
								   const size_t* use_counts, size_t source_count, Poco_lib* lib,
								   char* err_file, size_t err_file_capacity, long* err_line,
								   int* err_char, bool verbose)
{
	return compile_poco_sources_with_vm(vm, ppexe, source_names, physical_source_paths, sources,
										source_lengths, include_dirs, use_indices, use_counts,
										source_count, NULL, NULL, lib, err_file, err_file_capacity,
										err_line, err_char, verbose);
}

Errcode compile_poco(void** ppexe,        /* returns executable pexe on Success */
					 char* source_name,   /* name of source file */
					 char* errors_name,   /* error file or NULL for stderr */
					 char* dump_name,     /* disassembly file or NULL for none */
					 Poco_lib* lib,       /* for built-in function library */
					 char* err_file,      /* file where error detected */
					 long* err_line,      /* line where error detected */
					 int* err_char,       /* character in line where err detected */
					 Names* include_dirs, /* include search path */
					 bool verbose         /* enable verbose debug output */
)
{
	return compile_poco_with_vm(NULL, ppexe, source_name, errors_name, dump_name, lib, err_file,
								err_line, err_char, include_dirs, verbose);
}

/*****************************************************************************
 * run a poco program compiled earlier using compile_poco. exe entry from PJ.
 ****************************************************************************/
Errcode run_poco(void** ppexe, char* trace_file, bool (*check_abort)(void*), void* check_abort_data,
				 long* err_line)
{
	PocoVm legacy_vm = {0};
	PocoVm* vm;
	Poco_run_env* executable;
	Poco_program_code code;
	PocoActivation* activation = NULL;
	Errcode run_err;
	bool uses_legacy_vm = false;

	if (ppexe == NULL || (executable = *ppexe) == NULL) {
		return (Err_not_found);
	}

	vm = executable->vm;
	if (vm == NULL) {
		vm = &legacy_vm;
		uses_legacy_vm = true;
	}
	memset(&code, 0, sizeof(code));
	code.stack_size = executable->stack_size;
	code.data_size = executable->data_size;
	code.functions = executable->fff;
	code.literals = executable->literals;
	code.prototypes = executable->protos;
	code.ffi_bindings = executable->func_map;
	code.builtin_libraries = executable->lib;
	code.loaded_libraries = executable->loaded_libs;
	code.allocation_owner = executable->compile_pcb;
	run_err = poco_activation_create(NULL, &code, vm, &activation);
	if (run_err < Success) {
		return run_err;
	}
	activation->enable_debug_trace = true;
	activation->check_abort = check_abort;
	activation->check_abort_data = check_abort_data;
	activation->trace_file = trace_file;
	activation->err_line = err_line;
	run_err = po_pev_alloc_data(activation);
	if (run_err >= Success) {
		run_err = po_activation_run_entry(activation, "main");
	}
	/* Legacy callers inspect this field after run_poco(). */
	executable->result = activation->result;
	poco_activation_destroy(activation);
	(void)uses_legacy_vm;

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
		compile_pcb = (Poco_cb*)pp->compile_pcb;
		po_free_run_env(pp);
		pj_free_pocorexes(&pp->loaded_libs);
		pj_free(pp); /* this is allocated from PJ, free back to PJ. */
		*ppexe = NULL;
	}
	if (compile_pcb != NULL) {
		po_free_all_memory(compile_pcb);
	}
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
	Func_frame* function = fuf;

	if (function == NULL || function->magic != FUNC_MAGIC || function->code_pt == NULL ||
		function->activation == NULL) {
		return NULL;
	}
	return function;
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
