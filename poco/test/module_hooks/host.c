#include <poco/poco.h>

#include <stdio.h>
#include <string.h>

typedef struct HookState
{
	const char *expected_name;
	PocoStatus load_result;
	int load_count;
	int unload_count;
} HookState;

static int check(int condition, const char *message)
{
	if (!condition) {
		fprintf(stderr, "poco_module_hooks: %s\n", message);
		return 0;
	}
	return 1;
}

static PocoStatus module_loaded(void *user_data, const PocoModuleInfo *module)
{
	HookState *state = user_data;

	if (state == NULL || module == NULL || module->requested_name == NULL ||
		module->resolved_path == NULL ||
		strcmp(module->requested_name, state->expected_name) != 0)
		return POCO_STATUS_INTERNAL_ERROR;
	++state->load_count;
	return state->load_result;
}

static void module_unloading(void *user_data, const PocoModuleInfo *module)
{
	HookState *state = user_data;

	if (state == NULL || module == NULL || module->requested_name == NULL ||
		strcmp(module->requested_name, state->expected_name) != 0)
		return;
	++state->unload_count;
}

static int compile_with_hooks(const char *script_name, HookState *state,
	PocoStatus expected_status, int expect_run, PocoModuleLoadHook load_hook)
{
	PocoModuleHooks module_hooks = {
		load_hook,
		module_unloading,
		state,
	};
	PocoVmOptions vm_options = {
		.module_hooks = &module_hooks,
	};
	PocoVm *vm = NULL;
	PocoProgram *program = NULL;
	int32_t result = 0;
	char script_path[1024];
	int ok = 1;

	if (snprintf(script_path, sizeof(script_path), "%s/%s",
		POCO_MODULE_HOOK_FIXTURE_DIR, script_name) >= (int)sizeof(script_path))
		return check(0, "fixture path fits");
	ok &= check(poco_vm_create(&vm_options, &vm) == POCO_STATUS_OK, "create VM");
	if (ok) {
		PocoStatus status = poco_vm_compile_file(vm, script_path, &program);
		ok &= check(status == expected_status, "module compile status");
	}
	if (ok && expect_run) {
		ok &= check(poco_vm_run(vm, program, NULL, &result) == POCO_STATUS_OK,
			"run valid module");
		ok &= check(result == 42, "valid module result");
	}
	poco_program_destroy(program);
	poco_vm_destroy(vm);
	return ok;
}

int main(void)
{
	HookState valid = {"poco_module_hook_valid.poe", POCO_STATUS_OK, 0, 0};
	HookState invalid = {"poco_module_hook_invalid.poe", POCO_STATUS_OK, 0, 0};
	HookState rejected = {"poco_module_hook_valid.poe", POCO_STATUS_ACCESS_DENIED, 0, 0};
	int ok = 1;

	ok &= compile_with_hooks("valid.poc", &valid, POCO_STATUS_OK, 1, module_loaded);
	ok &= check(valid.load_count == 1 && valid.unload_count == 1,
		"valid module receives one load and unload hook");

	ok &= compile_with_hooks("invalid.poc", &invalid, POCO_STATUS_REPORTED, 0, module_loaded);
	ok &= check(invalid.load_count == 1 && invalid.unload_count == 1,
		"failed module validation unwinds host policy");

	ok &= compile_with_hooks("valid.poc", &rejected, POCO_STATUS_REPORTED, 0, module_loaded);
	ok &= check(rejected.load_count == 1 && rejected.unload_count == 0,
		"rejected module does not receive unload hook");

	{
		HookState unload_only = {"poco_module_hook_valid.poe", POCO_STATUS_OK, 0, 0};
		ok &= compile_with_hooks("valid.poc", &unload_only, POCO_STATUS_OK, 1, NULL);
		ok &= check(unload_only.load_count == 0 && unload_only.unload_count == 1,
			"unload-only module policy runs during teardown");
	}

	return ok ? 0 : 1;
}
