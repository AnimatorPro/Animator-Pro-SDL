#include <poco/poco.h>

#include "activation.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

#define ARRAY_COUNT(values) (sizeof(values) / sizeof((values)[0]))

static int fixed_add(int left, int right)
{
	return left + right;
}

static int variadic_sum(int count, ...)
{
	va_list arguments;
	int index;
	int result = 0;

	va_start(arguments, count);
	for (index = 0; index < count; ++index) {
		result += va_arg(arguments, int);
	}
	va_end(arguments);
	return result;
}

static int check(int condition, const char* message)
{
	if (condition) {
		return 1;
	}
	fprintf(stderr, "poco_ffi_cif_cache: %s\n", message);
	return 0;
}

static const PocoBinding bindings[] = {
	{"int FixedAdd(int left, int right);", (PocoNativeFunction)fixed_add},
	{"int VariadicSum(int count, ...);", (PocoNativeFunction)variadic_sum},
};

static const PocoLibrary library = {
	"ffi-cif-cache",
	bindings,
	ARRAY_COUNT(bindings),
};

static const char fixed_source[] =
	"main()\n"
	"{\n"
	"  int i; int result;\n"
	"  i = 0; result = 0;\n"
	"  while (i < 1000) { result = FixedAdd(result, 1); ++i; }\n"
	"  return result;\n"
	"}\n";

static const char variadic_source[] =
	"main()\n"
	"{\n"
	"  return VariadicSum(3, 10, 20, 30);\n"
	"}\n";

static int run_program(PocoProgram* program, int32_t expected, size_t expected_native_calls,
					   int expect_variadic_work)
{
	PocoActivation* public_activation = NULL;
	struct PocoActivation* activation;
	PocoStatus status;
	int32_t result = 0;
	int ok = 1;

	status = poco_activation_acquire(program, &public_activation);
	if (!check(status == POCO_STATUS_OK, "activation acquisition failed")) {
		return 0;
	}
	activation = (struct PocoActivation*)public_activation;
	ok &= check(activation->ffi_fixed_call_prep_count == 0 &&
					activation->ffi_variadic_call_prep_count == 0 &&
					activation->ffi_per_call_allocation_count == 0 &&
					activation->ffi_fixed_call_cache_hit_count == 0,
				"new activation counters were not clear");
	status = poco_activation_run(public_activation, NULL, &result);
	ok &= check(status == POCO_STATUS_OK && result == expected, "native call result changed");
	if (expect_variadic_work) {
		ok &= check(activation->ffi_variadic_call_prep_count == expected_native_calls,
					"variadic call did not prepare its cif per call");
		ok &= check(activation->ffi_per_call_allocation_count == expected_native_calls,
					"variadic call did not allocate its per-call buffers");
		ok &= check(activation->ffi_fixed_call_cache_hit_count == 0,
					"variadic call took the fixed cache path");
	} else {
		/*
		 * Every fixed call must reuse the cif and argument buffers prepared once
		 * at binding time: one cache hit per native call, and zero per-call cif
		 * preparation or heap allocation. A regression to per-call preparation
		 * or allocation moves these counts off their expected values.
		 */
		ok &= check(activation->ffi_fixed_call_cache_hit_count == expected_native_calls,
					"fixed calls did not reuse the cached cif and buffers");
		ok &= check(activation->ffi_fixed_call_prep_count == 0,
					"fixed call prepared a cif during execution");
		ok &= check(activation->ffi_variadic_call_prep_count == 0,
					"fixed call entered the variadic prep path");
		ok &= check(activation->ffi_per_call_allocation_count == 0,
					"fixed call allocated per-call buffers");
	}
	poco_activation_release(public_activation);
	return ok;
}

int main(void)
{
	PocoVm* vm = NULL;
	PocoProgram* fixed_program = NULL;
	PocoProgram* variadic_program = NULL;
	PocoStatus status;
	int ok = 1;

	status = poco_vm_create(NULL, &vm);
	if (!check(status == POCO_STATUS_OK, "VM creation failed")) {
		return 1;
	}
	status = poco_vm_register_library(vm, &library);
	if (!check(status == POCO_STATUS_OK, "library registration failed")) {
		ok = 0;
		goto CLEANUP;
	}
	status = poco_vm_compile_buffer(vm, "fixed-cache.poc", fixed_source, sizeof(fixed_source) - 1,
									&fixed_program);
	if (!check(status == POCO_STATUS_OK, "fixed program compilation failed")) {
		ok = 0;
		goto CLEANUP;
	}
	status = poco_vm_compile_buffer(vm, "variadic-cache.poc", variadic_source,
									sizeof(variadic_source) - 1, &variadic_program);
	if (!check(status == POCO_STATUS_OK, "variadic program compilation failed")) {
		ok = 0;
		goto CLEANUP;
	}
	ok &= run_program(fixed_program, 1000, 1000, 0);
	ok &= run_program(variadic_program, 60, 1, 1);

CLEANUP:
	poco_program_destroy(variadic_program);
	poco_program_destroy(fixed_program);
	poco_vm_destroy(vm);
	return ok ? 0 : 1;
}
