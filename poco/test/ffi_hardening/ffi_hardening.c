#include <poco/poco.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "poco.h"

#define FIXTURE_PATH(name) POCO_FFI_HARDENING_FIXTURE_DIR "/" name

static int failures;

#define CHECK(condition, message) \
	do { \
		if (!(condition)) { \
			fprintf(stderr, "poco_ffi_hardening: %s\n", (message)); \
			++failures; \
		} \
	} while (0)

static int fixture_function(void)
{
	return 42;
}

static int fixture_add(int left, int right)
{
	return left + right;
}

enum { FIXTURE_DYNAMIC_ARGUMENT_COUNT = 20 };

static int fixture_sum_20(int a01, int a02, int a03, int a04, int a05,
	int a06, int a07, int a08, int a09, int a10,
	int a11, int a12, int a13, int a14, int a15,
	int a16, int a17, int a18, int a19, int a20)
{
	return a01 + a02 + a03 + a04 + a05 + a06 + a07 + a08 + a09 + a10 +
		a11 + a12 + a13 + a14 + a15 + a16 + a17 + a18 + a19 + a20;
}

static int fixture_variadic_sum(int count, ...)
{
	int index;
	int sum = 0;
	va_list arguments;

	va_start(arguments, count);
	for (index = 0; index < count; ++index) {
		sum += va_arg(arguments, int);
	}
	va_end(arguments);
	return sum;
}

static long fixture_long_result(void)
{
	return 123456789L;
}

static double fixture_double_result(void)
{
	return 1234.5;
}

static int fixture_pointer_payload;

static void* fixture_pointer_result(void)
{
	return &fixture_pointer_payload;
}

static int fixture_void_called;

static void fixture_void_result(void)
{
	fixture_void_called = 1;
}

typedef union fixture_stack
{
	double double_alignment;
	void* pointer_alignment;
	unsigned char bytes[sizeof(int) +
		(FIXTURE_DYNAMIC_ARGUMENT_COUNT * sizeof(int))];
} FixtureStack;

static void init_environment(Poco_run_env* env, C_frame* binding)
{
	static C_frame root;
	static C_frame main_frame;

	memset(env, 0, sizeof(*env));
	memset(&root, 0, sizeof(root));
	memset(&main_frame, 0, sizeof(main_frame));
	root.next = &main_frame;
	root.mlink = &main_frame;
	main_frame.mlink = binding;
	env->protos = &root;
}

static void init_binding(C_frame* frame, const char* name, void* function,
	Type_info* return_type, Symbol* parameters, short parameter_count)
{
	memset(frame, 0, sizeof(*frame));
	frame->name = (char*)name;
	frame->code_pt = function;
	frame->type = CFF_C;
	frame->return_type = return_type;
	frame->parameters = parameters;
	frame->pcount = parameter_count;
}

static void check_invalid_binding(const char* label, C_frame* binding)
{
	Poco_run_env env;

	init_environment(&env, binding);
	CHECK(po_ffi_build_structures(&env) == Err_poco_ffi_invalid_binding, label);
	CHECK(env.func_map == NULL, "rejected descriptor must not retain a function map");
	po_ffi_free_structures(&env);
}

static void test_descriptor_validation(void)
{
	Type_info int_type = {0};
	Type_info function_type = {0};
	Symbol unsupported_parameter = {0};
	C_frame binding;

	int_type.ido_type = IDO_INT;
	function_type.ido_type = IDO_VPT;
	unsupported_parameter.ti = &function_type;

	init_binding(&binding, "null_function", NULL, &int_type, NULL, 0);
	check_invalid_binding("null function must be rejected", &binding);

	init_binding(&binding, "unsupported_parameter", (void*)fixture_function,
		&int_type, &unsupported_parameter, 1);
	check_invalid_binding("unsupported parameter type must be rejected", &binding);

	init_binding(&binding, "invalid_return", (void*)fixture_function,
		&function_type, NULL, 0);
	check_invalid_binding("unsupported return type must be rejected", &binding);
}

