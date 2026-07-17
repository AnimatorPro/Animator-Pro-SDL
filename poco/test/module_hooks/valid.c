#include <poco/poco.h>

static int module_hook_answer(void)
{
	return 42;
}

static const PocoBinding module_hook_bindings[] = {
	{"int ModuleHookAnswer(void);", (PocoNativeFunction)module_hook_answer},
};

static const PocoLibrary module_hook_library = {
	"module-hook-valid",
	module_hook_bindings,
	sizeof(module_hook_bindings) / sizeof(module_hook_bindings[0]),
	NULL,
	NULL,
	NULL,
};

static const PocoModuleDescriptor module_hook_module = {
	POCO_MODULE_ABI_VERSION,
	"module-hook-valid",
	&module_hook_library,
	NULL,
	NULL,
};

POCO_MODULE_EXPORT const PocoModuleDescriptor *poco_module_get(void)
{
	return &module_hook_module;
}
