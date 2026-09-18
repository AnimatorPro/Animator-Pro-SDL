/*******************************************************************************
 * activation.c - Per-run state for an immutable compiled Poco program.
 ******************************************************************************/

#include "activation.h"
#include "debug_internal.h"
#include "poco_errcodes.h"
#include "pocoface.h"
#include "pocolib.h"
#include "pocoload.h"
#include "pocotype.h"
#include "program_internal.h"
#include "runops.h"
#include "strlib.h"
#include "vm_api.h"
#include "vm_diagnostics.h"

#include <float.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static Errcode po_pev_alloc_data(PocoActivation* activation);

static void free_libraries(Poco_lib* library);
static void free_program_libraries(Poco_program_library* library);
static void free_callback_frames(Func_frame* frame);

static Poco_lib* clone_libraries(const Poco_lib* source, PocoVm* runtime_vm)
{
	Poco_lib* first = NULL;
	Poco_lib* tail = NULL;

	while (source != NULL) {
		Poco_lib* copy = calloc(1, sizeof(*copy));

		if (copy == NULL) {
			free_libraries(first);
			return NULL;
		}
		*copy = *source;
		copy->next = NULL;
		copy->vm = runtime_vm;
		init_list(&copy->resources);
		if (poco_clone_loaded_library_runtime(source, copy) < Success) {
			free(copy);
			free_libraries(first);
			return NULL;
		}
		if (tail != NULL) {
			tail->next = copy;
		} else {
			first = copy;
		}
		tail = copy;
		source = source->next;
	}
	return first;
}

static Poco_program_library* clone_program_libraries(const Poco_program_library* source,
													 PocoVm* runtime_vm, Poco_lib** out_libraries)
{
	Poco_program_library* first = NULL;
	Poco_program_library* tail = NULL;

	*out_libraries = NULL;
	while (source != NULL) {
		Poco_program_library* copy = calloc(1, sizeof(*copy));

		if (copy == NULL) {
			free_program_libraries(first);
			*out_libraries = NULL;
			return NULL;
		}
		*copy = *source;
		copy->next = NULL;
		copy->initialized = 0;
		copy->library = source->library;
		copy->library.next = NULL;
		copy->library.vm = runtime_vm;
		init_list(&copy->library.resources);
		if (source->public_library != NULL) {
			copy->public_library_storage = *source->public_library;
			copy->public_library = &copy->public_library_storage;
			copy->library.local_data = copy;
		}
		if (tail != NULL) {
			tail->next = copy;
			tail->library.next = &copy->library;
		} else {
			first = copy;
			*out_libraries = &copy->library;
		}
		tail = copy;
		source = source->next;
	}
	return first;
}

static void free_libraries(Poco_lib* library)
{
	while (library != NULL) {
		Poco_lib* next = library->next;
		poco_free_loaded_library_runtime(library);
		free(library);
		library = next;
	}
}

static void free_program_libraries(Poco_program_library* library)
{
	while (library != NULL) {
		Poco_program_library* next = library->next;
		free(library);
		library = next;
	}
}

static void free_callback_frames(Func_frame* frame)
{
	while (frame != NULL) {
		Func_frame* next = frame->mlink;
		free(frame);
		frame = next;
	}
}

void poco_activation_release_main_argv(PocoActivation* activation)
{
	if (activation != NULL && activation->main_argv_allocation != NULL) {
		pj_free(activation->main_argv_allocation);
		activation->main_argv_allocation = NULL;
	}
}

Errcode poco_activation_marshal_main_argv(PocoActivation* activation, int argc, char* const* argv,
										  Popot* out_argv)
{
	Popot* entries;
	char* string_data;
	size_t entry_bytes;
	size_t string_bytes = 0;
	size_t total_bytes;
	int index;

	if (activation == NULL || out_argv == NULL) {
		return Err_null_ref;
	}
	out_argv->pt = out_argv->min = out_argv->max = NULL;
	poco_activation_release_main_argv(activation);
	if (argc < 0) {
		return Err_parameter_range;
	}
	if (argc == 0) {
		return Success;
	}
	if (argv == NULL) {
		return Err_null_ref;
	}
	if ((size_t)argc > SIZE_MAX / sizeof(*entries)) {
		return Err_parameter_range;
	}
	entry_bytes = (size_t)argc * sizeof(*entries);
	for (index = 0; index < argc; ++index) {
		size_t length;

		if (argv[index] == NULL) {
			return Err_null_ref;
		}
		length = strlen(argv[index]);
		if (length == SIZE_MAX || string_bytes > SIZE_MAX - length - 1) {
			return Err_parameter_range;
		}
		string_bytes += length + 1;
	}
	if (entry_bytes > SIZE_MAX - string_bytes) {
		return Err_parameter_range;
	}
	total_bytes = entry_bytes + string_bytes;
	if (total_bytes > (size_t)LONG_MAX) {
		return Err_parameter_range;
	}
	entries = pj_malloc((long)total_bytes);
	if (entries == NULL) {
		return Err_no_memory;
	}
	activation->main_argv_allocation = entries;
	string_data = (char*)entries + entry_bytes;
	for (index = 0; index < argc; ++index) {
		size_t length = strlen(argv[index]) + 1;

		memcpy(string_data, argv[index], length);
		entries[index].pt = string_data;
		entries[index].min = string_data;
		entries[index].max = string_data + length - 1;
		string_data += length;
	}
	out_argv->pt = entries;
	out_argv->min = entries;
	out_argv->max = (char*)entries + entry_bytes - 1;
	return Success;
}

