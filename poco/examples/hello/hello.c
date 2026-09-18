#include <poco/poco.h>

#include <stdio.h>

static void hello_func(void)
{
	printf("Hello from POE!\n");
}

static const PocoBinding hello_bindings[] = {
	{"void HelloFunc(void);", (PocoNativeFunction)hello_func},
};

static const PocoLibrary hello_library = {
	.identity = "Hello POE",
	.bindings = hello_bindings,
	.binding_count = sizeof(hello_bindings) / sizeof(hello_bindings[0]),
};

static const PocoModuleDescriptor hello_module = {
	.abi_version = POCO_MODULE_ABI_VERSION,
	.identity = "Hello POE",
	.library = &hello_library,
};

POCO_MODULE_EXPORT const PocoModuleDescriptor* poco_module_get(void)
{
	return &hello_module;
}
