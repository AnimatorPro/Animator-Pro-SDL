#ifndef POCO_POCO_H
#define POCO_POCO_H

/*
 * Poco's stable embedding API.
 *
 * This header is self-contained: a host only needs the installed Poco include
 * directory to compile against it.  Internal compiler and Animator headers
 * are deliberately not part of this interface.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/*
 * An API version is encoded as major.minor.patch.  Code compiled against a
 * particular major version may use additions from newer minor versions only
 * after checking POCO_API_VERSION.  A major version change may break ABI.
 */
#define POCO_MAKE_API_VERSION(major, minor, patch) \
	((((major) & 0x3ffu) << 22) | (((minor) & 0x3ffu) << 12) | ((patch) & 0xfffu))
#define POCO_API_VERSION_MAJOR 2u
#define POCO_API_VERSION_MINOR 0u
#define POCO_API_VERSION_PATCH 0u
#define POCO_API_VERSION \
	POCO_MAKE_API_VERSION(POCO_API_VERSION_MAJOR, POCO_API_VERSION_MINOR, POCO_API_VERSION_PATCH)

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Status values returned by the Poco embedding API.  This is Poco's error
 * domain; hosts must translate these values to their own error domains at
 * their adapter boundary rather than sharing an Errcode definition.
 */
typedef int32_t PocoStatus;

enum {
	POCO_STATUS_OK = 0,
	POCO_STATUS_REPORTED = -1,
	POCO_STATUS_NULL_REFERENCE = -2,
	POCO_STATUS_INVALID_FILE = -3,
	POCO_STATUS_BUFFER_TOO_SMALL = -4,
	POCO_STATUS_OUT_OF_MEMORY = -5,
	POCO_STATUS_STRING_ERROR = -6,
	POCO_STATUS_PARAMETER_RANGE = -7,
	POCO_STATUS_TOO_FEW_PARAMETERS = -8,
	POCO_STATUS_TOO_MANY_PARAMETERS = -9,
	POCO_STATUS_CREATE_FAILED = -10,
	POCO_STATUS_ERROR_FILE = -11,
	POCO_STATUS_NOT_FOUND = -12,
	POCO_STATUS_NO_MAIN = -13,
	POCO_STATUS_INTERNAL_ERROR = -14,
	POCO_STATUS_FREE_NULL = -15,
	POCO_STATUS_FREE_FAILED = -16,
	POCO_STATUS_WRITE_FAILED = -17,
	POCO_STATUS_UNIMPLEMENTED = -18,
	POCO_STATUS_OVERFLOW = -19,
	POCO_STATUS_NO_FILE = -100,
	POCO_STATUS_NO_DEVICE = -102,
	POCO_STATUS_READ_FAILED = -104,
	POCO_STATUS_SEEK_FAILED = -105,
	POCO_STATUS_END_OF_FILE = -106,
	POCO_STATUS_ACCESS_DENIED = -118,
	POCO_STATUS_DIRECTORY_TOO_LONG = -143,
	POCO_STATUS_DIRECTORY_NAME = -144,
	POCO_STATUS_FILE_NAME = -145,
	POCO_STATUS_STACK_OVERFLOW = -402,
	POCO_STATUS_BAD_INSTRUCTION = -403,
	POCO_STATUS_SYNTAX = -404,
	POCO_STATUS_DIVIDE_BY_ZERO = -411,
	POCO_STATUS_FLOATING_POINT = -412,
	POCO_STATUS_INDEX_TOO_SMALL = -414,
	POCO_STATUS_INDEX_TOO_LARGE = -415,
	POCO_STATUS_FREE_RESOURCES = -418,
	POCO_STATUS_ZERO_ALLOCATION = -419,
	POCO_STATUS_READ_BUFFER = -421,
	POCO_STATUS_WRITE_BUFFER = -422,
	POCO_STATUS_EARLY_EXIT = -425,
	POCO_STATUS_FUNCTION_NOT_FOUND = -427,
	POCO_STATUS_EXIT = -429,
	POCO_STATUS_ABORTED = -430,
	POCO_STATUS_NOT_IMPLEMENTED = -551,
	POCO_STATUS_FFI_FUNCTION_NOT_FOUND = -1201,
	POCO_STATUS_FFI_NO_PROTOTYPES = -1202,
	POCO_STATUS_FFI_NO_FUNCTION_MAP = -1203,
	POCO_STATUS_FFI_FUNCTION_MAP_INSERT = -1204,
	POCO_STATUS_FFI_VARIADIC_OVERFLOW = -1205,
	POCO_STATUS_FFI_INVALID_BINDING = -1206,
	POCO_STATUS_FFI_BOUNDS = -1207,
	POCO_STATUS_MODULE_NOT_FOUND = -1300,
	POCO_STATUS_MODULE_LOAD_FAILED = -1301,
	POCO_STATUS_MODULE_NO_ENTRY = -1302,
	POCO_STATUS_MODULE_INVALID = -1303,
	POCO_STATUS_MODULE_VERSION = -1304,
	POCO_STATUS_MODULE_EMPTY = -1305,
	POCO_STATUS_IMAGE_TRUNCATED = -1400,
	POCO_STATUS_IMAGE_CORRUPT = -1401,
	POCO_STATUS_IMAGE_VERSION_MISMATCH = -1402
};