static void reset_libraries(const Poco_lib* source, Poco_lib* runtime, PocoVm* runtime_vm)
{
	while (source != NULL && runtime != NULL) {
		Poco_lib* next = runtime->next;
		void* retained_local_data = runtime->local_data;

		*runtime = *source;
		poco_reset_loaded_library_runtime(source, runtime, retained_local_data);
		runtime->next = next;
		runtime->vm = runtime_vm;
		init_list(&runtime->resources);
		source = source->next;
		runtime = next;
	}
}

static void reset_program_libraries(const Poco_program_library* source,
									Poco_program_library* runtime, PocoVm* runtime_vm)
{
	while (source != NULL && runtime != NULL) {
		Poco_program_library* next = runtime->next;
		Poco_lib* next_library = next != NULL ? &next->library : NULL;

		*runtime = *source;
		runtime->next = next;
		runtime->initialized = 0;
		runtime->library = source->library;
		runtime->library.next = next_library;
		runtime->library.vm = runtime_vm;
		init_list(&runtime->library.resources);
		if (source->public_library != NULL) {
			runtime->public_library_storage = *source->public_library;
			runtime->public_library = &runtime->public_library_storage;
			runtime->library.local_data = runtime;
		}
		source = source->next;
		runtime = next;
	}
}

static void reset_callback_frames(PocoActivation* activation)
{
	Func_frame* runtime = activation->callback_frames;

	while (runtime != NULL) {
		Func_frame* next = runtime->mlink;
		const Func_frame* source = runtime->compiled_frame;

		*runtime = *source;
		runtime->next = NULL;
		runtime->mlink = next;
		runtime->compiled_frame = source;
		runtime->activation = activation;
		runtime = next;
	}
}

static Func_frame* clone_callback_frames(const Func_frame* source, PocoActivation* activation)
{
	Func_frame* first = NULL;
	Func_frame* tail = NULL;

	while (source != NULL) {
		Func_frame* copy = malloc(sizeof(*copy));

		if (copy == NULL) {
			free_callback_frames(first);
			return NULL;
		}
		*copy = *source;
		copy->next = NULL;
		copy->mlink = NULL;
		copy->compiled_frame = source;
		copy->activation = activation;
		if (tail != NULL) {
			tail->mlink = copy;
		} else {
			first = copy;
		}
		tail = copy;
		source = source->mlink;
	}
	return first;
}