static void test_duplicate_descriptors(void)
{
	Type_info int_type = {0};
	C_frame first;
	C_frame second;
	Poco_run_env env;

	int_type.ido_type = IDO_INT;
	init_binding(&first, "duplicate_name", (void*)fixture_function, &int_type, NULL, 0);
	init_binding(&second, "duplicate_name", (void*)fixture_add, &int_type, NULL, 0);
	first.mlink = &second;
	init_environment(&env, &first);
	CHECK(po_ffi_build_structures(&env) == Err_poco_ffi_invalid_binding,
		"duplicate function names must be rejected");
	CHECK(env.func_map == NULL, "duplicate names must not leave a partial map");
	po_ffi_free_structures(&env);

	init_binding(&first, "first_address", (void*)fixture_function, &int_type, NULL, 0);
	init_binding(&second, "second_address", (void*)fixture_function, &int_type, NULL, 0);
	first.mlink = &second;
	init_environment(&env, &first);
	CHECK(po_ffi_build_structures(&env) == Err_poco_ffi_invalid_binding,
		"duplicate function addresses must be rejected");
	CHECK(env.func_map == NULL, "duplicate addresses must not leave a partial map");
	po_ffi_free_structures(&env);
}

static Po_FFI* build_binding(Poco_run_env* env, C_frame* binding, const char* message)
{
	init_environment(env, binding);
	CHECK(po_ffi_build_structures(env) == Success, message);
	return po_ffi_find_binding_by_name(env, binding->name);
}

static void write_int(FixtureStack* stack, size_t offset, int value)
{
	memcpy(stack->bytes + offset, &value, sizeof(value));
}

