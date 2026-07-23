/*******************************************************************************
 * activation.c - Per-run state for an immutable compiled Poco program.
 ******************************************************************************/

#include "activation.h"
#include "debug_internal.h"
#include "pocoload.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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
	activation->ffi_activation_magic = UINT64_C(0x504f434f46464941);

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
	free(activation->ffi_struct_result_allocation);
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