Errcode poco_activation_create(const PocoProgram* program, const Poco_program_code* code,
							   PocoVm* owner_vm, PocoActivation** out_activation)
{
	PocoActivation* activation;
	long stack_size;

	if (code == NULL || out_activation == NULL) {
		return Err_null_ref;
	}
	*out_activation = NULL;
	activation = calloc(1, sizeof(*activation));
	if (activation == NULL) {
		return Err_no_memory;
	}
	activation->program = program;
	activation->code = code;
	activation->vm = calloc(1, sizeof(*activation->vm));
	if (activation->vm == NULL) {
		goto OUT_OF_MEMORY;
	}
	if (owner_vm != NULL) {
		*activation->vm = *owner_vm;
	}
	activation->vm->activation = activation;
	activation->vm->last_error[0] = '\0';
	activation->pointer_registry =
		poco_pointer_registry_create_child(owner_vm != NULL ? owner_vm->pointer_registry : NULL);
	if (activation->pointer_registry == NULL) {
		goto OUT_OF_MEMORY;
	}
	activation->ffi.activation_magic = POCO_FFI_ACTIVATION_MAGIC;

	stack_size = code->stack_size != 0 ? code->stack_size : POCO_STACKSIZE_DEFAULT;
	activation->stack_size = stack_size;
	activation->data_size = code->data_size;
	activation->stack = calloc(1, (size_t)stack_size);
	if (activation->stack == NULL) {
		goto OUT_OF_MEMORY;
	}
	if (code->data_size > 0) {
		activation->data = calloc(1, (size_t)code->data_size);
		if (activation->data == NULL) {
			goto OUT_OF_MEMORY;
		}
	}
	activation->callback_frames = clone_callback_frames(code->prototypes, activation);
	if (code->prototypes != NULL && activation->callback_frames == NULL) {
		goto OUT_OF_MEMORY;
	}
	if (code->program_libraries != NULL) {
		activation->program_libraries = clone_program_libraries(
			code->program_libraries, activation->vm, &activation->builtin_libraries);
		if (activation->program_libraries == NULL) {
			goto OUT_OF_MEMORY;
		}
	} else {
		activation->builtin_libraries = clone_libraries(code->builtin_libraries, activation->vm);
		if (code->builtin_libraries != NULL && activation->builtin_libraries == NULL) {
			goto OUT_OF_MEMORY;
		}
	}
	activation->loaded_libraries = clone_libraries(code->loaded_libraries, activation->vm);
	if (code->loaded_libraries != NULL && activation->loaded_libraries == NULL) {
		goto OUT_OF_MEMORY;
	}
	if (po_ffi_activation_calls_create(activation) < Success) {
		goto OUT_OF_MEMORY;
	}
	if (poco_activation_reset_state(activation) < Success) {
		goto OUT_OF_MEMORY;
	}
	*out_activation = activation;
	return Success;

OUT_OF_MEMORY:
	poco_activation_destroy(activation);
	return Err_no_memory;
}

Errcode poco_activation_reset_state(PocoActivation* activation)
{
	if (activation == NULL || activation->code == NULL || activation->vm == NULL) {
		return Err_null_ref;
	}
	poco_activation_release_main_argv(activation);
	if (activation->libraries_initialized) {
		po_cleanup_libs(activation->loaded_libraries);
		po_cleanup_libs(activation->builtin_libraries);
		activation->libraries_initialized = 0;
	}
	if (activation->stack_size > 0) {
		memset(activation->stack, 0, (size_t)activation->stack_size);
	}
	if (activation->data_size > 0) {
		memset(activation->data, 0, (size_t)activation->data_size);
	}
	activation->check_abort = NULL;
	activation->check_abort_data = NULL;
	activation->trace_file = NULL;
	activation->err_line = NULL;
	activation->enable_debug_trace = true;
	memset(&activation->result, 0, sizeof(activation->result));
	activation->builtin_error = Success;
	activation->run_depth = 0;
	activation->debug_call_depth = 0;
	po_debug_activation_reset(activation);
	activation->findpoe_next = NULL;
	po_ffi_variadic_types_reset(&activation->variadic);
	po_ffi_activation_calls_reset(activation);
	reset_callback_frames(activation);
	if (activation->program_libraries != NULL) {
		reset_program_libraries(activation->code->program_libraries, activation->program_libraries,
								activation->vm);
	} else {
		reset_libraries(activation->code->builtin_libraries, activation->builtin_libraries,
						activation->vm);
	}
	reset_libraries(activation->code->loaded_libraries, activation->loaded_libraries,
					activation->vm);
	activation->vm->activation = activation;
	activation->vm->last_error[0] = '\0';
	activation->needs_reset = 0;
	activation->initialized = 0;
	return Success;
}

void poco_activation_destroy(PocoActivation* activation)
{
	if (activation == NULL) {
		return;
	}
	po_debug_activation_destroy(activation);
	poco_activation_release_main_argv(activation);
	if (activation->libraries_initialized) {
		po_cleanup_libs(activation->loaded_libraries);
		po_cleanup_libs(activation->builtin_libraries);
	}
	po_ffi_variadic_types_release(&activation->variadic);
	po_ffi_activation_calls_release(activation);
	poco_pointer_registry_destroy(activation->pointer_registry);
	free(activation->ffi.struct_result_allocation);
	free_callback_frames(activation->callback_frames);
	free_libraries(activation->loaded_libraries);
	if (activation->program_libraries != NULL) {
		free_program_libraries(activation->program_libraries);
	} else {
		free_libraries(activation->builtin_libraries);
	}
	free(activation->data);
	free(activation->stack);
	free(activation->vm);
	free(activation);
}

