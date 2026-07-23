/* Poco's private, host-neutral standard-library catalog. */
#ifndef POCO_STANDARD_LIBRARY_H
#define POCO_STANDARD_LIBRARY_H

#include "pocolib.h"

#define POCO_STANDARD_CONSOLE_LIBRARY_ID "poco.standard.console"
#define POCO_STANDARD_STRING_LIBRARY_ID "poco.standard.string"
#define POCO_STANDARD_MEMORY_LIBRARY_ID "poco.standard.memory"
#define POCO_STANDARD_FILE_LIBRARY_ID "poco.standard.file"
#define POCO_STANDARD_MATH_LIBRARY_ID "poco.standard.math"
#define POCO_STANDARD_PATH_LIBRARY_ID "poco.standard.path"

typedef void (*PocoLibraryRuntimeCleanup)(Poco_lib* library);

/* These descriptors are registered through the public PocoLibrary API. */
const PocoLibrary* poco_standard_console_library(void);
const PocoLibrary* poco_standard_string_library(void);
const PocoLibrary* poco_standard_memory_library(void);
const PocoLibrary* poco_standard_file_library(void);
const PocoLibrary* poco_standard_math_library(void);
const PocoLibrary* poco_standard_path_library(void);

/* Resource-backed bindings clean up the program-local legacy snapshot. */
void poco_standard_memory_cleanup(Poco_lib* library);
void poco_standard_file_cleanup(Poco_lib* library);

/* Private adapter used only while constructing the standard catalog. */
PocoStatus poco_vm_register_library_with_runtime_cleanup(PocoVm* vm, const PocoLibrary* library,
														 PocoLibraryRuntimeCleanup runtime_cleanup);
PocoStatus poco_register_standard_library_catalog(PocoVm* vm);

#endif /* POCO_STANDARD_LIBRARY_H */
