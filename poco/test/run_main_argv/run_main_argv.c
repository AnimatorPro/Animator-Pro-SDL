#include <poco/poco.h>

#include "activation.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef POCO_RUN_MAIN_ARGV_FIXTURE_DIR
#error "POCO_RUN_MAIN_ARGV_FIXTURE_DIR must name the run-main fixture directory"
#endif

static int check(int condition, const char* context)
{
	if (condition) {
		return 1;
	}
	fprintf(stderr, "poco_run_main_argv: %s\n", context);
	return 0;
}

static int acquire_fixture(PocoVm* vm, const char* name, PocoProgram** out_program,
						   PocoActivation** out_activation)
{
	char path[1024];

	if (snprintf(path, sizeof(path), "%s/%s", POCO_RUN_MAIN_ARGV_FIXTURE_DIR, name) >=
		(int)sizeof(path)) {
		return 0;
	}
	return poco_vm_compile_file(vm, path, out_program) == POCO_STATUS_OK &&
		   poco_activation_acquire(*out_program, out_activation) == POCO_STATUS_OK;
}

static void release_fixture(PocoProgram** program, PocoActivation** activation)
{
	poco_activation_release(*activation);
	poco_program_destroy(*program);
	*activation = NULL;
	*program = NULL;
}

static int exercise_argv_program(PocoActivation* activation)
{
	char* reset_argv[] = {"reset"};
	char** host_argv;
	Popot reset_span;
	int32_t result = -1;
	int ok = 1;

	host_argv = malloc(3 * sizeof(*host_argv));
	if (host_argv == NULL) {
		return 0;
	}
	host_argv[0] = malloc(5);
	host_argv[1] = malloc(4);
	host_argv[2] = malloc(1);
	if (host_argv[0] == NULL || host_argv[1] == NULL || host_argv[2] == NULL) {
		free(host_argv[0]);
		free(host_argv[1]);
		free(host_argv[2]);
		free(host_argv);
		return 0;
	}
	memcpy(host_argv[0], "anim", 5);
	memcpy(host_argv[1], "two", 4);
	host_argv[2][0] = '\0';

	ok &= check(poco_activation_init(activation) == POCO_STATUS_OK, "initialize argv activation");
	ok &= check(poco_activation_run_main(activation, 3, host_argv, &result) == POCO_STATUS_OK &&
					result == 73,
				"main(int, char **) observes copied strings and returns int");
	ok &= check(strcmp(host_argv[1], "two") == 0,
				"script writes affect only the activation-owned argv copy");
	ok &= check(activation->main_argv_allocation == NULL,
				"marshaled argv storage is released when main returns");
	free(host_argv[0]);
	free(host_argv[1]);
	free(host_argv[2]);
	free(host_argv);
	ok &= check(poco_activation_run_main(activation, 0, NULL, NULL) == POCO_STATUS_PARAMETER_RANGE,
				"run-main requires reset before reuse");
	ok &= check(poco_activation_reset(activation) == POCO_STATUS_OK, "reset argv activation");
	ok &=
		check(poco_activation_run_main(activation, -1, NULL, NULL) == POCO_STATUS_PARAMETER_RANGE &&
				  !activation->needs_reset,
			  "negative argc is rejected before execution");
	ok &= check(poco_activation_run_main(activation, 1, NULL, NULL) == POCO_STATUS_NULL_REFERENCE &&
					activation->main_argv_allocation == NULL,
				"nonzero argc requires host argv");
	ok &= check(
		poco_activation_marshal_main_argv(activation, 1, reset_argv, &reset_span) == Success &&
			activation->main_argv_allocation != NULL,
		"direct marshal creates activation-owned reset state");
	ok &= check(poco_activation_reset(activation) == POCO_STATUS_OK &&
					activation->main_argv_allocation == NULL,
				"reset releases an outstanding marshaled argv copy");
	return ok;
}

