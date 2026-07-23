#include <poco/poco.h>

static int initialize_count;

static int external_value(void)
{
	return initialize_count;
}

static PocoStatus initialize(PocoLibrary* library)
{
	(void)library;
	++initialize_count;
	return POCO_STATUS_OK;
}

static const PocoBinding bindings[] = {
	{"int ExternalValue(void);", (PocoNativeFunction)external_value, NULL, 0},
};

static const PocoLibrary library = {
	"serialize-fixture-identity",
	bindings,
	sizeof(bindings) / sizeof(bindings[0]),
	initialize,
	NULL,
	NULL,
};

static const PocoModuleDescriptor module = {
	POCO_MODULE_ABI_VERSION, "serialize-fixture-identity", &library, NULL, NULL,
};

POCO_MODULE_EXPORT const PocoModuleDescriptor* poco_module_get(void)
{
	return &module;
}
