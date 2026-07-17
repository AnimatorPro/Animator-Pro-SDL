#include <poco/poco.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#define FIXTURE_PATH(name) POCO_FFI_BOUNDS_FIXTURE_DIR "/" name
#define PARAMETER_NONE POCO_BINDING_PARAMETER_NONE

static PocoVm *borrowed_vm;
static unsigned char borrowed_bytes[4] = {3, 4, 5, 6};
static unsigned char trusted_bytes[1] = {9};
static int read_calls;
static int write_calls;
static int variadic_calls;
static int trusted_calls;
static int owned_release_calls;
static int failures;

static void report_diagnostic(void *user_data, const PocoDiagnostic *diagnostic)
{
	(void)user_data;
	fprintf(stderr, "poco_ffi_bounds diagnostic: %d %s\n", diagnostic->status,
		diagnostic->message != NULL ? diagnostic->message : "");
}

#define CHECK(condition, message) do { \
	if (!(condition)) { \
		fprintf(stderr, "poco_ffi_bounds: %s\n", message); \
		++failures; \
	} \
} while (0)

static int read_span(char *bytes, int count)
{
	++read_calls;
	return bytes != NULL && count != 0 ? (unsigned char)bytes[0] : 0;
}

static int write_span(char *bytes, int count)
{
	++write_calls;
	if (bytes != NULL && count != 0)
		bytes[count - 1] = 27;
	return 1;
}

static int read_variadic(char *bytes, int count, ...)
{
	va_list arguments;
	int value;

	++variadic_calls;
	if (bytes == NULL || count == 0)
		return 0;
	va_start(arguments, count);
	value = va_arg(arguments, int);
	va_end(arguments);
	return (unsigned char)bytes[0] + value;
}

static char *alias_span(char *bytes)
{
	return bytes;
}

static char *owned_span(int count)
{
	return calloc((size_t)count, 1);
}

static void release_owned_span(void *pointer, void *user_data)
{
	(void)user_data;
	++owned_release_calls;
	free(pointer);
}

static char *borrowed_span(void)
{
	return (char *)borrowed_bytes;
}

static int unregister_borrowed_span(void)
{
	return poco_vm_unregister_borrowed_span(borrowed_vm, borrowed_bytes);
}

static char *trusted_span(void)
{
	return (char *)trusted_bytes;
}

/* Intentionally does not dereference bytes: this is the trusted escape hatch. */
static int trusted_read(char *bytes, int count)
{
	(void)bytes;
	(void)count;
	++trusted_calls;
	return 77;
}

static int bad_depth(char *bytes)
{
	return bytes != NULL;
}

static const PocoBindingPointerContract read_pointer_contracts[] = {
	{
		.parameter_index = 0,
		.permissions = POCO_POINTER_PERMISSION_READ,
		.pointer_depth = 1,
		.span_kind = POCO_BINDING_SPAN_BYTES,
		.byte_count = 1,
		.byte_count_parameter = 1,
		.element_count_parameter = PARAMETER_NONE,
		.string_parameter = PARAMETER_NONE,
	},
};

static const PocoBindingPointerContract write_pointer_contracts[] = {
	{
		.parameter_index = 0,
		.permissions = POCO_POINTER_PERMISSION_WRITE,
		.pointer_depth = 1,
		.span_kind = POCO_BINDING_SPAN_BYTES,
		.byte_count = 1,
		.byte_count_parameter = 1,
		.element_count_parameter = PARAMETER_NONE,
		.string_parameter = PARAMETER_NONE,
	},
};

static const PocoBindingPointerContract alias_pointer_contracts[] = {
	{
		.parameter_index = 0,
		.permissions = POCO_POINTER_PERMISSION_READ,
		.pointer_depth = 1,
		.span_kind = POCO_BINDING_SPAN_BYTES,
		.byte_count = 0,
		.byte_count_parameter = PARAMETER_NONE,
		.element_count_parameter = PARAMETER_NONE,
		.string_parameter = PARAMETER_NONE,
	},
};

