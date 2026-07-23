#include <poco/poco.h>

static int external_value(void)
{
	return 99;
}

static const PocoBinding bindings[] = {
	{"int ExternalValue(void);", (PocoNativeFunction)external_value, NULL, 0},
};

static const PocoLibrary library = {
	"bad-abi-fixture", bindings, sizeof(bindings) / sizeof(bindings[0]), NULL, NULL, NULL,
};

static const PocoModuleDescriptor module = {
	POCO_MODULE_ABI_VERSION + 1u, "bad-abi-fixture", &library, NULL, NULL,
};

POCO_MODULE_EXPORT const PocoModuleDescriptor* poco_module_get(void)
{
	return &module;
}
