#include <stdio.h>

#include "standard_library.h"

static const PocoBinding console_bindings[] = {
	{"int puts(char *s);", (PocoNativeFunction)puts, NULL},
	{"int printf(char *format, ...);", (PocoNativeFunction)printf, NULL},
};

static const PocoLibrary console_library = {
	POCO_STANDARD_CONSOLE_LIBRARY_ID,
	console_bindings,
	Array_els(console_bindings),
	NULL,
	NULL,
	NULL,
};

const PocoLibrary *poco_standard_console_library(void)
{
	return &console_library;
}

PocoStatus poco_register_standard_library_catalog(PocoVm *vm)
{
	const PocoLibrary *const catalog[] = {
		poco_standard_console_library(),
		poco_standard_string_library(),
		poco_standard_memory_library(),
		poco_standard_file_library(),
		poco_standard_math_library(),
		poco_standard_path_library(),
	};
	const PocoLibraryRuntimeCleanup cleanup[] = {
		NULL,
		NULL,
		poco_standard_memory_cleanup,
		poco_standard_file_cleanup,
		NULL,
		NULL,
	};
	size_t index;
	PocoStatus status;

	for (index = 0; index < Array_els(catalog); ++index) {
		status = poco_vm_register_library_with_runtime_cleanup(vm, catalog[index],
			cleanup[index]);
		if (status != POCO_STATUS_OK)
			return status;
	}
	return POCO_STATUS_OK;
}