/* Opaque ownership handles.  Hosts interact with these through API calls. */
typedef struct PocoVm PocoVm;
typedef struct PocoProgram PocoProgram;
typedef struct PocoActivation PocoActivation;
typedef struct PocoCall PocoCall;
typedef struct PocoDebugSession PocoDebugSession;
typedef struct PocoModule PocoModule;

/* Debug metadata tier selected when a compiled program is serialized. */
typedef enum PocoDebugLevel {
	POCO_DEBUG_LEVEL_MINIMAL = 0,
	POCO_DEBUG_LEVEL_EXTENDED = 1
} PocoDebugLevel;

/* Source location reported while an activation is stopped in the debug hook. */
typedef struct PocoDebugLocation {
	const char* file;
	long line;
} PocoDebugLocation;

/* One active Poco call frame, ordered from the paused frame to its callers. */
typedef struct PocoDebugFrame {
	const char* function;
	long line;
} PocoDebugFrame;

/*
 * Called synchronously before the first opcode mapped to a breakpoint or
 * stepping destination line.
 * The interpreter remains paused, so the callback may query the session and
 * change its breakpoints before returning control to the program.
 */
typedef void (*PocoDebugPauseCallback)(PocoDebugSession* session, void* user_data);

/*
 * Popot is intentionally public only because it crosses the native module
 * ABI.  Its three-pointer layout has remained stable since API major version
 * 1; hosts should otherwise use ordinary C pointers through the embedding API.
 */
typedef struct Popot {
	void* pt;
	void* min;
	void* max;
} Popot;

/* Typed values used by host-driven global access and function invocation. */
typedef enum PocoCallbackValueKind {
	POCO_CALLBACK_VALUE_INT,
	POCO_CALLBACK_VALUE_LONG,
	POCO_CALLBACK_VALUE_DOUBLE,
	POCO_CALLBACK_VALUE_POPOT,
	POCO_CALLBACK_VALUE_INVALID
} PocoCallbackValueKind;

typedef struct PocoCallbackValue {
	PocoCallbackValueKind kind;

	union {
		int int_value;
		long long_value;
		double double_value;
		Popot popot_value;
	} value;
} PocoCallbackValue;

