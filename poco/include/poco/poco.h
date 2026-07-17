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

/*
 * An API version is encoded as major.minor.patch.  Code compiled against a
 * particular major version may use additions from newer minor versions only
 * after checking POCO_API_VERSION.  A major version change may break ABI.
 */
#define POCO_MAKE_API_VERSION(major, minor, patch) \
	((((major) & 0x3ffu) << 22) | (((minor) & 0x3ffu) << 12) | ((patch) & 0xfffu))
#define POCO_API_VERSION_MAJOR 1u
#define POCO_API_VERSION_MINOR 1u
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

enum
{
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
	POCO_STATUS_MODULE_EMPTY = -1305
};

/* Opaque ownership handles.  Hosts interact with these through API calls. */
typedef struct PocoVm PocoVm;
typedef struct PocoProgram PocoProgram;
typedef struct PocoModule PocoModule;

/*
 * Popot is intentionally public only because it crosses the native module
 * ABI.  Its three-pointer layout is stable for API major version 1; hosts
 * should otherwise use ordinary C pointers through the embedding API.
 */
typedef struct Popot
{
	void *pt;
	void *min;
	void *max;
} Popot;

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
typedef enum PocoPointerPermission
{
	POCO_POINTER_PERMISSION_NONE = 0,
	POCO_POINTER_PERMISSION_READ = 1u << 0,
	POCO_POINTER_PERMISSION_WRITE = 1u << 1
} PocoPointerPermission;

/* How a pointer parameter's required byte span is derived. */
typedef enum PocoBindingSpanKind
{
	POCO_BINDING_SPAN_BYTES,
	POCO_BINDING_SPAN_C_STRING,
	POCO_BINDING_SPAN_APPEND_C_STRING
} PocoBindingSpanKind;

/* The origin policy used to restore a Popot result after a raw libffi call. */
typedef enum PocoPointerReturnOrigin
{
	/* Uncontracted/explicitly trusted native returns are intentionally unbounded. */
	POCO_POINTER_RETURN_TRUSTED,
	/* The result must fall in one pointer argument's Popot span. */
	POCO_POINTER_RETURN_ALIAS,
	/* Poco owns and releases the result when the VM is destroyed. */
	POCO_POINTER_RETURN_OWNED,
	/* The result must be present in the VM's active borrowed-span registry. */
	POCO_POINTER_RETURN_BORROWED
} PocoPointerReturnOrigin;

typedef void (*PocoOwnedPointerRelease)(void *pointer, void *user_data);

/*
 * A contract for one fixed pointer parameter.  Parameter indexes are
 * zero-based.  For POCO_BINDING_SPAN_BYTES, byte_count is multiplied by the
 * optional integer-valued byte_count_parameter and element_count_parameter.
 * For C strings, string_parameter identifies the input to scan (normally this
 * parameter); append-string spans include the current destination string.
 */
