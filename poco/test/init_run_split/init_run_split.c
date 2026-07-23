#include <poco/poco.h>

#include "activation.h"
#include "pocoface.h"

#include <stdio.h>

#ifndef POCO_INIT_RUN_SPLIT_FIXTURE_DIR
#error "POCO_INIT_RUN_SPLIT_FIXTURE_DIR must name the init/run fixture directory"
#endif

static int library_init_calls;
static int library_cleanup_calls;
static int* captured_global;

static int check(int condition, const char* context)
{
	if (condition) {
		return 1;
	}
	fprintf(stderr, "poco_init_run_split: %s\n", context);
	return 0;
}

static void capture_global(int* value)
{
	captured_global = value;
}

static PocoStatus initialize_library(PocoLibrary* library)
{
	(void)library;
	++library_init_calls;
	return POCO_STATUS_OK;
}

static void cleanup_library(PocoLibrary* library)
{
	(void)library;
	++library_cleanup_calls;
}

int main(void)
{
	static const PocoBinding bindings[] = {
		{"void capture_global(int *value);", (PocoNativeFunction)capture_global, NULL, 0},
	};
	static const PocoLibrary library = {
		"test.init_run_split", bindings,        sizeof(bindings) / sizeof(bindings[0]),
		initialize_library,    cleanup_library, NULL,
	};
	PocoVm* vm = NULL;
	PocoProgram* program = NULL;
	PocoActivation* activation = NULL;
	PocoStatus status;
	char script_path[1024];
	int32_t result = 0;
	int ok = 1;

	ok &= check(poco_activation_init(NULL) == POCO_STATUS_NULL_REFERENCE,
				"null activation init is rejected");
	if (snprintf(script_path, sizeof(script_path), "%s/init_run_split.poc",
				 POCO_INIT_RUN_SPLIT_FIXTURE_DIR) >= (int)sizeof(script_path)) {
		return 1;
	}
	ok &= check(poco_vm_create(NULL, &vm) == POCO_STATUS_OK, "create VM");
	ok &=
		check(poco_vm_register_library(vm, &library) == POCO_STATUS_OK, "register fixture library");
	ok &=
		check(poco_vm_compile_file(vm, script_path, &program) == POCO_STATUS_OK, "compile fixture");
	ok &= check(poco_activation_acquire(program, &activation) == POCO_STATUS_OK,
				"acquire activation");
	if (!ok) {
		goto CLEANUP;
	}

	status = poco_activation_init(activation);
	ok &= check(status == POCO_STATUS_OK, "explicit init succeeds");
	ok &= check(activation->initialized && !activation->needs_reset,
				"explicit init records initialized state without completing a run");
	ok &= check(library_init_calls == 1 && library_cleanup_calls == 0,
				"library resources remain active between init and main");
	status = (PocoStatus)po_activation_run_entry(activation, "probe_initialized_global");
	ok &= check(status == POCO_STATUS_OK && activation->result.i == 41,
				"explicit init leaves the global at its initializer value");
	ok &= check(captured_global != NULL && *captured_global == 41,
				"initialized global is addressable before main");
	*captured_global = 50;

	status = (PocoStatus)po_activation_run_entry(activation, "main");
	ok &= check(status == POCO_STATUS_OK, "internal main-only path succeeds");
	ok &= check(captured_global != NULL && *captured_global == 82,
				"main-only path does not reinitialize and preserves the written global");
	ok &= check(library_init_calls == 2 && library_cleanup_calls == 2,
				"split entry paths retain balanced library lifecycles");

	ok &= check(poco_activation_reset(activation) == POCO_STATUS_OK,
				"reset after main-only execution succeeds");
	ok &=
		check(captured_global != NULL && *captured_global == 0, "reset discards persisted globals");
	ok &= check(!activation->initialized && !activation->libraries_initialized,
				"reset returns activation to needs-init state");

	captured_global = NULL;
	status = poco_activation_run(activation, NULL, &result);
	ok &= check(status == POCO_STATUS_OK && result == 73,
				"bundled activation run preserves its result");
	ok &= check(captured_global != NULL && *captured_global == 73,
				"bundled activation run performs init then main and preserves globals");
	ok &= check(library_init_calls == 3 && library_cleanup_calls == 3,
				"bundled activation run retains one library lifecycle");
	ok &= check(poco_activation_run(activation, NULL, NULL) == POCO_STATUS_PARAMETER_RANGE,
				"bundled run still requires reset before reuse");

	ok &= check(poco_activation_reset(activation) == POCO_STATUS_OK,
				"reset before release-path init succeeds");
	ok &= check(poco_activation_init(activation) == POCO_STATUS_OK,
				"explicit init before release succeeds");
	poco_activation_release(activation);
	activation = NULL;
	ok &= check(library_init_calls == 4 && library_cleanup_calls == 4,
				"release cleans resources held by an initialized activation");

	result = 0;
	status = poco_vm_run(vm, program, NULL, &result);
	ok &= check(status == POCO_STATUS_OK && result == 73,
				"VM bundled convenience remains behavior-compatible");
	ok &= check(library_init_calls == 5 && library_cleanup_calls == 5,
				"VM bundled convenience uses one init/main lifecycle");

CLEANUP:
	poco_activation_release(activation);
	poco_program_destroy(program);
	poco_vm_destroy(vm);
	return ok ? 0 : 1;
}
