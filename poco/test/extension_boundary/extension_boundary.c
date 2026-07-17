#include <poco/poco.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "poco.h"

#define FIXTURE_PATH(name) POCO_EXTENSION_BOUNDARY_FIXTURE_DIR "/" name

static int failures;

#define CHECK(condition, message)                                         \
	do {                                                                  \
		if (!(condition)) {                                               \
			fprintf(stderr, "poco_extension_boundary: %s\\n", (message)); \
			++failures;                                                   \
		}                                                                 \
	} while (0)

static int fixed_int(int value)
{
	return value + 1;
}

static long fixed_long(long value)
{
	return value + 1;
}

static double fixed_double(double value)
{
	return value + 0.25;
}

static int* fixed_pointer(int* value)
{
	return value;
}

static int* fixed_popot(int* value)
{
	return value;
}

static void fixed_void(int* value)
{
	if (value != NULL) {
		*value = 52;
	}
}

static int mixed_variadic(int count, ...)
{
	va_list arguments;
	int integer_value;
	long long_value;
	double double_value;
	void* pointer_value;

	if (count == 0) {
		return 77;
	}
	if (count != 4) {
		return -1;
	}
	va_start(arguments, count);
	integer_value = va_arg(arguments, int);
	long_value = va_arg(arguments, long);
	double_value = va_arg(arguments, double);
	pointer_value = va_arg(arguments, void*);
	va_end(arguments);
	return integer_value == 7 && long_value == 123456789L && double_value == 1.5 &&
				   pointer_value == NULL
			   ? 99
			   : -2;
}

static int variadic_sum(int count, ...)
{
	va_list arguments;
	int index;
	int sum = 0;

	va_start(arguments, count);
	for (index = 0; index < count; ++index) {
		sum += va_arg(arguments, int);
	}
	va_end(arguments);
	return sum;
}

static void init_environment(Poco_run_env* environment, C_frame* binding)
{
	static C_frame root;
	static C_frame main_frame;

	memset(environment, 0, sizeof(*environment));
	memset(&root, 0, sizeof(root));
	memset(&main_frame, 0, sizeof(main_frame));
	root.next = &main_frame;
	root.mlink = &main_frame;
	main_frame.mlink = binding;
	environment->protos = &root;
}

static void init_binding(C_frame* frame, const char* name, void* function, Type_info* return_type,
						 Symbol* parameters, short parameter_count)
{
	memset(frame, 0, sizeof(*frame));
	frame->name = (char*)name;
	frame->code_pt = function;
	frame->type = CFF_C;
	frame->return_type = return_type;
	frame->parameters = parameters;
	frame->pcount = parameter_count;
}

static void test_descriptor_rejections(void)
{
	Type_info int_type = {0};
	Type_info function_type = {0};
	Symbol unsupported_parameter = {0};
	C_frame first;
	C_frame second;
	Poco_run_env environment;
	Po_FFI_Variadic_Descriptor variadic = {0};

	int_type.ido_type = IDO_INT;
	function_type.ido_type = IDO_VPT;
	unsupported_parameter.ti = &function_type;

	init_binding(&first, "null_binding", NULL, &int_type, NULL, 0);
	init_environment(&environment, &first);
	CHECK(po_ffi_build_structures(&environment) == Err_poco_ffi_invalid_binding,
		  "null native binding must be rejected");
	CHECK(environment.func_map == NULL, "null native binding must not leave descriptors behind");
	po_ffi_free_structures(&environment);

	init_binding(&first, "unsupported_binding", (void*)fixed_int, &int_type, &unsupported_parameter,
				 1);
	init_environment(&environment, &first);
	CHECK(po_ffi_build_structures(&environment) == Err_poco_ffi_invalid_binding,
		  "unsupported binding type must be rejected");
	CHECK(strstr(poco_get_error(), "unsupported parameter") != NULL,
		  "unsupported binding must report a diagnostic");
	CHECK(environment.func_map == NULL, "unsupported binding must not leave descriptors behind");
	po_ffi_free_structures(&environment);

	init_binding(&first, "duplicate_binding", (void*)fixed_int, &int_type, NULL, 0);
	init_binding(&second, "duplicate_binding", (void*)fixed_long, &int_type, NULL, 0);
	first.mlink = &second;
	init_environment(&environment, &first);
	CHECK(po_ffi_build_structures(&environment) == Err_poco_ffi_invalid_binding,
		  "duplicate binding name must be rejected");
	CHECK(environment.func_map == NULL, "duplicate binding name must clean up descriptors");
	po_ffi_free_structures(&environment);

	CHECK(po_ffi_variadic_types_append(&variadic, &ffi_type_float) == Err_poco_ffi_invalid_binding,
		  "unpromoted float variadic argument must be rejected");
	CHECK(strstr(poco_get_error(), "Unsupported variadic") != NULL,
		  "unsupported variadic type must report a diagnostic");
	CHECK(po_ffi_variadic_types_append(&variadic, &ffi_type_void) == Err_poco_ffi_invalid_binding,
		  "void variadic argument must be rejected");
	po_ffi_variadic_types_release(&variadic);
}

