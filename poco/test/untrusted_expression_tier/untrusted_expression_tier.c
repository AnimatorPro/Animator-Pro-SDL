#include <poco/poco.h>

#include <stdio.h>
#include <string.h>

static int pointer_binding_calls;

static void* pointer_identity(void* value)
{
	++pointer_binding_calls;
	return value;
}

static const PocoBinding pointer_bindings[] = {
	{"void *pointer_identity(void *value);", (PocoNativeFunction)pointer_identity, NULL, 0},
};

static const PocoLibrary pointer_library = {
	"poco.test.expression.pointer",
	pointer_bindings,
	sizeof(pointer_bindings) / sizeof(pointer_bindings[0]),
	NULL,
	NULL,
	NULL,
};

static int check(int condition, const char* message)
{
	if (condition) {
		return 1;
	}
	fprintf(stderr, "poco_untrusted_expression_tier: %s\n", message);
	return 0;
}

static PocoStatus compile_source(PocoVm* vm, const char* name, const char* source,
								 PocoProgram** out_program)
{
	return poco_vm_compile_buffer(vm, name, source, strlen(source), out_program);
}

int main(void)
{
	static const char expression_source[] =
		"main()\n"
		"{\n"
		"  if (sqrt(81.0) != 9.0 || fabs(-3.0) != 3.0) return 1;\n"
		"  if ((6 & 3) != 2 || (1 << 5) != 32) return 2;\n"
		"  if (!(1 && 2) || (0 || 0)) return 3;\n"
		"  return 0;\n"
		"}\n";
	static const char pointer_source[] =
		"main()\n"
		"{\n"
		"  int value;\n"
		"  value = 17;\n"
		"  if (pointer_identity(&value) == &value) return 0;\n"
		"  return 1;\n"
		"}\n";
	PocoVm* expression_vm = NULL;
	PocoVm* late_vm = NULL;
	PocoVm* restricted_vm = NULL;
	PocoVm* trusted_vm = NULL;
	PocoProgram* program = NULL;
	PocoStatus status;
	int32_t result = -1;
	int ok = 1;

	ok &= check(poco_vm_create(NULL, &expression_vm) == POCO_STATUS_OK, "create expression VM");
	ok &= check(poco_vm_register_untrusted_expression_library(expression_vm) == POCO_STATUS_OK,
				"register expression table");
	ok &= check(poco_vm_register_untrusted_expression_library(expression_vm) == POCO_STATUS_OK,
				"expression registration is not idempotent");
	status = compile_source(expression_vm, "expression-math", expression_source, &program);
	ok &= check(status == POCO_STATUS_OK && program != NULL,
				"expression table did not expose scalar math and logic");
	if (program != NULL) {
		ok &= check(poco_vm_register_untrusted_expression_library(expression_vm) == POCO_STATUS_OK,
					"expression registration stopped being idempotent after compile");
		status = poco_vm_run(expression_vm, program, NULL, &result);
		ok &=
			check(status == POCO_STATUS_OK && result == 0, "expression math/logic fixture failed");
		poco_program_destroy(program);
		program = NULL;
	}
	poco_vm_destroy(expression_vm);

	/* The source and pointer library are identical for both VMs below.  Only
	 * the registered capability table differs. */
	ok &= check(poco_vm_create(NULL, &trusted_vm) == POCO_STATUS_OK, "create trusted VM");
	ok &= check(poco_vm_register_trusted_graph_library(trusted_vm) == POCO_STATUS_OK,
				"register trusted graph table");
	ok &= check(poco_vm_register_library(trusted_vm, &pointer_library) == POCO_STATUS_OK,
				"register trusted pointer binding");
	status = compile_source(trusted_vm, "trusted-pointer", pointer_source, &program);
	if (status != POCO_STATUS_OK) {
		fprintf(stderr, "trusted pointer compile status=%d: %s\n", (int)status,
				poco_get_last_error(trusted_vm));
	}
	ok &= check(status == POCO_STATUS_OK && program != NULL,
				"trusted tier unexpectedly rejected pointer binding");
	if (program != NULL) {
		status = poco_vm_run(trusted_vm, program, NULL, &result);
		ok &= check(status == POCO_STATUS_OK && result == 0, "trusted pointer binding failed");
		poco_program_destroy(program);
		program = NULL;
	}
	poco_vm_destroy(trusted_vm);
	ok &= check(pointer_binding_calls == 1,
				"trusted pointer fixture did not reach native binding exactly once");

	/* A tier cannot be tightened after a pointer-capable program has already
	 * captured FFI descriptors from its VM. */
	ok &= check(poco_vm_create(NULL, &late_vm) == POCO_STATUS_OK, "create late-tier VM");
	ok &= check(poco_vm_register_library(late_vm, &pointer_library) == POCO_STATUS_OK,
				"register late-tier pointer binding");
	status = compile_source(late_vm, "late-pointer", pointer_source, &program);
	ok &= check(status == POCO_STATUS_OK && program != NULL,
				"late-tier pointer program did not compile");
	ok &=
		check(poco_vm_register_untrusted_expression_library(late_vm) == POCO_STATUS_PARAMETER_RANGE,
			  "expression tier replaced a live pointer-capable program policy");
	if (program != NULL) {
		poco_program_destroy(program);
		program = NULL;
	}
	poco_vm_destroy(late_vm);

	ok &= check(poco_vm_create(NULL, &restricted_vm) == POCO_STATUS_OK, "create restricted VM");
	ok &= check(poco_vm_register_untrusted_expression_library(restricted_vm) == POCO_STATUS_OK,
				"register restricted expression table");
	ok &= check(poco_vm_register_library(restricted_vm, &pointer_library) == POCO_STATUS_OK,
				"register pointer fixture for negative compile");
	status = compile_source(restricted_vm, "untrusted-pointer", pointer_source, &program);
	if (status != POCO_STATUS_FFI_INVALID_BINDING) {
		fprintf(stderr, "untrusted pointer compile status=%d: %s\n", (int)status,
				poco_get_last_error(restricted_vm));
	}
	ok &= check(status == POCO_STATUS_FFI_INVALID_BINDING && program == NULL,
				"expression tier published a program with an FFI pointer binding");
	ok &= check(pointer_binding_calls == 1, "expression tier reached the native pointer binding");
	ok &= check(strstr(poco_get_last_error(restricted_vm), "excludes pointer") != NULL,
				"expression tier did not diagnose its pointer exclusion");
	if (program != NULL) {
		poco_program_destroy(program);
	}
	poco_vm_destroy(restricted_vm);

	return ok ? 0 : 1;
}