static int serialize_program(PocoVm* vm, PocoProgram* program, PocoProgram** out_decoded)
{
	uint8_t* bytes = NULL;
	size_t size = 0;
	int ok;

	if (poco_program_serialize_buffer(program, POCO_DEBUG_LEVEL_MINIMAL, NULL, 0, &size) !=
			POCO_STATUS_BUFFER_TOO_SMALL ||
		size == 0) {
		return 0;
	}
	bytes = malloc(size);
	if (bytes == NULL) {
		return 0;
	}
	ok = poco_program_serialize_buffer(program, POCO_DEBUG_LEVEL_MINIMAL, bytes, size, &size) ==
			 POCO_STATUS_OK &&
		 poco_vm_deserialize_buffer(vm, bytes, size, out_decoded) == POCO_STATUS_OK;
	free(bytes);
	return ok;
}

int main(void)
{
	PocoVm* vm = NULL;
	PocoProgram* program = NULL;
	PocoProgram* decoded = NULL;
	PocoActivation* activation = NULL;
	PocoActivation* decoded_activation = NULL;
	char* argv_values[] = {"anim", "two", ""};
	int32_t result;
	int ok = 1;

	ok &= check(poco_activation_run_main(NULL, 0, NULL, NULL) == POCO_STATUS_NULL_REFERENCE,
				"null activation is rejected");
	ok &= check(poco_vm_create(NULL, &vm) == POCO_STATUS_OK, "create VM");
	ok &=
		check(acquire_fixture(vm, "main_argv.poc", &program, &activation), "compile argv fixture");
	if (activation != NULL) {
		ok &= exercise_argv_program(activation);
	}
	ok &= check(serialize_program(vm, program, &decoded), "serialize argv program metadata");
	ok &= check(
		decoded != NULL && poco_activation_acquire(decoded, &decoded_activation) == POCO_STATUS_OK,
		"acquire decoded argv program");
	if (decoded_activation != NULL) {
		ok &= check(poco_activation_init(decoded_activation) == POCO_STATUS_OK,
					"initialize decoded argv program");
		result = -1;
		ok &= check(poco_activation_run_main(decoded_activation, 3, argv_values, &result) ==
							POCO_STATUS_OK &&
						result == 73,
					"decoded program retains main signature and argv execution");
	}
	poco_activation_release(decoded_activation);
	poco_program_destroy(decoded);
	release_fixture(&program, &activation);

	ok &= check(acquire_fixture(vm, "main_no_args.poc", &program, &activation),
				"compile no-argument main");
	result = -1;
	ok &= check(activation != NULL &&
					poco_activation_run_main(activation, -99, (char**)(uintptr_t)1, &result) ==
						POCO_STATUS_OK &&
					result == 31,
				"main() ignores host argc and argv and captures int return");
	release_fixture(&program, &activation);

	ok &= check(acquire_fixture(vm, "main_void.poc", &program, &activation), "compile void main");
	result = -1;
	ok &= check(activation != NULL &&
					poco_activation_run_main(activation, 0, NULL, &result) == POCO_STATUS_OK &&
					result == 0,
				"void main produces a zero result");
	release_fixture(&program, &activation);

	ok &= check(acquire_fixture(vm, "main_bad_signature.poc", &program, &activation),
				"compile bad-signature main");
	ok &= check(activation != NULL &&
					poco_activation_run_main(activation, 1, argv_values, NULL) ==
						POCO_STATUS_PARAMETER_RANGE &&
					!activation->needs_reset,
				"main(int) is rejected without consuming the activation");
	release_fixture(&program, &activation);

	ok &= check(acquire_fixture(vm, "main_argv_outer_oob.poc", &program, &activation),
				"compile outer-bounds fixture");
	ok &= check(activation != NULL &&
					poco_activation_run_main(activation, 3, argv_values, NULL) != POCO_STATUS_OK &&
					activation->main_argv_allocation == NULL,
				"argv array bounds reject argv[argc] and release the copy");
	release_fixture(&program, &activation);

	ok &= check(acquire_fixture(vm, "main_argv_string_oob.poc", &program, &activation),
				"compile string-bounds fixture");
	ok &= check(activation != NULL &&
					poco_activation_run_main(activation, 3, argv_values, NULL) != POCO_STATUS_OK &&
					activation->main_argv_allocation == NULL,
				"per-string bounds reject bytes past NUL and release the copy");
	release_fixture(&program, &activation);

	poco_vm_destroy(vm);
	return ok ? 0 : 1;
}