/*
 * A Poco binding is a C function exposed to a script through a Poco prototype
 * string.  The function must exactly match the prototype's fixed arguments
 * and return type.  The stable libffi ABI supports int, long, double, C
 * pointers, and void results; script pointers retain Popot bounds until their
 * final native conversion.  Cast a typed C function to PocoNativeFunction
 * when initializing this field.
 *
 * A variadic declaration must have at least one fixed argument before "...".
 * Poco records only the actual variadic values: C default promotions apply,
 * so char/short use int and float uses double.  Native-width int, long,
 * double, and pointer arguments are supported; unpromoted narrow integers,
 * float, and void are rejected before the native function is invoked.
 */
typedef void (*PocoNativeFunction)(void);

/* Use this for a contract field that does not refer to a function parameter. */
#define POCO_BINDING_PARAMETER_NONE ((size_t)-1)

/* A pointer span may be readable, writable, or both while it crosses libffi. */
typedef enum PocoPointerPermission {
	POCO_POINTER_PERMISSION_NONE = 0,
	POCO_POINTER_PERMISSION_READ = 1u << 0,
	POCO_POINTER_PERMISSION_WRITE = 1u << 1
} PocoPointerPermission;

/* How a pointer parameter's required byte span is derived. */
typedef enum PocoBindingSpanKind {
	POCO_BINDING_SPAN_BYTES,
	POCO_BINDING_SPAN_C_STRING,
	POCO_BINDING_SPAN_APPEND_C_STRING
} PocoBindingSpanKind;

/* The origin policy used to restore a Popot result after a raw libffi call. */
typedef enum PocoPointerReturnOrigin {
	/* Uncontracted/explicitly trusted native returns are intentionally unbounded. */
	POCO_POINTER_RETURN_TRUSTED,
	/* The result must fall in one pointer argument's Popot span. */
	POCO_POINTER_RETURN_ALIAS,
	/* Poco owns and releases the result when the VM is destroyed. */
	POCO_POINTER_RETURN_OWNED,
	/* The result must be present in the VM's active borrowed-span registry. */
	POCO_POINTER_RETURN_BORROWED
} PocoPointerReturnOrigin;

typedef void (*PocoOwnedPointerRelease)(void* pointer, void* user_data);

/*
 * A contract for one fixed pointer parameter.  Parameter indexes are
 * zero-based.  For POCO_BINDING_SPAN_BYTES, byte_count is multiplied by the
 * optional integer-valued byte_count_parameter and element_count_parameter.
 * For C strings, string_parameter identifies the input to scan (normally this
 * parameter); append-string spans include the current destination string.
 */
typedef struct PocoBindingPointerContract {
	size_t parameter_index;
	uint32_t permissions;
	uint32_t pointer_depth;
	PocoBindingSpanKind span_kind;
	size_t byte_count;
	size_t byte_count_parameter;
	size_t element_count_parameter;
	size_t string_parameter;
} PocoBindingPointerContract;

/*
 * The optional return contract restores bounds after the native function has
 * returned a raw pointer.  Size fields use the same rules as pointer spans.
 */
typedef struct PocoBindingReturnContract {
	PocoPointerReturnOrigin origin;
	size_t alias_parameter;
	PocoBindingSpanKind span_kind;
	size_t byte_count;
	size_t byte_count_parameter;
	size_t element_count_parameter;
	size_t string_parameter;
	uint32_t permissions;
	PocoOwnedPointerRelease release;
	void* release_user_data;
} PocoBindingReturnContract;

typedef struct PocoBindingContract {
	const PocoBindingPointerContract* pointer_contracts;
	size_t pointer_contract_count;
	PocoBindingReturnContract return_value;
} PocoBindingContract;

typedef struct PocoBinding {
	const char* prototype;
	PocoNativeFunction function;
	/* Optional: a two-field initializer remains a trusted binding. */
	const PocoBindingContract* contract;
	/*
	 * Optional flags.  A POCO_BINDING_RUN_CONTEXT function receives PocoVm*
	 * after every fixed argument declared by prototype.  For a variadic
	 * prototype, the PocoVm* is the final named C parameter immediately before
	 * "...".  Adding this field changed the public ABI in API major version 2;
	 * source initializers that omit it continue to select zero flags.
	 */
	uint32_t flags;
} PocoBinding;

