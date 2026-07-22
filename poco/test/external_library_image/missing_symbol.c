#include <poco/poco.h>

static int different_value(void)
{
	return 99;
}

static const PocoBinding bindings[] = {
	{"int DifferentValue(void);", (PocoNativeFunction)different_value, NULL, 0},
};

static const PocoLibrary library = {
	"missing-symbol-fixture", bindings, sizeof(bindings) / sizeof(bindings[0]), NULL, NULL, NULL,
};

static const PocoModuleDescriptor module = {
	POCO_MODULE_ABI_VERSION, "missing-symbol-fixture", &library, NULL, NULL,
};

POCO_MODULE_EXPORT const PocoModuleDescriptor* poco_module_get(void)
{
	return &module;
}
