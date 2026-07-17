#include <poco/poco.h>

static int generic_answer(void)
{
	return 42;
}

static const PocoBinding generic_bindings[] = {
	{"int GenericAnswer(void);", (PocoNativeFunction)generic_answer},
};

static const PocoLibrary generic_library = {
	"generic-bad-version",
	generic_bindings,
	sizeof(generic_bindings) / sizeof(generic_bindings[0]),
	NULL,
	NULL,
	NULL,
};

static const PocoModuleDescriptor generic_module = {
	POCO_MAKE_API_VERSION(POCO_MODULE_ABI_VERSION_MAJOR + 1u, 0u, 0u),
	"generic-bad-version",
	&generic_library,
	NULL,
	NULL,
};

POCO_MODULE_EXPORT const PocoModuleDescriptor *poco_module_get(void)
{
	return &generic_module;
}