enum { POCO_BINDING_RUN_CONTEXT = (1u << 0) };

typedef struct PocoLibrary PocoLibrary;

/*
 * Hooks run once around each program execution, in registration order.  Poco
 * calls cleanup after a completed, failed, or cancelled run when initialization
 * reached that library.  user_data is owned by the embedding host.
 */
typedef PocoStatus (*PocoLibraryInitialize)(PocoLibrary* library);
typedef void (*PocoLibraryCleanup)(PocoLibrary* library);

struct PocoLibrary {
	const char* identity;
	const PocoBinding* bindings;
	size_t binding_count;
	PocoLibraryInitialize initialize;
	PocoLibraryCleanup cleanup;
	void* user_data;
};

/* A compiler or runtime diagnostic reported through PocoVmOptions. */
typedef struct PocoDiagnostic {
	PocoStatus status;
	const char* source_name;
	long line;
	int column;
	const char* message;
} PocoDiagnostic;

typedef void (*PocoDiagnosticCallback)(void* user_data, const PocoDiagnostic* diagnostic);

/*
 * Native module ABI version 2.0.0.  A module descriptor must use this exact
 * version.  The descriptor is intentionally separate from POCO_API_VERSION:
 * the embedding API may grow without changing the dynamic-module ABI.
 * Version 2 adds PocoBinding.flags and therefore changes the binding-array
 * layout supplied by a module.
 */
#define POCO_MODULE_ABI_VERSION_MAJOR 2u
#define POCO_MODULE_ABI_VERSION_MINOR 0u
#define POCO_MODULE_ABI_VERSION_PATCH 0u
#define POCO_MODULE_ABI_VERSION                                                         \
	POCO_MAKE_API_VERSION(POCO_MODULE_ABI_VERSION_MAJOR, POCO_MODULE_ABI_VERSION_MINOR, \
						  POCO_MODULE_ABI_VERSION_PATCH)
#define POCO_MODULE_HOST_ABI_VERSION 1u

/* The sole entry point exported by a generic Poco native module. */
#define POCO_MODULE_ENTRY_POINT "poco_module_get"

#if defined(_WIN32)
#define POCO_MODULE_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define POCO_MODULE_EXPORT __attribute__((visibility("default")))
#else
#define POCO_MODULE_EXPORT
#endif

typedef struct PocoModuleHost PocoModuleHost;

/*
 * The loader supplies these services to module initialization.  get_service
 * returns an optional host-defined service whose requested ABI version must
 * match; it returns NULL when the service is unavailable.  No Animator
 * symbols, vtables, or globals are part of this contract.
 */
typedef void (*PocoModuleReportDiagnostic)(const PocoModuleHost* host,
										   const PocoDiagnostic* diagnostic);
typedef const void* (*PocoModuleGetService)(const PocoModuleHost* host, const char* service_name,
											uint32_t service_abi_version);

struct PocoModuleHost {
	uint32_t abi_version;
	void* user_data;
	PocoModuleReportDiagnostic report_diagnostic;
	PocoModuleGetService get_service;
};

typedef PocoStatus (*PocoModuleInitialize)(const PocoModuleHost* host, void** out_module_data);
typedef void (*PocoModuleCleanup)(void* module_data);

/*
 * A generic module contributes exactly one PocoLibrary.  initialize runs
 * once after descriptor validation; cleanup runs once before the dynamic
 * module is unloaded.  A module may use report_diagnostic during initialize
 * and may query host-defined services only through PocoModuleHost.  Generic
 * modules must not require Animator headers, globals, or Polib* tables.
 */
typedef struct PocoModuleDescriptor {
	uint32_t abi_version;
	const char* identity;
	const PocoLibrary* library;
	PocoModuleInitialize initialize;
	PocoModuleCleanup cleanup;
} PocoModuleDescriptor;

