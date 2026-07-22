#include <poco/poco.h>

#ifndef POCO_SEARCH_PATH_VALUE
#error POCO_SEARCH_PATH_VALUE must identify this fixture location
#endif

static int search_path_value(void)
{
	return POCO_SEARCH_PATH_VALUE;
}

static const PocoBinding search_path_bindings[] = {
	{"int SearchPathValue(void);", (PocoNativeFunction)search_path_value},
};

static const PocoLibrary search_path_library = {
	"poe-search-path-fixture",
	search_path_bindings,
	sizeof(search_path_bindings) / sizeof(search_path_bindings[0]),
	NULL,
	NULL,
	NULL,
};

static const PocoModuleDescriptor search_path_module = {
	POCO_MODULE_ABI_VERSION,
	"poe-search-path-fixture",
	&search_path_library,
	NULL,
	NULL,
};

POCO_MODULE_EXPORT const PocoModuleDescriptor *poco_module_get(void)
{
	return &search_path_module;
}
