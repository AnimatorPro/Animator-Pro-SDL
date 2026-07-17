#include <poco/poco.h>

#include <stdio.h>

typedef struct LegacyPoeState
{
	int load_count;
	int unload_count;
} LegacyPoeState;

static int check(int condition, const char *message)
{
	if (!condition)
		fprintf(stderr, "poco_legacy_poe_fence: %s\n", message);
	return condition;
}

static PocoStatus allow_legacy_poe(void *user_data, const PocoModuleInfo *module)
{
	LegacyPoeState *state = user_data;

	if (state == NULL || module == NULL || module->requested_name == NULL ||
		module->resolved_path == NULL)
		return POCO_STATUS_NULL_REFERENCE;
	++state->load_count;
	return POCO_STATUS_OK;
}

static void unload_legacy_poe(void *user_data, const PocoModuleInfo *module)
{
	LegacyPoeState *state = user_data;

	if (state != NULL && module != NULL)
		++state->unload_count;
}

static PocoStatus compile_fixture(const PocoVmOptions *options,
	PocoProgram **out_program, PocoVm **out_vm)
{
	char script_path[1024];
	PocoStatus status;

	if (snprintf(script_path, sizeof(script_path), "%s/legacy_poe_fixture.poc",
		POCO_LEGACY_POE_FENCE_FIXTURE_DIR) >= (int)sizeof(script_path))
		return POCO_STATUS_INTERNAL_ERROR;
	status = poco_vm_create(options, out_vm);
	if (status == POCO_STATUS_OK)
		status = poco_vm_compile_file(*out_vm, script_path, out_program);
	return status;
}

int main(void)
{
	PocoVm *generic_vm = NULL;
	PocoProgram *generic_program = NULL;
	PocoVm *legacy_vm = NULL;
	PocoProgram *legacy_program = NULL;
	LegacyPoeState state = {0, 0};
	PocoModuleHooks hooks = {
		.on_load = allow_legacy_poe,
		.on_unload = unload_legacy_poe,
		.user_data = &state,
		.allow_legacy_poe = 1,
	};
	PocoVmOptions legacy_options = {
		.module_hooks = &hooks,
	};
	PocoStatus status;
	int32_t result = 0;
	int ok = 1;

	status = compile_fixture(NULL, &generic_program, &generic_vm);
	ok &= check(status != POCO_STATUS_OK,
		"generic VM rejects the legacy native-POE ABI by default");
	poco_program_destroy(generic_program);
	poco_vm_destroy(generic_vm);

	status = compile_fixture(&legacy_options, &legacy_program, &legacy_vm);
	ok &= check(status == POCO_STATUS_OK,
		"explicit legacy-POE policy admits the compatibility module");
	if (status == POCO_STATUS_OK) {
		ok &= check(poco_vm_run(legacy_vm, legacy_program, NULL, &result) == POCO_STATUS_OK,
			"legacy-POE compatibility module runs");
		ok &= check(result == 73, "legacy-POE compatibility module result");
	}
	poco_program_destroy(legacy_program);
	poco_vm_destroy(legacy_vm);
	ok &= check(state.load_count == 1 && state.unload_count == 1,
		"explicit legacy-POE policy owns the module lifecycle");

	return ok ? 0 : 1;
}