typedef struct PocoBindingPointerContract
{
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
typedef struct PocoBindingReturnContract
{
	PocoPointerReturnOrigin origin;
	size_t alias_parameter;
	PocoBindingSpanKind span_kind;
	size_t byte_count;
	size_t byte_count_parameter;
	size_t element_count_parameter;
	size_t string_parameter;
	uint32_t permissions;
	PocoOwnedPointerRelease release;
	void *release_user_data;
} PocoBindingReturnContract;

typedef struct PocoBindingContract
{
	const PocoBindingPointerContract *pointer_contracts;
	size_t pointer_contract_count;
	PocoBindingReturnContract return_value;
} PocoBindingContract;

typedef struct PocoBinding
{
	const char *prototype;
	PocoNativeFunction function;
	/* Optional: a two-field initializer remains a trusted binding. */
	const PocoBindingContract *contract;
} PocoBinding;

typedef struct PocoLibrary PocoLibrary;

/*
 * Hooks run once around each program execution, in registration order.  Poco
 * calls cleanup after a completed, failed, or cancelled run when initialization
 * reached that library.  user_data is owned by the embedding host.
 */
typedef PocoStatus (*PocoLibraryInitialize)(PocoLibrary *library);
typedef void (*PocoLibraryCleanup)(PocoLibrary *library);

struct PocoLibrary
{
	const char *identity;
	const PocoBinding *bindings;
	size_t binding_count;
	PocoLibraryInitialize initialize;
	PocoLibraryCleanup cleanup;
	void *user_data;
};

/* A compiler or runtime diagnostic reported through PocoVmOptions. */
typedef struct PocoDiagnostic
{
	PocoStatus status;
	const char *source_name;
	long line;
	int column;
	const char *message;
} PocoDiagnostic;

typedef void (*PocoDiagnosticCallback)(void *user_data, const PocoDiagnostic *diagnostic);

/*
 * Native module ABI version 1.0.0.  A module descriptor must use this exact
 * version.  The descriptor is intentionally separate from POCO_API_VERSION:
 * the embedding API may grow without changing the dynamic-module ABI.
 */
#define POCO_MODULE_ABI_VERSION_MAJOR 1u
#define POCO_MODULE_ABI_VERSION_MINOR 0u
#define POCO_MODULE_ABI_VERSION_PATCH 0u
#define POCO_MODULE_ABI_VERSION \
	POCO_MAKE_API_VERSION(POCO_MODULE_ABI_VERSION_MAJOR, \
		POCO_MODULE_ABI_VERSION_MINOR, POCO_MODULE_ABI_VERSION_PATCH)
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
typedef void (*PocoModuleReportDiagnostic)(const PocoModuleHost *host,
	const PocoDiagnostic *diagnostic);
typedef const void *(*PocoModuleGetService)(const PocoModuleHost *host,
	const char *service_name, uint32_t service_abi_version);

struct PocoModuleHost
{
	uint32_t abi_version;
	void *user_data;
	PocoModuleReportDiagnostic report_diagnostic;
	PocoModuleGetService get_service;
};

typedef PocoStatus (*PocoModuleInitialize)(const PocoModuleHost *host,
	void **out_module_data);
typedef void (*PocoModuleCleanup)(void *module_data);

/*
 * A generic module contributes exactly one PocoLibrary.  initialize runs
 * once after descriptor validation; cleanup runs once before the dynamic
 * module is unloaded.  A module may use report_diagnostic during initialize
 * and may query host-defined services only through PocoModuleHost.  Generic
 * modules must not require Animator headers, globals, or Polib* tables.
 */
typedef struct PocoModuleDescriptor
{
	uint32_t abi_version;
	const char *identity;
	const PocoLibrary *library;
	PocoModuleInitialize initialize;
	PocoModuleCleanup cleanup;
} PocoModuleDescriptor;

typedef const PocoModuleDescriptor *(*PocoModuleEntryPoint)(void);

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
typedef struct PocoModuleInfo
{
	const char *requested_name;
	const char *resolved_path;
} PocoModuleInfo;

typedef PocoStatus (*PocoModuleLoadHook)(void *user_data,
	const PocoModuleInfo *module);
typedef void (*PocoModuleUnloadHook)(void *user_data,
	const PocoModuleInfo *module);

typedef struct PocoModuleHooks
{
	PocoModuleLoadHook on_load;
	PocoModuleUnloadHook on_unload;
	void *user_data;
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
typedef struct PocoVmOptions
{
	const char *const *include_paths;
	size_t include_path_count;
	PocoDiagnosticCallback diagnostic_callback;
	void *diagnostic_user_data;
	int verbose;
	const PocoModuleHooks *module_hooks;
} PocoVmOptions;

/* A cancellation callback returns non-zero to stop program execution. */
typedef int (*PocoCancelCallback)(void *user_data);

typedef struct PocoRunOptions
{
	PocoCancelCallback cancel_callback;
	void *cancel_user_data;
	const char *trace_file;
} PocoRunOptions;

/*
 * Create an empty VM.  Libraries are opt-in: call
 * poco_vm_register_standard_library() and/or poco_vm_register_library()
 * before compiling a program.  A registered library is copied, so the host
 * may release its descriptor and binding array after registration.  Poco's
 * compiler is not concurrently re-entrant; serialize compile/run calls.  The
 * VM/program lifecycle and this header are the public embedding boundary;
 * compile_poco(), Poco_lib, and related compatibility headers are not.
 */
PocoStatus poco_vm_create(const PocoVmOptions *options, PocoVm **out_vm);
void poco_vm_destroy(PocoVm *vm);

/*
 * Borrowed spans describe host-owned buffers that contracted bindings may
 * access.  Unregistering a span invalidates it immediately; the VM keeps a
 * tombstone until teardown so a stale Popot is rejected rather than trusted.
 */
PocoStatus poco_vm_register_borrowed_span(PocoVm *vm, void *pointer,
	size_t byte_count, uint32_t permissions);
PocoStatus poco_vm_unregister_borrowed_span(PocoVm *vm, const void *pointer);

/* Replace the VM include-path list.  The paths are copied and searched in order. */
PocoStatus poco_vm_set_include_paths(PocoVm *vm, const char *const *paths, size_t path_count);

/*
 * Register a host library in deterministic registration order.  identity and
 * every binding's prototype/function pair must be non-null, and a library
 * must contain at least one binding.  The standard
 * registry contains only host-neutral bindings and is explicitly opt-in.
 */
PocoStatus poco_vm_register_library(PocoVm *vm, const PocoLibrary *library);
PocoStatus poco_vm_register_standard_library(PocoVm *vm);

/* Compile a source file into a program owned by the caller. */
PocoStatus poco_vm_compile_file(PocoVm *vm, const char *source_name, PocoProgram **out_program);

/* Execute a program compiled by vm.  out_result may be NULL. */
PocoStatus poco_vm_run(PocoVm *vm, PocoProgram *program,
	const PocoRunOptions *options, int32_t *out_result);
void poco_program_destroy(PocoProgram *program);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* POCO_POCO_H */
