#include <poco/poco.h>

static const PocoLibrary generic_library = {
	"generic-empty", NULL, 0, NULL, NULL, NULL,
};

static const PocoModuleDescriptor generic_module = {
	POCO_MODULE_ABI_VERSION, "generic-empty", &generic_library, NULL, NULL,
};

POCO_MODULE_EXPORT const PocoModuleDescriptor* poco_module_get(void)
{
	return &generic_module;
}