static void test_dynamic_call_storage(void)
{
	Type_info int_type = {0};
	Type_info ellipsis_type = {0};
	TypeComp ellipsis_components[1] = {0};
	Symbol fixed_parameters[FIXTURE_DYNAMIC_ARGUMENT_COUNT] = {0};
	Symbol variadic_parameters[2] = {0};
	C_frame binding;
	Poco_run_env env;
	Po_FFI* ffi_binding;
	FixtureStack stack = {0};
	Po_FFI_Variadic_Descriptor variadic = {0};
	Pt_num result;
	int index;
	int expected_sum = 0;
	size_t offset;

	int_type.ido_type = IDO_INT;
	for (index = 0; index < FIXTURE_DYNAMIC_ARGUMENT_COUNT; ++index) {
		fixed_parameters[index].ti = &int_type;
		fixed_parameters[index].link = index + 1 < FIXTURE_DYNAMIC_ARGUMENT_COUNT ?
			&fixed_parameters[index + 1] : NULL;
		expected_sum += index + 1;
		write_int(&stack, (size_t)index * sizeof(int), index + 1);
	}
	init_binding(&binding, "dynamic_fixed_arguments", (void*)fixture_sum_20,
		&int_type, fixed_parameters, FIXTURE_DYNAMIC_ARGUMENT_COUNT);
	ffi_binding = build_binding(&env, &binding,
		"more than sixteen fixed arguments must build safely");
	if (ffi_binding != NULL) {
		builtin_err = Success;
		result = po_ffi_call(ffi_binding, (const Pt_num*)stack.bytes, NULL, NULL);
		CHECK(builtin_err == Success, "dynamic fixed-argument call must not set an FFI error");
		CHECK(result.i == expected_sum, "dynamic fixed-argument call result");
	}
	po_ffi_free_structures(&env);

	memset(&stack, 0, sizeof(stack));
	ellipsis_type.comp = ellipsis_components;
	ellipsis_type.comp_count = 1;
	ellipsis_type.comp[0] = TYPE_ELLIPSIS;
	variadic_parameters[0].ti = &int_type;
	variadic_parameters[0].link = &variadic_parameters[1];
	variadic_parameters[1].ti = &ellipsis_type;
	offset = 0;
	write_int(&stack, offset, FIXTURE_DYNAMIC_ARGUMENT_COUNT);
	offset += sizeof(int);
	for (index = 0; index < FIXTURE_DYNAMIC_ARGUMENT_COUNT; ++index) {
		write_int(&stack, offset + ((size_t)index * sizeof(int)), index + 1);
		CHECK(po_ffi_variadic_types_append(&variadic, &ffi_type_sint32) == Success,
			"variadic descriptor must record each actual argument type");
	}
	init_binding(&binding, "dynamic_variadic_arguments", (void*)fixture_variadic_sum,
		&int_type, variadic_parameters, 2);
	ffi_binding = build_binding(&env, &binding,
		"fixed-plus-variadic descriptor must build safely");
	if (ffi_binding != NULL) {
		builtin_err = Success;
		result = po_ffi_call(ffi_binding, (const Pt_num*)stack.bytes, &variadic, NULL);
		CHECK(builtin_err == Success, "dynamic fixed-plus-variadic call must not set an FFI error");
		CHECK(result.i == expected_sum, "dynamic fixed-plus-variadic call result");
	}
	po_ffi_variadic_types_release(&variadic);
	po_ffi_free_structures(&env);

	memset(&stack, 0, sizeof(stack));
	write_int(&stack, 0, 0);
	init_binding(&binding, "invalid_variadic_count", (void*)fixture_variadic_sum,
		&int_type, variadic_parameters, 2);
	ffi_binding = build_binding(&env, &binding,
		"zero-variadic descriptor must build safely");
	if (ffi_binding != NULL) {
		builtin_err = Success;
		(void)po_ffi_call(ffi_binding, (const Pt_num*)stack.bytes, &variadic, NULL);
		CHECK(builtin_err == Success,
			"zero variadic arguments must not require a hidden stack count");
	}
	po_ffi_free_structures(&env);

	CHECK(po_ffi_variadic_types_append(&variadic, &ffi_type_void) ==
		Err_poco_ffi_invalid_binding,
		"void must be rejected as an unsupported variadic promotion");
	CHECK(po_ffi_variadic_types_append(&variadic, &ffi_type_float) ==
		Err_poco_ffi_invalid_binding,
		"float must be rejected until promoted to double for a variadic call");
	if (sizeof(int) > sizeof(int8_t)) {
		CHECK(po_ffi_variadic_types_append(&variadic, &ffi_type_sint8) ==
			Err_poco_ffi_invalid_binding,
			"narrow integers must be rejected until promoted for a variadic call");
	}
	po_ffi_variadic_types_release(&variadic);
}