typedef const PocoModuleDescriptor* (*PocoModuleEntryPoint)(void);

/*
 * Module lifecycle policy belongs to the embedding host.  Poco invokes
 * on_load after the dynamic image is opened but before it resolves either
 * module ABI entry point.  Returning a non-OK PocoStatus rejects the module;
 * the loader emits that status as a diagnostic and closes the image.  If
 * on_load is absent, Poco accepts the image by default.  After an accepted
 * image, Poco invokes on_unload exactly once before closing the image,
 * including when later module validation fails.
 *
 * The strings in PocoModuleInfo are valid only for the callback.  Hooks must
 * copy any values they need after the callback returns.  The hook set is
 * copied by poco_vm_create() and applies to modules loaded while compiling
 * programs from that VM.  This gives hosts with different policies isolated
 * module lifecycles without adding host-specific symbols to Poco.
 */
typedef struct PocoModuleInfo {
	const char* requested_name;
	const char* resolved_path;
} PocoModuleInfo;

typedef PocoStatus (*PocoModuleLoadHook)(void* user_data, const PocoModuleInfo* module);
typedef void (*PocoModuleUnloadHook)(void* user_data, const PocoModuleInfo* module);

typedef struct PocoModuleHooks {
	PocoModuleLoadHook on_load;
	PocoModuleUnloadHook on_unload;
	void* user_data;
	/*
	 * Disabled by default.  Set this only in an Animator-owned compatibility
	 * host that deliberately loads a legacy poco_rexlib_get() POE module.
	 * It does not provide Animator symbols or function tables; the Ani host's
	 * on_load callback remains solely responsible for installing that ABI.
	 * Generic PocoModuleDescriptor modules neither need nor receive it.
	 */
	int allow_legacy_poe;
} PocoModuleHooks;

/*
 * Compatibility-only native modules export poco_rexlib_get() and return a
 * Pocorex from the legacy pocoload.h/pocorex.h headers.  The loader rejects
 * that format by default: an Animator-owned host must explicitly set
 * PocoModuleHooks.allow_legacy_poe and install its native table from on_load.
 * That opt-in does not provide Animator symbols or function tables to generic
 * modules.  New modules must export POCO_MODULE_ENTRY_POINT and use
 * PocoModuleDescriptor instead.
 */

/* Options copied by poco_vm_create(); include directories are searched in order. */
typedef struct PocoVmOptions {
	const char* const* include_paths;
	size_t include_path_count;
	PocoDiagnosticCallback diagnostic_callback;
	void* diagnostic_user_data;
	int verbose;
	const PocoModuleHooks* module_hooks;
} PocoVmOptions;

/* A cancellation callback returns non-zero to stop program execution. */
typedef int (*PocoCancelCallback)(void* user_data);

typedef struct PocoRunOptions {
	PocoCancelCallback cancel_callback;
	void* cancel_user_data;
	const char* trace_file;
} PocoRunOptions;

/*
 * Create an empty VM.  Libraries are opt-in: call
 * poco_vm_register_standard_library() and/or poco_vm_register_library()
 * before compiling a program.  A registered library is copied, so the host
 * may release its descriptor and binding array after registration.  Poco's
 * compiler and VM mutation APIs are not concurrently re-entrant; serialize
 * compilation, library/path registration, and VM teardown.  Distinct
 * activations of one immutable program may run concurrently as documented
 * below.  The VM/program lifecycle and this header are the public embedding
 * boundary; compile_poco(), Poco_lib, and related compatibility headers are
 * not.
 */
PocoStatus poco_vm_create(const PocoVmOptions* options, PocoVm** out_vm);
void poco_vm_destroy(PocoVm* vm);

/*
 * Borrowed spans describe host-owned buffers that contracted bindings may
 * access.  Unregistering a span invalidates it immediately; the VM keeps a
 * tombstone until teardown so a stale Popot is rejected rather than trusted.
 */
