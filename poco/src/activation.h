/*******************************************************************************
 * activation.h - Internal immutable-program/per-run-activation boundary.
 *
 * This header defines the immutable-program / per-run-activation ownership model.
 ******************************************************************************/

#ifndef POCO_ACTIVATION_H
#define POCO_ACTIVATION_H

#include "poco.h"
#include "standard_library.h"

struct Poco_registered_library;

/*
 * A run receives an activation-local view of its owner VM.  Registered
 * libraries, hooks, and the pointer registry remain borrowed services, while
 * the active activation and diagnostics are private to the run.
 */
struct PocoVm {
	Names* include_dirs;
	Names* library_dirs;
	Names* library_dirs_tail;
	struct Poco_registered_library* libraries;
	struct Poco_registered_library* libraries_tail;
	PocoDiagnosticCallback diagnostic_callback;
	void* diagnostic_user_data;
	int verbose;
	int poe_libraries_disabled;
	PocoModuleHooks module_hooks;
	char last_error[512];
	void* diagnostic_lock;
	PocoPointerRegistry* pointer_registry;
	struct PocoActivation* activation;
	int standard_library_registered;
	int trusted_graph_library_registered;
	int untrusted_expression_library_registered;
	int program_count;
	int destroy_requested;
};

typedef struct Poco_program_library {
	struct Poco_program_library* next;
	Poco_lib library;
	Lib_proto* prototypes;
	PocoLibrary* public_library;
	PocoLibrary public_library_storage;
	PocoLibraryRuntimeCleanup runtime_cleanup;
	int initialized;
} Poco_program_library;

/*
 * Compiled artifacts owned by PocoProgram.  Once compilation publishes a
 * program, every field below is read-only until program destruction.  The
 * pointees are const here so new run paths cannot accidentally use this type to
 * mutate shared code.  allocation_owner is an opaque lifetime token used only
 * when the program is destroyed; it is never per-run state.
 */
typedef struct Poco_program_code {
	long stack_size;
	long data_size;
	const Func_frame* functions;
	const Names* literals;
	const Func_frame* prototypes;
	const Po_FuncMap* ffi_bindings;
	const Poco_lib* builtin_libraries;
	const Poco_lib* loaded_libraries;
	const Poco_program_library* program_libraries;
	const void* allocation_owner;
} Poco_program_code;

/*
 * One independently allocatable execution of a const PocoProgram.
 *
 * Ownership:
 * - stack/data, callback_frames, runtime library lists, and variadic storage are
 *   activation-owned and must be reset or freed with the activation;
 * - program and vm are borrowed references whose owners outlive the activation;
 * - pointer_registry is an activation-owned borrowed-span overlay over the
 *   VM-owned registry;
 * - run options, result, builtin_error, and the POE walk cursor are values local
 *   to this activation.
 *
 * The runtime-library lists are activation-local copies of the immutable
 * definitions in Poco_program_code.  This keeps their embedded resource lists,
 * init/cleanup state, runtime local_data, module wrapper state, and list cursors
 * out of the shared program.  callback_frames similarly prevent
 * Func_frame::run_env from becoming a write into shared compiled frames.
 */
struct PocoActivation {
	const PocoProgram* program;
	const Poco_program_code* code;

	char* stack;
	long stack_size;
	char* data;
	long data_size;
	bool (*check_abort)(void* data);
	void* check_abort_data;
	const char* trace_file;
	long* err_line;
	PoBoolean enable_debug_trace;
	Pt_num result;
	Errcode builtin_error;
	unsigned int run_depth;
	size_t debug_call_depth;
	int needs_reset;
	int initialized;
	int libraries_initialized;
	void* main_argv_allocation;

	Po_FFI_Variadic_Descriptor variadic;
	uint64_t ffi_activation_magic;
	struct po_ffi_activation_call* ffi_calls;
	void* ffi_struct_result_allocation;
	void* ffi_struct_result;
	size_t ffi_struct_result_capacity;
	size_t ffi_fixed_call_prep_count;
	size_t ffi_variadic_call_prep_count;
	size_t ffi_per_call_allocation_count;
	size_t ffi_fixed_call_cache_hit_count;
	Func_frame* callback_frames;
	Poco_lib* builtin_libraries;
	Poco_lib* loaded_libraries;
	Poco_lib* findpoe_next;
	Poco_program_library* program_libraries;
	void (*debug_hook)(struct PocoActivation* activation, Code* instruction, void* stack_area,
					   void* frame_base);
	PocoDebugSession* debug_session;

	PocoVm* vm;
	PocoPointerRegistry* pointer_registry;
};

Errcode poco_activation_create(const PocoProgram* program, const Poco_program_code* code,
							   PocoVm* owner_vm, PocoActivation** out_activation);
void poco_activation_destroy(PocoActivation* activation);
Errcode poco_activation_reset_state(PocoActivation* activation);
Errcode poco_activation_marshal_main_argv(PocoActivation* activation, int argc, char* const* argv,
										  Popot* out_argv);
void poco_activation_release_main_argv(PocoActivation* activation);
Func_frame* poco_activation_callback_handle(PocoActivation* activation,
											const Func_frame* compiled_frame);

Errcode po_ffi_activation_calls_create(PocoActivation* activation);
void po_ffi_activation_calls_reset(PocoActivation* activation);
void po_ffi_activation_calls_release(PocoActivation* activation);

#endif /* POCO_ACTIVATION_H */