static void test_abi_safe_return_storage(void)
{
	Type_info int_type = {0};
	Type_info long_type = {0};
	Type_info double_type = {0};
	Type_info pointer_type = {0};
	Type_info void_type = {0};
	C_frame binding;
	Poco_run_env env;
	Po_FFI* ffi_binding;
	Pt_num result;

	int_type.ido_type = IDO_INT;
	long_type.ido_type = IDO_LONG;
	double_type.ido_type = IDO_DOUBLE;
	pointer_type.ido_type = IDO_POINTER;
	void_type.ido_type = IDO_VOID;

	init_binding(&binding, "int_return", (void*)fixture_function, &int_type, NULL, 0);
	ffi_binding = build_binding(&env, &binding, "int return descriptor must build");
	if (ffi_binding != NULL) {
		result = po_ffi_call(ffi_binding, NULL, NULL, NULL);
		CHECK(result.i == 42, "int return mapping");
	}
	po_ffi_free_structures(&env);

	init_binding(&binding, "long_return", (void*)fixture_long_result, &long_type, NULL, 0);
	ffi_binding = build_binding(&env, &binding, "long return descriptor must build");
	if (ffi_binding != NULL) {
		result = po_ffi_call(ffi_binding, NULL, NULL, NULL);
		CHECK(result.l == fixture_long_result(), "long return mapping");
	}
	po_ffi_free_structures(&env);

	init_binding(&binding, "double_return", (void*)fixture_double_result, &double_type, NULL, 0);
	ffi_binding = build_binding(&env, &binding, "double return descriptor must build");
	if (ffi_binding != NULL) {
		result = po_ffi_call(ffi_binding, NULL, NULL, NULL);
		CHECK(result.d == fixture_double_result(), "double return mapping");
	}
	po_ffi_free_structures(&env);

	init_binding(&binding, "pointer_return", (void*)fixture_pointer_result, &pointer_type, NULL, 0);
	ffi_binding = build_binding(&env, &binding, "pointer return descriptor must build");
	if (ffi_binding != NULL) {
		result = po_ffi_call(ffi_binding, NULL, NULL, NULL);
		CHECK(result.ppt.pt == &fixture_pointer_payload, "pointer return mapping");
	}
	po_ffi_free_structures(&env);

	pointer_type.ido_type = IDO_CPT;
	init_binding(&binding, "c_pointer_return", (void*)fixture_pointer_result, &pointer_type, NULL, 0);
	ffi_binding = build_binding(&env, &binding, "C pointer return descriptor must build");
	if (ffi_binding != NULL) {
		result = po_ffi_call(ffi_binding, NULL, NULL, NULL);
		CHECK(result.p == &fixture_pointer_payload, "C pointer return mapping");
	}
	po_ffi_free_structures(&env);
	pointer_type.ido_type = IDO_POINTER;

	fixture_void_called = 0;
	init_binding(&binding, "void_return", (void*)fixture_void_result, &void_type, NULL, 0);
	ffi_binding = build_binding(&env, &binding, "void return descriptor must build");
	if (ffi_binding != NULL) {
		(void)po_ffi_call(ffi_binding, NULL, NULL, NULL);
		CHECK(fixture_void_called == 1, "void return mapping");
	}
	po_ffi_free_structures(&env);
}

static void test_valid_descriptor_cleanup(void)
{
	Type_info int_type = {0};
	C_frame binding;
	Poco_run_env env;
	int iteration;

	int_type.ido_type = IDO_INT;
	for (iteration = 0; iteration != 32; ++iteration) {
		init_binding(&binding, "valid_descriptor", (void*)fixture_function,
			&int_type, NULL, 0);
		init_environment(&env, &binding);
		CHECK(po_ffi_build_structures(&env) == Success,
			"valid descriptor must build");
		CHECK(po_ffi_find_binding_by_name(&env, "valid_descriptor") != NULL,
			"valid descriptor must be findable by name");
		po_ffi_free_structures(&env);
		CHECK(env.func_map == NULL, "descriptor cleanup must reset the function map");
	}
}

static void test_public_vm_cycles(void)
{
	static const PocoBinding bindings[] = {
		{"int fixture_add(int left, int right);", (PocoNativeFunction)fixture_add},
	};
	static const PocoLibrary library = {
		"ffi-hardening-cycle", bindings, 1, NULL, NULL, NULL,
	};
	int iteration;

	for (iteration = 0; iteration != 16; ++iteration) {
		PocoVm* vm = NULL;
		PocoProgram* program = NULL;
		int32_t result = 0;

		CHECK(poco_vm_create(NULL, &vm) == POCO_STATUS_OK, "create VM for cycle");
		if (vm == NULL)
			continue;
		CHECK(poco_vm_register_library(vm, &library) == POCO_STATUS_OK,
			"register cycle binding");
		CHECK(poco_vm_compile_file(vm, FIXTURE_PATH("valid.poc"), &program) == POCO_STATUS_OK,
			"compile cycle program");
		if (program != NULL) {
			CHECK(poco_vm_run(vm, program, NULL, &result) == POCO_STATUS_OK,
				"run cycle program");
			CHECK(result == 42, "cycle program result");
		}
		poco_program_destroy(program);
		poco_vm_destroy(vm);
	}
}

int main(void)
{
	test_descriptor_validation();
	test_duplicate_descriptors();
	test_dynamic_call_storage();
	test_abi_safe_return_storage();
	test_valid_descriptor_cleanup();
	test_public_vm_cycles();
	return failures == 0 ? 0 : 1;
}