static const PocoBindingContract read_contract = {
	.pointer_contracts = read_pointer_contracts,
	.pointer_contract_count = sizeof(read_pointer_contracts) / sizeof(read_pointer_contracts[0]),
};

static const PocoBindingContract write_contract = {
	.pointer_contracts = write_pointer_contracts,
	.pointer_contract_count = sizeof(write_pointer_contracts) / sizeof(write_pointer_contracts[0]),
};

static const PocoBindingContract alias_contract = {
	.pointer_contracts = alias_pointer_contracts,
	.pointer_contract_count = sizeof(alias_pointer_contracts) / sizeof(alias_pointer_contracts[0]),
	.return_value = {
		.origin = POCO_POINTER_RETURN_ALIAS,
		.alias_parameter = 0,
	},
};

static const PocoBindingContract owned_contract = {
	.return_value = {
		.origin = POCO_POINTER_RETURN_OWNED,
		.span_kind = POCO_BINDING_SPAN_BYTES,
		.byte_count = 1,
		.byte_count_parameter = 0,
		.element_count_parameter = PARAMETER_NONE,
		.string_parameter = PARAMETER_NONE,
		.permissions = POCO_POINTER_PERMISSION_READ | POCO_POINTER_PERMISSION_WRITE,
		.release = release_owned_span,
	},
};

static const PocoBindingContract borrowed_contract = {
	.return_value = {
		.origin = POCO_POINTER_RETURN_BORROWED,
		.permissions = POCO_POINTER_PERMISSION_READ,
	},
};

static const PocoBindingPointerContract bad_depth_pointer_contracts[] = {
	{
		.parameter_index = 0,
		.permissions = POCO_POINTER_PERMISSION_READ,
		.pointer_depth = 2,
		.span_kind = POCO_BINDING_SPAN_BYTES,
		.byte_count = 1,
		.byte_count_parameter = PARAMETER_NONE,
		.element_count_parameter = PARAMETER_NONE,
		.string_parameter = PARAMETER_NONE,
	},
};

static const PocoBindingContract bad_depth_contract = {
	.pointer_contracts = bad_depth_pointer_contracts,
	.pointer_contract_count = sizeof(bad_depth_pointer_contracts) /
		sizeof(bad_depth_pointer_contracts[0]),
};

static const PocoBinding bindings[] = {
	{"int read_span(char *bytes, int count);", (PocoNativeFunction)read_span, &read_contract},
	{"int write_span(char *bytes, int count);", (PocoNativeFunction)write_span, &write_contract},
	{"int read_variadic(char *bytes, int count, ...);", (PocoNativeFunction)read_variadic, &read_contract},
	{"char *alias_span(char *bytes);", (PocoNativeFunction)alias_span, &alias_contract},
	{"char *owned_span(int count);", (PocoNativeFunction)owned_span, &owned_contract},
	{"char *borrowed_span();", (PocoNativeFunction)borrowed_span, &borrowed_contract},
	{"int unregister_borrowed_span();", (PocoNativeFunction)unregister_borrowed_span, NULL},
	{"char *trusted_span();", (PocoNativeFunction)trusted_span, NULL},
	{"int trusted_read(char *bytes, int count);", (PocoNativeFunction)trusted_read, NULL},
};

static const PocoLibrary library = {
	"ffi-bounds", bindings, sizeof(bindings) / sizeof(bindings[0]), NULL, NULL, NULL,
};

static const PocoBinding bad_depth_bindings[] = {
	{"int bad_depth(char *bytes);", (PocoNativeFunction)bad_depth, &bad_depth_contract},
};

static const PocoLibrary bad_depth_library = {
	"ffi-bad-depth", bad_depth_bindings,
	sizeof(bad_depth_bindings) / sizeof(bad_depth_bindings[0]), NULL, NULL, NULL,
};