PocoStatus poco_vm_register_borrowed_span(PocoVm* vm, void* pointer, size_t byte_count,
										  uint32_t permissions);
PocoStatus poco_vm_unregister_borrowed_span(PocoVm* vm, const void* pointer);

/* Replace the VM include-path list.  The paths are copied and searched in order. */
PocoStatus poco_vm_set_include_paths(PocoVm* vm, const char* const* paths, size_t path_count);

/*
 * Append a directory to the VM's native .poe module search list.  The path is
 * copied and may include or omit its trailing directory separator.  Module
 * lookup searches the physical script directory (file compiles only), CWD,
 * the executable directory, and then these host-added directories in order.
 */
PocoStatus poco_vm_add_library_path(PocoVm* vm, const char* path);

/*
 * Permanently disable automatic loading of native .poe modules recorded in
 * serialized programs.  This is an opt-out safety control: loading is enabled
 * for a newly created VM, while a disabled VM refuses such an image before it
 * opens any module.
 */
void poco_vm_disable_poe_libraries(PocoVm* vm);

/*
 * Register a host library in deterministic registration order.  identity and
 * every binding's prototype/function pair must be non-null, and a library
 * must contain at least one binding.  The standard
 * registry contains only host-neutral bindings and is explicitly opt-in.
 */
PocoStatus poco_vm_register_library(PocoVm* vm, const PocoLibrary* library);
PocoStatus poco_vm_register_standard_library(PocoVm* vm);

/*
 * Register the trusted graph capability table.  This table contains only the
 * scalar ANSI math functions supplied by Poco.  Logic and bitwise operators
 * are language built-ins and therefore require no native binding.  Console,
 * string, memory, file, path, and time functions are deliberately absent.
 * Hosts provide vec3, quaternion, and matrix operations per rig as ordinary
 * IDO_STRUCT PocoBinding entries; they are not part of this table.
 */
PocoStatus poco_vm_register_trusted_graph_library(PocoVm* vm);

/*
 * Register the untrusted expression capability tier.  It exposes the same
 * scalar ANSI math functions and language logic/bitwise operators as the
 * trusted graph table, but native bindings with script-visible pointer
 * parameters or results are rejected when programs are compiled.  This also
 * makes the legacy permissive void* to Popot conversion unreachable.  The
 * restriction is selected solely by registering this table; Poco never
 * attempts to infer whether source text is trustworthy.
 */
PocoStatus poco_vm_register_untrusted_expression_library(PocoVm* vm);

/*
 * Compile source bytes into a program owned by the caller.  source_name is
 * copied into compiler metadata and used in diagnostics; it need not name a
 * filesystem object.  source_length excludes any terminating NUL byte, and
 * embedded NUL bytes are rejected.  Include directives search the VM's
 * host-configured include paths in order.  Native-module lookup never derives
 * a script directory from source_name.
 */
PocoStatus poco_vm_compile_buffer(PocoVm* vm, const char* source_name, const char* source,
								  size_t source_length, PocoProgram** out_program);

/* Read a source file and compile it through the shared buffer path.  Includes
 * search the source file's directory before the VM's host-configured paths. */
PocoStatus poco_vm_compile_file(PocoVm* vm, const char* source_name, PocoProgram** out_program);

/* Compile an ordered list of source files into one program image. Each file is
 * parsed as an independent translation unit; names and diagnostics retain the
 * corresponding entry from source_names. */
PocoStatus poco_vm_compile_files(PocoVm* vm, const char* const* source_names, size_t source_count,
								 PocoProgram** out_program);

/*
 * Serialize an immutable compiled program into a source-less, versioned
 * bytecode container.  The container retains the diagnostic source name and
 * BLAKE3 source-content address, but never the source bytes.  Minimal images
 * omit the source path; extended images retain the source path supplied to a
 * file compile.  Serializing an extended image from a program without a source
 * path returns POCO_STATUS_PARAMETER_RANGE.  For the memory form, out_size
 * always receives the required size.  Passing buffer as NULL, or a buffer
 * smaller than that size, returns POCO_STATUS_BUFFER_TOO_SMALL without writing
 * a partial image.  The file form writes at the FILE's current position and
 * does not close it.
 */
