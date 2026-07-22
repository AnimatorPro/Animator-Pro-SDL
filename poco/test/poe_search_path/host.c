#include <poco/poco.h>

#include <stdio.h>
#include <string.h>

static int check(int condition, const char *message)
{
	if (!condition) {
		fprintf(stderr, "poco_poe_search_path: %s\n", message);
		return 0;
	}
	return 1;
}

static int add_host_paths(PocoVm *vm)
{
	int ok = 1;

	ok &= check(poco_vm_add_library_path(NULL, POCO_SEARCH_HOST_FIRST_DIR) ==
		POCO_STATUS_NULL_REFERENCE, "NULL VM is rejected");
	ok &= check(poco_vm_add_library_path(vm, NULL) == POCO_STATUS_NULL_REFERENCE,
		"NULL library path is rejected");
	ok &= check(poco_vm_add_library_path(vm, "") == POCO_STATUS_PARAMETER_RANGE,
		"empty library path is rejected");
	ok &= check(poco_vm_add_library_path(vm, POCO_SEARCH_HOST_FIRST_DIR) ==
		POCO_STATUS_OK, "append first host path");
	ok &= check(poco_vm_add_library_path(vm, POCO_SEARCH_HOST_SECOND_DIR) ==
		POCO_STATUS_OK, "append second host path");
	return ok;
}

static int run_program(PocoVm *vm, PocoProgram *program, int expected,
	const char *context)
{
	int32_t result = 0;
	PocoStatus status = poco_vm_run(vm, program, NULL, &result);
	int ok = 1;

	ok &= check(status == POCO_STATUS_OK, context);
	if (status == POCO_STATUS_OK && result != expected) {
		fprintf(stderr, "poco_poe_search_path: %s returned %d, expected %d\n",
			context, (int)result, expected);
		ok = 0;
	}
	return ok;
}

static int compile_file_case(void)
{
	PocoVm *vm = NULL;
	PocoProgram *program = NULL;
	PocoStatus status;
	int ok = 1;

	ok &= check(poco_vm_create(NULL, &vm) == POCO_STATUS_OK, "create file VM");
	if (vm != NULL)
		ok &= add_host_paths(vm);
	if (ok) {
		status = poco_vm_compile_file(vm, POCO_SEARCH_SCRIPT_PATH, &program);
		ok &= check(status == POCO_STATUS_OK,
			"file compile resolves script-directory module");
	}
	if (program != NULL)
		ok &= run_program(vm, program, 11,
			"script directory precedes CWD/executable/host paths");
	poco_program_destroy(program);
	poco_vm_destroy(vm);
	return ok;
}

static int compile_buffer_case(const char *source_name, const char *module_name,
	int expected, const char *context)
{
	char source[512];
	PocoVm *vm = NULL;
	PocoProgram *program = NULL;
	PocoStatus status;
	int source_length;
	int ok = 1;

	source_length = snprintf(source, sizeof(source),
		"#pragma poco library \"%s\"\nmain(){return SearchPathValue();}\n",
		module_name);
	if (source_length < 0 || source_length >= (int)sizeof(source))
		return check(0, "buffer fixture source fits");
	ok &= check(poco_vm_create(NULL, &vm) == POCO_STATUS_OK, "create buffer VM");
	if (vm != NULL)
		ok &= add_host_paths(vm);
	if (ok) {
		status = poco_vm_compile_buffer(vm, source_name, source,
			(size_t)source_length, &program);
		ok &= check(status == POCO_STATUS_OK, context);
	}
	if (program != NULL)
		ok &= run_program(vm, program, expected, context);
	poco_program_destroy(program);
	poco_vm_destroy(vm);
	return ok;
}

int main(void)
{
	int ok = 1;

	ok &= compile_file_case();
	/* A conflicting module exists under CWD/virtual/looks.  A buffer's
	 * diagnostic name must not create a physical script-directory leg. */
	ok &= compile_buffer_case("virtual/looks/not-a-file.poc", "buffer_cwd.poe", 22,
		"pathless buffer skips diagnostic-name directory and resolves CWD first");
	ok &= compile_buffer_case("memory:executable", "executable_over_host.poe", 33,
		"pathless buffer resolves executable directory before host paths");
	ok &= compile_buffer_case("memory:host", "host_order.poe", 44,
		"pathless buffer resolves host paths in append order");
	return ok ? 0 : 1;
}