static PocoStatus run_fixture(PocoVm *vm, const char *name, int32_t *out_result)
{
	PocoProgram *program = NULL;
	PocoStatus status;
	char source_name[512];

	snprintf(source_name, sizeof(source_name), "%s/%s",
		POCO_FFI_BOUNDS_FIXTURE_DIR, name);
	status = poco_vm_compile_file(vm, source_name, &program);
	if (status == POCO_STATUS_OK)
		status = poco_vm_run(vm, program, NULL, out_result);
	if (status != POCO_STATUS_OK)
		fprintf(stderr, "poco_ffi_bounds: %s returned %d\n", name, status);
	poco_program_destroy(program);
	return status;
}

static PocoVm *make_vm(int register_borrowed)
{
	PocoVm *vm = NULL;
	PocoVmOptions options = {0};

	options.diagnostic_callback = report_diagnostic;

	CHECK(poco_vm_create(&options, &vm) == POCO_STATUS_OK, "create bounds VM");
	if (vm == NULL)
		return NULL;
	CHECK(poco_vm_register_library(vm, &library) == POCO_STATUS_OK,
		"register contracted bindings");
	if (register_borrowed) {
		CHECK(poco_vm_register_borrowed_span(vm, borrowed_bytes, sizeof(borrowed_bytes),
			POCO_POINTER_PERMISSION_READ) == POCO_STATUS_OK,
			"register readable borrowed span");
	}
	borrowed_vm = vm;
	return vm;
}

static void test_success_and_rejections(void)
{
	PocoVm *vm;
	int32_t result = 0;

	read_calls = write_calls = variadic_calls = 0;
	vm = make_vm(0);
	if (vm != NULL) {
		CHECK(run_fixture(vm, "success.poc", &result) == POCO_STATUS_OK,
			"contracted readable/writable fixed and variadic calls succeed");
		CHECK(result == 107, "success fixture result");
		CHECK(read_calls == 1 && write_calls == 1 && variadic_calls == 1,
			"success fixture reached every contracted native binding");
		poco_vm_destroy(vm);
	}

	read_calls = 0;
	vm = make_vm(0);
	if (vm != NULL) {
		CHECK(run_fixture(vm, "read_oob.poc", NULL) == POCO_STATUS_FFI_BOUNDS,
			"fixed read out of bounds is rejected before native code");
		CHECK(read_calls == 0, "out-of-bounds read did not enter native code");
		poco_vm_destroy(vm);
	}

	write_calls = 0;
	vm = make_vm(0);
	if (vm != NULL) {
		CHECK(run_fixture(vm, "write_oob.poc", NULL) == POCO_STATUS_FFI_BOUNDS,
			"fixed write out of bounds is rejected before native code");
		CHECK(write_calls == 0, "out-of-bounds write did not enter native code");
		poco_vm_destroy(vm);
	}

	variadic_calls = 0;
	vm = make_vm(0);
	if (vm != NULL) {
		CHECK(run_fixture(vm, "variadic_oob.poc", NULL) == POCO_STATUS_FFI_BOUNDS,
			"variadic binding validates its fixed pointer span before native code");
		CHECK(variadic_calls == 0, "out-of-bounds variadic call did not enter native code");
		poco_vm_destroy(vm);
	}

	read_calls = 0;
	vm = make_vm(0);
	if (vm != NULL) {
		CHECK(run_fixture(vm, "null_pointer.poc", NULL) == POCO_STATUS_FFI_BOUNDS,
			"null pointer with nonzero read span is rejected");
		CHECK(read_calls == 0, "null pointer did not enter native code");
		poco_vm_destroy(vm);
	}
}

