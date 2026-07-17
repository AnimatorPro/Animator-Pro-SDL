#include <poco/poco.h>

static const PocoLibrary module_hook_library = {
	"module-hook-invalid",
	NULL,
	0,
	NULL,
	NULL,
	NULL,
};

static const PocoModuleDescriptor module_hook_module = {
	POCO_MODULE_ABI_VERSION,
	"module-hook-invalid",
	&module_hook_library,
	NULL,
	NULL,
};

POCO_MODULE_EXPORT const PocoModuleDescriptor *poco_module_get(void)
{
	return &module_hook_module;
}