PocoStatus poco_program_serialize_buffer(const PocoProgram* program, PocoDebugLevel debug_level,
										 void* buffer, size_t buffer_size, size_t* out_size);
PocoStatus poco_program_serialize_file(const PocoProgram* program, PocoDebugLevel debug_level,
									   FILE* file);

/*
 * Deserialize a validated bytecode container using vm's registered native
 * bindings.  Both the image-integrity hash and the source content-address
 * hash (a fingerprint of the original source, not embedded source text) are
 * verified before the runnable program is returned.  Truncated,
 * corrupt, and version-mismatched images return POCO_STATUS_IMAGE_TRUNCATED,
 * POCO_STATUS_IMAGE_CORRUPT, and POCO_STATUS_IMAGE_VERSION_MISMATCH,
 * respectively.  The file form reads from the FILE's current position through
 * end-of-file and does not close it.
 */
PocoStatus poco_vm_deserialize_buffer(PocoVm* vm, const void* buffer, size_t buffer_size,
									  PocoProgram** out_program);
PocoStatus poco_vm_deserialize_file(PocoVm* vm, FILE* file, PocoProgram** out_program);

/* The returned pointer remains valid until the VM is destroyed or another
 * compile/run operation updates that VM's diagnostic message. */
const char* poco_get_last_error(PocoVm* vm);

/*
 * Acquire one mutable activation bound to program's immutable compiled code.
 * Acquiring allocates the activation's working storage once; reset retains
 * that storage so a host-owned pool can reuse it without recompiling or
 * allocating between runs.  A program, its VM, and the VM's registered
 * services must outlive every activation acquired from that program.
 *
 * Different activations of the same program may run concurrently.  A single
 * activation is owned by one host thread at a time; Poco deliberately does
 * not provide locking, a queue, or any other pooling policy.
 */
PocoStatus poco_activation_acquire(PocoProgram* program, PocoActivation** out_activation);

/*
 * Clear all per-run state while retaining the activation's allocated working
 * storage.  This discards globals and returns the activation to a needs-init
 * state without running global initializer code.  Call reset before re-running
 * an activation that has completed or failed.  Calling run twice without an
 * intervening reset is rejected.
 */
PocoStatus poco_activation_reset(PocoActivation* activation);

/*
 * Zero the activation's data segment and run its global initializer code.
 * Globals remain available for host access and later execution.  The explicit
 * host-driven ordering is init -> set globals -> run main -> get globals, with
 * reset between runs.  Main-only and named-call entry points do not auto-init.
 */
PocoStatus poco_activation_init(PocoActivation* activation);

/*
 * Seed or inspect a named program global in this activation's persistent data
 * segment. Scalars must use the matching value kind; pointer values must be
 * NULL or carry a valid bounded Popot span. Unsupported or unknown reads
 * return POCO_CALLBACK_VALUE_INVALID.
 */
PocoStatus poco_activation_set_global(PocoActivation* activation, const char* name,
									  PocoCallbackValue value);
PocoCallbackValue poco_activation_get_global(PocoActivation* activation, const char* name);

/*
 * Run main without reinitializing the activation. main may be declared with
 * no parameters or as main(int argc, char **argv); host strings are copied
 * into activation-owned bounded storage for the duration of the call. An int
 * return is copied to out_result, while a void return produces zero.
 */
PocoStatus poco_activation_run_main(PocoActivation* activation, int argc, char** argv,
									int32_t* out_result);

/*
 * Build and invoke one named script-function call against an activation's
 * current data segment. Numeric arguments are coerced to declared parameter
 * types at invoke time. Pointer arguments borrow an exact host span, which the
 * host must keep alive through invoke, and enforce its requested read/write
 * permissions. A call handle is one-shot and must be ended by its owner whether
 * invocation succeeds or fails. The activation and its program must outlive
 * the call handle.
 */