static void test_return_origins_and_lifetime(void)
{
	PocoVm *vm;

	write_calls = 0;
	vm = make_vm(0);
	if (vm != NULL) {
		CHECK(run_fixture(vm, "alias_oob.poc", NULL) == POCO_STATUS_FFI_BOUNDS,
			"alias return preserves input bounds for a later contracted write");
		CHECK(write_calls == 0, "alias out-of-bounds write did not enter native code");
		poco_vm_destroy(vm);
	}

	owned_release_calls = 0;
	write_calls = 0;
	vm = make_vm(0);
	if (vm != NULL) {
		CHECK(run_fixture(vm, "owned_oob.poc", NULL) == POCO_STATUS_FFI_BOUNDS,
			"owned return records its derived allocation bounds");
		CHECK(write_calls == 0, "owned out-of-bounds write did not enter native code");
		poco_vm_destroy(vm);
		CHECK(owned_release_calls == 1, "VM teardown releases owned native returns exactly once");
	}

	read_calls = 0;
	vm = make_vm(1);
	if (vm != NULL) {
		CHECK(run_fixture(vm, "borrowed_success.poc", NULL) == POCO_STATUS_OK,
			"registered borrowed return retains readable bounds");
		CHECK(read_calls == 1, "borrowed span reached native code while registered");
		poco_vm_destroy(vm);
	}

	read_calls = 0;
	vm = make_vm(1);
	if (vm != NULL) {
		CHECK(run_fixture(vm, "stale_borrowed.poc", NULL) == POCO_STATUS_FFI_BOUNDS,
			"unregistered borrowed span is rejected after its Popot was retained");
		CHECK(read_calls == 0, "stale borrowed span did not enter native code");
		poco_vm_destroy(vm);
	}
}

static void test_trusted_boundary(void)
{
	PocoVm *vm;
	int32_t result = 0;

	trusted_calls = 0;
	vm = make_vm(0);
	if (vm != NULL) {
		CHECK(run_fixture(vm, "trusted_raw.poc", &result) == POCO_STATUS_OK,
			"an uncontracted trusted binding remains callable");
		CHECK(result == 77 && trusted_calls == 1,
			"trusted native code remains intentionally outside Poco span enforcement");
		poco_vm_destroy(vm);
	}
}

static void test_standard_string_contracts(void)
{
	PocoVm *vm = NULL;

	CHECK(poco_vm_create(NULL, &vm) == POCO_STATUS_OK,
		"create VM for standard string contracts");
	if (vm == NULL)
		return;
	CHECK(poco_vm_register_standard_library(vm) == POCO_STATUS_OK,
		"register standard string library");
	if (failures == 0) {
		CHECK(run_fixture(vm, "standard_snprintf_oob.poc", NULL) == POCO_STATUS_FFI_BOUNDS,
			"bounded formatting rejects a destination span that is too small");
		CHECK(run_fixture(vm, "standard_strcpy_oob.poc", NULL) == POCO_STATUS_FFI_BOUNDS,
			"strcpy scans its source within bounds and rejects a short destination");
		CHECK(run_fixture(vm, "standard_strcat_oob.poc", NULL) == POCO_STATUS_FFI_BOUNDS,
			"strcat validates existing destination plus source before native code");
	}
	poco_vm_destroy(vm);
}

static void test_pointer_depth_validation(void)
{
	PocoVm *vm = NULL;
	PocoProgram *program = NULL;

	CHECK(poco_vm_create(NULL, &vm) == POCO_STATUS_OK,
		"create VM for pointer-depth validation");
	if (vm == NULL)
		return;
	CHECK(poco_vm_register_library(vm, &bad_depth_library) == POCO_STATUS_OK,
		"register bad-depth contract for compile validation");
	CHECK(poco_vm_compile_file(vm, FIXTURE_PATH("no_bindings.poc"), &program) ==
		POCO_STATUS_FFI_INVALID_BINDING,
		"contract pointer depth must match its declared native pointer depth");
	poco_program_destroy(program);
	poco_vm_destroy(vm);
}

int main(void)
{
	test_success_and_rejections();
	test_return_origins_and_lifetime();
	test_trusted_boundary();
	test_standard_string_contracts();
	test_pointer_depth_validation();
	return failures == 0 ? 0 : 1;
}