Func_frame* poco_activation_callback_handle(PocoActivation* activation,
											const Func_frame* compiled_frame)
{
	Func_frame* frame;

	if (activation == NULL || compiled_frame == NULL) {
		return NULL;
	}
	for (frame = activation->callback_frames; frame != NULL; frame = frame->mlink) {
		if (frame->compiled_frame == compiled_frame) {
			return frame;
		}
	}
	return NULL;
}

/*
 * Copy a run's private diagnostic message into the shared program VM so a host
 * that inspects the program after the activation returns sees the last error.
 */
void po_activation_publish_last_error(PocoActivation* activation)
{
	PocoVm* shared;

	if (activation == NULL || activation->vm == NULL || activation->program == NULL) {
		return;
	}
	shared = activation->program->vm;
	if (shared == NULL || activation->vm->last_error[0] == '\0') {
		return;
	}
	po_vm_diagnostic_lock(shared);
	memcpy(shared->last_error, activation->vm->last_error, sizeof(shared->last_error));
	po_vm_diagnostic_unlock(shared);
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
	po_activation_publish_last_error(activation);
	if (init_status != Success) {
		po_vm_report(vm, (PocoStatus)init_status, NULL, error_line, 0,
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
	PocoCallbackValue value = po_invalid_callback_value();
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
				value = po_invalid_callback_value();
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
	main_frame = po_activation_find_function(activation, "main");
	if (main_frame == NULL) {
		return (PocoStatus)Err_no_main;
	}
	returns_int = poco_type_is_exact(main_frame->return_type, TYPE_INT);
	if (!returns_int && !poco_type_is_exact(main_frame->return_type, TYPE_VOID)) {
		po_vm_report(activation->program->vm, POCO_STATUS_PARAMETER_RANGE, NULL, 0, 0,
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
			po_vm_report(activation->program->vm, POCO_STATUS_PARAMETER_RANGE, NULL, 0, 0,
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
	po_activation_publish_last_error(activation);
	if (status != POCO_STATUS_OK) {
		po_vm_report(activation->program->vm, status, NULL, error_line, 0,
					 "Poco main execution failed");
		return status;
	}
	if (out_result != NULL) {
		*out_result = returns_int ? result.i : 0;
	}
	return POCO_STATUS_OK;
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
	po_activation_publish_last_error(activation);
	if (run_status != Success) {
		po_vm_report(vm, (PocoStatus)run_status, NULL, error_line, 0,
					 "Poco program execution failed");
	}
	return (PocoStatus)run_status;
}

void poco_activation_release(PocoActivation* activation)
{
	poco_activation_destroy(activation);
}

/*****************************************************************************
 * clear an activation's data area.
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
 * alloc an activation's stack and data areas. run data init code.
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
const Func_frame* po_activation_find_function(const PocoActivation* activation, const char* name)
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

	if ((f = po_activation_find_function(activation, entry)) == NULL) {
		return (Err_no_main);
	}
	return po_run_ops(activation, f->code_pt, NULL);
}

static Errcode run_file_values(PocoActivation* activation, const char* entry,
							   const PocoCallbackValue* values, size_t value_count, Pt_num* result)
{
	const Func_frame* f;

	if ((f = po_activation_find_function(activation, entry)) == NULL) {
		return Err_no_main;
	}
	return po_run_ops_values(activation, f->code_pt, result, values, value_count);
}

/*****************************************************************************
 * run a given function from a poco program after init'ing the libs.
 ****************************************************************************/
Errcode po_activation_run_entry(PocoActivation* activation, const char* entry)
{
	PocoVm* previous_vm = poco_push_active_vm(activation->vm);
	Errcode err;

	if (!activation->libraries_initialized) {
		if ((err = po_activation_init_libs(activation)) < Success) {
			poco_pop_active_vm(previous_vm);
			return err;
		}
	}
	err = run_file(activation, entry);
	po_activation_cleanup_libs(activation);
	poco_pop_active_vm(previous_vm);
	return (err);
}

Errcode po_activation_run_entry_values(PocoActivation* activation, const char* entry,
									   const PocoCallbackValue* values, size_t value_count,
									   Pt_num* result)
{
	PocoVm* previous_vm = poco_push_active_vm(activation->vm);
	Errcode err;
	int top_level = activation->run_depth == 0;

	if (!activation->libraries_initialized) {
		if ((err = po_activation_init_libs(activation)) < Success) {
			poco_pop_active_vm(previous_vm);
			return err;
		}
	}
	err = run_file_values(activation, entry, values, value_count, result);
	if (top_level) {
		po_activation_cleanup_libs(activation);
	}
	poco_pop_active_vm(previous_vm);
	return err;
}