PocoStatus poco_call_begin(PocoActivation* activation, const char* name, PocoCall** out_call);
PocoStatus poco_call_push_int(PocoCall* call, int value);
PocoStatus poco_call_push_long(PocoCall* call, long value);
PocoStatus poco_call_push_double(PocoCall* call, double value);
PocoStatus poco_call_push_pointer(PocoCall* call, void* pointer, size_t byte_count,
								  PocoPointerPermission permissions);
PocoStatus poco_call_invoke(PocoCall* call, PocoCallbackValue* out_result);
void poco_call_end(PocoCall* call);

/*
 * Execute an acquired activation.  This backward-compatible convenience
 * performs poco_activation_init() followed by main.  out_result may be NULL.
 */
PocoStatus poco_activation_run(PocoActivation* activation, const PocoRunOptions* options,
							   int32_t* out_result);

/* Release an activation and its retained working storage. */
void poco_activation_release(PocoActivation* activation);

/*
 * Attach one debug session to an activation. Line breakpoints use the
 * program's immutable opcode-to-line tables and stop synchronously before the
 * selected line executes. The session is owned by the activation until it is
 * detached; releasing the activation also releases an attached session.
 */
PocoStatus poco_debug_attach(PocoActivation* activation, PocoDebugPauseCallback pause_callback,
							 void* user_data, PocoDebugSession** out_session);
void poco_debug_detach(PocoDebugSession* session);

/*
 * Return the executing opcode's source location. The file is the retained
 * source path when extended debug metadata provides one, otherwise the
 * diagnostic source name. Before execution reaches mapped code this returns
 * POCO_STATUS_NOT_FOUND.
 */
PocoStatus poco_debug_current_location(const PocoDebugSession* session,
									   PocoDebugLocation* out_location);

/* Add or remove a breakpoint at the first opcode mapped to line. */
PocoStatus poco_debug_add_line_breakpoint(PocoDebugSession* session, long line);
PocoStatus poco_debug_clear_line_breakpoint(PocoDebugSession* session, long line);

/*
 * Resume a paused session by stepping into the next source line, stepping
 * over calls to the next source line in the current frame, or continuing
 * until a breakpoint or program end. Step and next may also be armed before
 * the first run of a fresh or reset activation; they pause at its first mapped
 * source line. Resume controls are otherwise valid only from the synchronous
 * pause callback.
 */
PocoStatus poco_debug_step_into(PocoDebugSession* session);
PocoStatus poco_debug_next_over(PocoDebugSession* session);
PocoStatus poco_debug_continue(PocoDebugSession* session);

/*
 * Inspect the innermost live local or parameter named name in the paused
 * frame. The program must contain extended debug-local metadata. Unsupported
 * aggregate and array values return POCO_STATUS_PARAMETER_RANGE.
 */
PocoStatus poco_debug_read_variable(const PocoDebugSession* session, const char* name,
									PocoCallbackValue* out_value);

/*
 * Copy the active Poco frames, ordered from the paused frame outward. The
 * required count is always written to out_frame_count; an undersized or NULL
 * frame buffer returns POCO_STATUS_BUFFER_TOO_SMALL without a partial result.
 * Frame names are borrowed from the program and remain valid for its lifetime.
 */
PocoStatus poco_debug_backtrace(const PocoDebugSession* session, PocoDebugFrame* frames,
								size_t frame_capacity, size_t* out_frame_count);

/*
 * Convenience entry point that acquires, runs, and releases a temporary
 * activation for a program compiled by vm.  out_result may be NULL.
 */
PocoStatus poco_vm_run(PocoVm* vm, PocoProgram* program, const PocoRunOptions* options,
					   int32_t* out_result);
void poco_program_destroy(PocoProgram* program);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* POCO_POCO_H */