static int raw_pointer_payload;

static int raw_pointer_argument(void* value)
{
	return value == &raw_pointer_payload;
}

static void* raw_pointer_result(void)
{
	return &raw_pointer_payload;
}

static void test_raw_pointer_abi(void)
{
	Type_info int_type = {0};
	Type_info pointer_type = {0};
	Symbol parameter = {0};
	C_frame binding;
	Poco_run_env environment;
	Po_FFI* ffi_binding;
	Pt_num argument = {0};
	Pt_num result;

	int_type.ido_type = IDO_INT;
	pointer_type.ido_type = IDO_CPT;
	init_binding(&binding, "raw_pointer_result", (void*)raw_pointer_result, &pointer_type, NULL, 0);
	init_environment(&environment, &binding);
	CHECK(po_ffi_build_structures(&environment) == Success,
		  "raw pointer return descriptor must build");
	ffi_binding = po_ffi_find_binding_by_name(&environment, "raw_pointer_result");
	if (ffi_binding != NULL) {
		builtin_err = Success;
		result = po_ffi_call(ffi_binding, NULL, NULL, NULL);
		CHECK(builtin_err == Success, "raw pointer return must not set an FFI error");
		CHECK(result.p == raw_pointer_result(), "raw pointer return mapping");
	}
	po_ffi_free_structures(&environment);

	parameter.ti = &pointer_type;
	init_binding(&binding, "raw_pointer_argument", (void*)raw_pointer_argument, &int_type,
				 &parameter, 1);
	init_environment(&environment, &binding);
	CHECK(po_ffi_build_structures(&environment) == Success,
		  "raw pointer argument descriptor must build");
	ffi_binding = po_ffi_find_binding_by_name(&environment, "raw_pointer_argument");
	if (ffi_binding != NULL) {
		argument.p = &raw_pointer_payload;
		builtin_err = Success;
		result = po_ffi_call(ffi_binding, &argument, NULL, NULL);
		CHECK(builtin_err == Success, "raw pointer argument must not set an FFI error");
		CHECK(result.i == 1, "raw pointer argument mapping");
	}
	po_ffi_free_structures(&environment);
}

static const PocoBinding extension_bindings[] = {
	{"int FixedInt(int value);", (PocoNativeFunction)fixed_int},
	{"long FixedLong(long value);", (PocoNativeFunction)fixed_long},
	{"double FixedDouble(double value);", (PocoNativeFunction)fixed_double},
	{"int *FixedPointer(int *value);", (PocoNativeFunction)fixed_pointer},
	{"int *FixedPopot(int *value);", (PocoNativeFunction)fixed_popot},
	{"void FixedVoid(int *value);", (PocoNativeFunction)fixed_void},
	{"int MixedVariadic(int count, ...);", (PocoNativeFunction)mixed_variadic},
	{"int VariadicSum(int count, ...);", (PocoNativeFunction)variadic_sum},
};

static const PocoLibrary extension_library = {
	"extension-boundary",
	extension_bindings,
	sizeof(extension_bindings) / sizeof(extension_bindings[0]),
	NULL,
	NULL,
	NULL,
};

static void test_vm_lifecycles(void)
{
	int iteration;

	for (iteration = 0; iteration < 16; ++iteration) {
		PocoVm* vm = NULL;
		PocoProgram* program = NULL;
		int32_t result = -1;

		CHECK(poco_vm_create(NULL, &vm) == POCO_STATUS_OK, "create extension-boundary VM");
		if (vm == NULL) {
			continue;
		}
		CHECK(poco_vm_register_library(vm, &extension_library) == POCO_STATUS_OK,
			  "register extension-boundary bindings");
		CHECK(poco_vm_compile_file(vm, FIXTURE_PATH("abi_shapes.poc"), &program) == POCO_STATUS_OK,
			  "compile extension-boundary fixture");
		if (program != NULL) {
			CHECK(poco_vm_run(vm, program, NULL, &result) == POCO_STATUS_OK,
				  "run extension-boundary fixture");
			CHECK(result == 0, "extension-boundary fixture result");
		}
		poco_program_destroy(program);
		poco_vm_destroy(vm);
	}
}

static void test_public_null_binding(void)
{
	const PocoBinding bindings[] = {
		{"int NullBinding();", NULL},
	};
	const PocoLibrary library = {
		"null-binding", bindings, 1, NULL, NULL, NULL,
	};
	PocoVm* vm = NULL;

	CHECK(poco_vm_create(NULL, &vm) == POCO_STATUS_OK, "create VM for null binding");
	if (vm != NULL) {
		CHECK(poco_vm_register_library(vm, &library) == POCO_STATUS_PARAMETER_RANGE,
			  "public null binding must be rejected");
		poco_vm_destroy(vm);
	}
}

int main(void)
{
	test_descriptor_rejections();
	test_raw_pointer_abi();
	test_public_null_binding();
	test_vm_lifecycles();
	return failures == 0 ? 0 : 1;
}
