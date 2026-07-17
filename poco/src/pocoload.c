
#include <poco/poco.h>

#include "poco_errcodes.h"
#include "filepath.h"
#include "pocorex.h"
#include "pocolib.h"



#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#include <limits.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif

#ifndef POCO_H
#include "poco.h"
#endif

#ifdef _WIN32
static void* poco_dlopen(const char* filename, int flag)
{
	HMODULE h = LoadLibraryA(filename);
	if (h == NULL) {
		return NULL;
	}
	return (void*)h;
}

static void* poco_dlsym(void* handle, const char* symbol)
{
	if (handle == NULL) {
		return NULL;
	}
	return (void*)GetProcAddress((HMODULE)handle, symbol);
}

static int poco_dlclose(void* handle)
{
	if (handle == NULL) {
		return -1;
	}
	if (FreeLibrary((HMODULE)handle) == 0) {
		return -1;
	}
	return 0;
}

static const char* poco_dlerror(void)
{
	static char buf[256];
	DWORD err = GetLastError();
	if (err == 0) {
		return NULL;
	}
	snprintf(buf, sizeof(buf), "Windows error %lu", err);
	return buf;
}

#else

static void* poco_dlopen(const char* filename, int flag)
{
	return dlopen(filename, flag);
}

static void* poco_dlsym(void* handle, const char* symbol)
{
	return dlsym(handle, symbol);
}

static int poco_dlclose(void* handle)
{
	return dlclose(handle);
}

static const char* poco_dlerror(void)
{
	return dlerror();
}

#endif

static const char* get_platform_extension(void)
{
#ifdef _WIN32
	return ".dll";
#elif defined(__APPLE__)
	return ".dylib";
#else
	return ".so";
#endif
}

static void get_directory_from_path(const char* path, char* dir, size_t dir_size)
{
	const char* last_slash = strrchr(path, '/');
#ifdef _WIN32
	const char* last_backslash = strrchr(path, '\\');
	if (last_backslash > last_slash) {
		last_slash = last_backslash;
	}
#endif
	if (last_slash != NULL) {
		size_t len = last_slash - path + 1;
		if (len < dir_size) {
			strncpy(dir, path, len);
			dir[len] = '\0';
		} else {
			dir[0] = '\0';
		}
	} else {
		dir[0] = '\0';
	}
}

static char* try_load_path(const char* base_dir, const char* libname, char* out_path, bool verbose)
{
	const char* extensions[] = {".poe", get_platform_extension(), NULL};
	const char* ext_ptr;
	int ext_idx = 0;
	
	if (libname == NULL || strlen(libname) == 0) {
		return NULL;
	}
	
	const char* existing_ext = strrchr(libname, '.');
	int has_extension = (existing_ext != NULL && existing_ext != libname && 
	                     strlen(existing_ext) > 1);
	
	if (has_extension) {
		snprintf(out_path, PATH_SIZE, "%s%s", base_dir, libname);
		if (verbose) {
			fprintf(stderr, "[poco library search] trying '%s'\n", out_path);
		}
		FILE* test_file = fopen(out_path, "r");
		if (test_file != NULL) {
			fclose(test_file);
			return out_path;
		}
		return NULL;
	}
	
	while ((ext_ptr = extensions[ext_idx++]) != NULL) {
		snprintf(out_path, PATH_SIZE, "%s%s%s", base_dir, libname, ext_ptr);
		if (verbose) {
			fprintf(stderr, "[poco library search] trying '%s'\n", out_path);
		}
		
		FILE* test_file = fopen(out_path, "r");
		if (test_file != NULL) {
			fclose(test_file);
			return out_path;
		}
	}
	
	return NULL;
}

char* poco_find_library_file(const char* script_path, const char* libname, bool verbose)
{
	static char result_path[PATH_SIZE];
	char test_path[PATH_SIZE];
	char dir_path[PATH_SIZE];
	
	if (libname == NULL || strlen(libname) == 0) {
		return NULL;
	}
	
	result_path[0] = '\0';
	if (verbose) {
		fprintf(stderr, "[poco library] #pragma poco library search for '%s'\n", libname);
	}
	
	if (script_path != NULL) {
		get_directory_from_path(script_path, dir_path, sizeof(dir_path));
		if (strlen(dir_path) > 0 && try_load_path(dir_path, libname, test_path, verbose) != NULL) {
			strncpy(result_path, test_path, PATH_SIZE - 1);
			result_path[PATH_SIZE - 1] = '\0';
			return result_path;
		}
	}
	
	if (getcwd(dir_path, sizeof(dir_path)) != NULL) {
		size_t len = strlen(dir_path);
		if (len > 0 && dir_path[len - 1] != '/' && dir_path[len - 1] != '\\') {
			strcat(dir_path, "/");
		}
		if (try_load_path(dir_path, libname, test_path, verbose) != NULL) {
			strncpy(result_path, test_path, PATH_SIZE - 1);
			result_path[PATH_SIZE - 1] = '\0';
			return result_path;
		}
	}
	
#ifdef _WIN32
	char exe_path[PATH_SIZE];
	DWORD len = GetModuleFileNameA(NULL, exe_path, PATH_SIZE);
	if (len > 0) {
		get_directory_from_path(exe_path, dir_path, sizeof(dir_path));
		if (strlen(dir_path) > 0 && try_load_path(dir_path, libname, test_path, verbose) != NULL) {
			strncpy(result_path, test_path, PATH_SIZE - 1);
			result_path[PATH_SIZE - 1] = '\0';
			return result_path;
		}
	}
#else
	char exe_path[PATH_SIZE];
	ssize_t len = readlink("/proc/self/exe", exe_path, PATH_SIZE - 1);
	if (len > 0) {
		exe_path[len] = '\0';
		get_directory_from_path(exe_path, dir_path, sizeof(dir_path));
		if (strlen(dir_path) > 0 && try_load_path(dir_path, libname, test_path, verbose) != NULL) {
			strncpy(result_path, test_path, PATH_SIZE - 1);
			result_path[PATH_SIZE - 1] = '\0';
			return result_path;
		}
	}
#ifdef __APPLE__
	char bundle_path[PATH_SIZE * 2];
	uint32_t bundle_size = sizeof(bundle_path);
	if (_NSGetExecutablePath(bundle_path, &bundle_size) == 0) {
		get_directory_from_path(bundle_path, dir_path, sizeof(dir_path));
		if (strlen(dir_path) > 0 && try_load_path(dir_path, libname, test_path, verbose) != NULL) {
			strncpy(result_path, test_path, PATH_SIZE - 1);
			result_path[PATH_SIZE - 1] = '\0';
			return result_path;
		}
	}
#endif
#endif
	
	if (verbose) {
		fprintf(stderr, "[poco library search] not found for '%s'\n", libname);
	}
	return NULL;
}

/*****************************************************************************
 * Format and output a library loading error message.
 * 
 * This utility function provides consistent error messaging for library
 * loading failures across the codebase.
 ****************************************************************************/
void format_poco_lib_error(Errcode err, const char* libname, const char* lib_path,
                           const char* sys_error, int expected_version, 
                           int actual_version, int count, bool verbose)
{
	switch (err) {
		case Err_poco_lib_not_found:
			fprintf(stderr, "Error: Poco library '%s' not found in search paths\n", libname);
			if (!verbose) {
				fprintf(stderr, "  Searched: script directory, current working directory, and executable directory\n");
				fprintf(stderr, "  (Use -V flag for verbose search output)\n");
			}
			break;
			
		case Err_poco_lib_load_failed:
			fprintf(stderr, "Error: Failed to load poco library '%s'", libname);
			if (lib_path != NULL) {
				fprintf(stderr, " from '%s'", lib_path);
			}
			fprintf(stderr, "\n");
			if (sys_error != NULL) {
				fprintf(stderr, "  System error: %s\n", sys_error);
			}
			break;
			
		case Err_poco_lib_no_entry:
			fprintf(stderr, "Error: Poco library '%s' is missing entry point '%s'\n", libname,
			        POCO_MODULE_ENTRY_POINT);
			fprintf(stderr, "  New modules must export '%s'. Legacy native-POE modules require an explicit Animator compatibility policy.\n",
			        POCO_MODULE_ENTRY_POINT);
			break;
			
		case Err_poco_lib_invalid:
			fprintf(stderr, "Error: Poco library '%s' entry point returned NULL\n", libname);
			fprintf(stderr, "  Library structure is invalid\n");
			break;
			
		case Err_poco_lib_version:
			fprintf(stderr, "Error: Poco library '%s' version mismatch\n", libname);
			if (expected_version > 0 && actual_version > 0) {
				fprintf(stderr, "  Expected version: %d, Library version: %d\n", 
				        expected_version, actual_version);
			}
			break;
			
		case Err_poco_lib_empty:
			fprintf(stderr, "Error: Poco library '%s' contains no functions\n", libname);
			if (count >= 0) {
				fprintf(stderr, "  Library count: %d, Library pointer: %s\n", 
				        count, (count == 0) ? "NULL or empty" : "valid");
			}
			break;
			
		default:
			fprintf(stderr, "Error: Unknown library loading error for '%s' (code %d)\n", 
			        libname, err);
			break;
	}
}

struct PocoModule {
	const PocoModuleDescriptor* descriptor;
	PocoLibrary library;
	Poco_lib legacy_library;
	Lib_proto* prototypes;
	void* module_data;
	int library_initialized;
};

typedef struct {
	void* handle;
	Pocorex* exe;
	PocoModule* generic_module;
	PocoModuleHooks hooks;
	PocoModuleInfo info;
	char* requested_name;
	char* resolved_path;
} Poco_lib_loaded;

/* Poco's compiler is already documented as non-reentrant.  The public VM
 * wrapper swaps this per-compilation policy around the legacy compiler call;
 * each successfully loaded module keeps its own copy for later unloading. */
static PocoModuleHooks poco_active_module_hooks;

void poco_loader_swap_module_hooks(const PocoModuleHooks* hooks,
	PocoModuleHooks* previous_hooks)
{
	if (previous_hooks != NULL)
		*previous_hooks = poco_active_module_hooks;
	if (hooks != NULL)
		poco_active_module_hooks = *hooks;
	else
		memset(&poco_active_module_hooks, 0, sizeof(poco_active_module_hooks));
}

static char* poco_copy_module_string(const char* value)
{
	char* copy;
	size_t length;

	if (value == NULL)
		return NULL;
	length = strlen(value) + 1;
	copy = malloc(length);
	if (copy != NULL)
		memcpy(copy, value, length);
	return copy;
}

static void poco_call_module_unload_hook(const PocoModuleHooks* hooks,
	const PocoModuleInfo* info)
{
	if (hooks != NULL && hooks->on_unload != NULL)
		hooks->on_unload(hooks->user_data, info);
}

static Poco_lib_loaded* poco_make_loaded_module(void* handle,
	const PocoModuleHooks* hooks, const PocoModuleInfo* info)
{
	Poco_lib_loaded* loaded;

	loaded = calloc(1, sizeof(*loaded));
	if (loaded == NULL)
		return NULL;
	loaded->requested_name = poco_copy_module_string(info->requested_name);
	loaded->resolved_path = poco_copy_module_string(info->resolved_path);
	if (loaded->requested_name == NULL || loaded->resolved_path == NULL) {
		free(loaded->requested_name);
		free(loaded->resolved_path);
		free(loaded);
		return NULL;
	}
	loaded->handle = handle;
	loaded->hooks = *hooks;
	loaded->info.requested_name = loaded->requested_name;
	loaded->info.resolved_path = loaded->resolved_path;
	return loaded;
}

static void poco_free_loaded_module(Poco_lib_loaded* loaded)
{
	if (loaded == NULL)
		return;
	free(loaded->requested_name);
	free(loaded->resolved_path);
	free(loaded);
}

static Errcode poco_call_module_load_hook(const PocoModuleHooks* hooks,
	const PocoModuleInfo* info, bool* out_hook_succeeded)
{
	PocoStatus status;

	if (out_hook_succeeded != NULL)
		*out_hook_succeeded = false;
	if (hooks == NULL)
		return Success;
	if (hooks->on_load == NULL) {
		if (out_hook_succeeded != NULL)
			*out_hook_succeeded = hooks->on_unload != NULL;
		return Success;
	}
	status = hooks->on_load(hooks->user_data, info);
	if (status == POCO_STATUS_OK) {
		if (out_hook_succeeded != NULL)
			*out_hook_succeeded = true;
		return Success;
	}
	fprintf(stderr, "Error: Poco library '%s' rejected by host module-load hook",
		info->requested_name != NULL ? info->requested_name : "(unknown)");
	if (info->resolved_path != NULL)
		fprintf(stderr, " at '%s'", info->resolved_path);
	fprintf(stderr, " (status %d)\n", status);
	return status < POCO_STATUS_OK ? (Errcode)status : Err_poco_lib_load_failed;
}

static void poco_generic_module_report_diagnostic(const PocoModuleHost* host,
	const PocoDiagnostic* diagnostic)
{
	(void)host;
	if (diagnostic == NULL)
		return;
	fprintf(stderr, "Poco module diagnostic (%d)%s%s%s%s\n", diagnostic->status,
		diagnostic->source_name != NULL ? " " : "",
		diagnostic->source_name != NULL ? diagnostic->source_name : "",
		diagnostic->message != NULL ? ": " : "",
		diagnostic->message != NULL ? diagnostic->message : "");
}

static const void* poco_generic_module_get_service(const PocoModuleHost* host,
	const char* service_name, uint32_t service_abi_version)
{
	(void)host;
	(void)service_name;
	(void)service_abi_version;
	return NULL;
}

static const PocoModuleHost poco_generic_module_host = {
	POCO_MODULE_HOST_ABI_VERSION,
	NULL,
	poco_generic_module_report_diagnostic,
	poco_generic_module_get_service,
};

static Errcode poco_generic_library_initialize(Poco_lib* legacy_library)
{
	PocoModule* module = legacy_library != NULL ? legacy_library->local_data : NULL;
	PocoStatus status;

	if (module == NULL)
		return Err_poco_internal;
	if (module->library.initialize == NULL) {
		module->library_initialized = 1;
		return Success;
	}
	status = module->library.initialize(&module->library);
	if (status == POCO_STATUS_OK)
		module->library_initialized = 1;
	return (Errcode)status;
}

static void poco_generic_library_cleanup(Poco_lib* legacy_library)
{
	PocoModule* module = legacy_library != NULL ? legacy_library->local_data : NULL;

	if (module != NULL && module->library_initialized && module->library.cleanup != NULL)
		module->library.cleanup(&module->library);
	if (module != NULL)
		module->library_initialized = 0;
}

static void poco_free_generic_module(PocoModule* module, bool call_cleanup)
{
	if (module == NULL)
		return;
	if (call_cleanup && module->descriptor != NULL &&
		module->descriptor->cleanup != NULL)
		module->descriptor->cleanup(module->module_data);
	free(module->prototypes);
	free(module);
}

static Errcode poco_load_generic_module(Poco_lib** out_library, void* handle,
	const char* name, const char* id_string, bool verbose,
	const PocoModuleHooks* hooks, const PocoModuleInfo* info)
{
	PocoModuleEntryPoint get_descriptor;
	const PocoModuleDescriptor* descriptor;
	PocoModule* module = NULL;
	Poco_lib_loaded* loaded = NULL;
	PocoStatus init_status;
	size_t index;
	bool module_initialize_called = false;

	if (out_library == NULL || handle == NULL)
		return Err_null_ref;
	*out_library = NULL;
	get_descriptor = (PocoModuleEntryPoint)poco_dlsym(handle, POCO_MODULE_ENTRY_POINT);
	if (get_descriptor == NULL)
		return Err_poco_lib_no_entry;

	descriptor = get_descriptor();
	if (descriptor == NULL || descriptor->identity == NULL || descriptor->library == NULL ||
		descriptor->library->identity == NULL) {
		format_poco_lib_error(Err_poco_lib_invalid, name, NULL, NULL, 0, 0, -1, verbose);
		return Err_poco_lib_invalid;
	}
	if (descriptor->abi_version != POCO_MODULE_ABI_VERSION) {
		format_poco_lib_error(Err_poco_lib_version, name, NULL, NULL,
			(int)POCO_MODULE_ABI_VERSION, (int)descriptor->abi_version, -1, verbose);
		return Err_poco_lib_version;
	}
	if (descriptor->library->bindings == NULL || descriptor->library->binding_count == 0) {
		format_poco_lib_error(Err_poco_lib_empty, name, NULL, NULL,
			0, 0, (int)descriptor->library->binding_count, verbose);
		return Err_poco_lib_empty;
	}
	if (descriptor->library->binding_count > INT_MAX) {
		format_poco_lib_error(Err_poco_lib_invalid, name, NULL, NULL, 0, 0, -1, verbose);
		return Err_poco_lib_invalid;
	}
	if (id_string != NULL && strcmp(id_string, descriptor->identity) != 0) {
		format_poco_lib_error(Err_poco_lib_invalid, name, NULL, NULL, 0, 0, -1, verbose);
		return Err_poco_lib_invalid;
	}

	module = calloc(1, sizeof(*module));
	if (module == NULL)
		return Err_no_memory;
	module->descriptor = descriptor;
	module->library = *descriptor->library;
	module->prototypes = calloc(module->library.binding_count, sizeof(*module->prototypes));
	if (module->prototypes == NULL)
		goto OUT_OF_MEMORY;
	for (index = 0; index < module->library.binding_count; ++index) {
		if (module->library.bindings[index].prototype == NULL ||
			module->library.bindings[index].function == NULL) {
			format_poco_lib_error(Err_poco_lib_invalid, name, NULL, NULL, 0, 0, -1, verbose);
			poco_free_generic_module(module, false);
			return Err_poco_lib_invalid;
		}
		module->prototypes[index].proto = (char*)module->library.bindings[index].prototype;
		module->prototypes[index].func = (void*)module->library.bindings[index].function;
	}

	if (descriptor->initialize != NULL) {
		module_initialize_called = true;
		init_status = descriptor->initialize(&poco_generic_module_host, &module->module_data);
		if (init_status != POCO_STATUS_OK) {
			poco_free_generic_module(module, true);
			return (Errcode)init_status;
		}
	}

	module->legacy_library.name = (char*)module->library.identity;
	module->legacy_library.lib = module->prototypes;
	module->legacy_library.count = (int)module->library.binding_count;
	module->legacy_library.init = poco_generic_library_initialize;
	module->legacy_library.cleanup = poco_generic_library_cleanup;
	module->legacy_library.local_data = module;

	loaded = poco_make_loaded_module(handle, hooks, info);
	if (loaded == NULL)
		goto OUT_OF_MEMORY_AFTER_INIT;
	loaded->generic_module = module;
	module->legacy_library.rexhead = loaded;
	*out_library = &module->legacy_library;
	return Success;

OUT_OF_MEMORY_AFTER_INIT:
	if (module_initialize_called)
		poco_free_generic_module(module, true);
	else
		poco_free_generic_module(module, false);
	return Err_no_memory;
OUT_OF_MEMORY:
	poco_free_generic_module(module, false);
	return Err_no_memory;
}

Errcode pj_load_pocorex(Poco_lib **lib, const char* script_path, char *name, char *id_string, bool verbose)
{
	Errcode err = Success;
	char* lib_path = NULL;
	void* handle = NULL;
	Poco_rexlib_get_func get_func = NULL;
	Pocorex* exe = NULL;
	Poco_lib_loaded* loaded = NULL;
	PocoModuleInfo module_info;
	PocoModuleHooks loaded_hooks;
	bool init_called = false;  /* Track whether init() was successfully called */
	bool module_load_hook_succeeded = false;
	
	if (lib == NULL || name == NULL) {
		return Err_null_ref;
	}
	
	lib_path = poco_find_library_file(script_path, name, verbose);
	if (lib_path == NULL) {
		format_poco_lib_error(Err_poco_lib_not_found, name, NULL, NULL, 0, 0, -1, verbose);
		return Err_poco_lib_not_found;
	}
	
	if (verbose) {
		fprintf(stderr, "[poco library] Loading library '%s' -> resolved to '%s'\n", name, lib_path);
	}
	module_info.requested_name = name;
	module_info.resolved_path = lib_path;
	memset(&loaded_hooks, 0, sizeof(loaded_hooks));
	
#ifdef _WIN32
	handle = poco_dlopen(lib_path, 0);
#else
	handle = poco_dlopen(lib_path, RTLD_LAZY);
#endif
	if (handle == NULL) {
		const char* err_msg = poco_dlerror();
		format_poco_lib_error(Err_poco_lib_load_failed, name, lib_path, err_msg, 0, 0, -1, verbose);
		return Err_poco_lib_load_failed;
	}

	err = poco_call_module_load_hook(&poco_active_module_hooks, &module_info,
		&module_load_hook_succeeded);
	if (err < Success)
		goto error;
	if (module_load_hook_succeeded)
		loaded_hooks = poco_active_module_hooks;

	/* Prefer the generic, host-neutral descriptor.  A module that exports the
	 * new entry point is never interpreted as a legacy module if descriptor
	 * validation fails: that makes the migration boundary deterministic. */
	if (poco_dlsym(handle, POCO_MODULE_ENTRY_POINT) != NULL) {
		err = poco_load_generic_module(lib, handle, name, id_string, verbose,
			&loaded_hooks, &module_info);
		if (err < Success)
			goto error;
		return Success;
	}

	/* Compatibility path only: existing Animator/POE modules may continue to
	 * export poco_rexlib_get while they migrate to PocoModuleDescriptor.  The
	 * generic loader never creates an Animator function table, so an explicit
	 * Ani-owned policy must both opt in and install that table from on_load. */
	get_func = (Poco_rexlib_get_func)poco_dlsym(handle, "poco_rexlib_get");
	if (get_func == NULL) {
		format_poco_lib_error(Err_poco_lib_no_entry, name, lib_path, NULL, 0, 0, -1, verbose);
		err = Err_poco_lib_no_entry;
		goto error;
	}
	if (!poco_active_module_hooks.allow_legacy_poe ||
		poco_active_module_hooks.on_load == NULL) {
		fprintf(stderr, "Error: Poco library '%s' is a legacy native-POE module; "
			"generic Poco requires an explicit Animator compatibility policy\n", name);
		err = Err_poco_lib_no_entry;
		goto error;
	}
	
	exe = get_func();
	if (exe == NULL) {
		format_poco_lib_error(Err_poco_lib_invalid, name, lib_path, NULL, 0, 0, -1, verbose);
		err = Err_poco_lib_invalid;
		goto error;
	}
	
	if (exe->hdr.version != POCOREX_VERSION) {
		format_poco_lib_error(Err_poco_lib_version, name, lib_path, NULL,
		                      POCOREX_VERSION, exe->hdr.version, -1, verbose);
		err = Err_poco_lib_version;
		goto error;
	}
	
	if (exe->lib.lib == NULL || exe->lib.count == 0) {
		format_poco_lib_error(Err_poco_lib_empty, name, lib_path, NULL,
		                      0, 0, exe->lib.count, verbose);
		err = Err_poco_lib_empty;
		goto error;
	}
	
	if (id_string != NULL && exe->hdr.id_string != NULL) {
		if (strcmp(id_string, exe->hdr.id_string) != 0) {
			err = Err_poco_lib_invalid;
			goto error;
		}
	}
	
	if (exe->hdr.init != NULL) {
		err = exe->hdr.init((void*)exe, NULL);
		if (err < Success) {
			/* Init failed - cleanup will be called in error path */
			init_called = true;  /* Mark init as called even though it failed */
			goto error;
		}
		init_called = true;  /* Init succeeded */
	}
	
	loaded = poco_make_loaded_module(handle, &loaded_hooks, &module_info);
	if (loaded == NULL) {
		err = Err_no_memory;
		/* If init was called and malloc fails, we need cleanup before goto error */
		if (init_called && exe->hdr.cleanup != NULL) {
			exe->hdr.cleanup((void*)exe);
			init_called = false;  /* Cleanup done, don't do it again in error path */
		}
		goto error;
	}
	
	loaded->exe = exe;
	
	exe->lib.rexhead = (void*)loaded;
	*lib = &exe->lib;
	return Success;
	
error:
	/*****************************************************************************
	 * Error cleanup path
	 * 
	 * This section ensures proper cleanup of all allocated resources when
	 * library loading fails. The cleanup order is critical:
	 * 
	 * 1. Call library's cleanup function (if init was called successfully)
	 *    - Only called if init_called is true
	 *    - This allows the library to clean up its own internal state
	 * 
	 * 2. Close the dynamic library handle (if dlopen succeeded)
	 *    - Must be done after calling cleanup, since cleanup is in the library
	 *    - Prevents handle leaks on error paths
	 * 
	 * Note: We don't free 'loaded' here because it's only allocated at the very
	 * end, after all error-prone operations. If we reach 'error:' label, either:
	 * - loaded is NULL (not yet allocated), or
	 * - loaded was allocated but we already cleaned it up before goto error
	 * 
	 * The 'exe' pointer points into the library's static data, so we never free it.
	 ****************************************************************************/
	if (init_called && exe != NULL && exe->hdr.cleanup != NULL) {
		exe->hdr.cleanup((void*)exe);
	}
	if (module_load_hook_succeeded)
		poco_call_module_unload_hook(&poco_active_module_hooks, &module_info);
	if (handle != NULL) {
		poco_dlclose(handle);
	}
	return err;
}

void pj_free_pocorexes(Poco_lib **libs)
{
	Poco_lib *lib, *next;
	Poco_lib_loaded* loaded;
	
	if (libs == NULL) {
		return;
	}
	
	next = *libs;
	while ((lib = next) != NULL) {
		next = lib->next;
		
		if (lib->rexhead != NULL) {
			loaded = (Poco_lib_loaded*)lib->rexhead;

			if (loaded->generic_module != NULL) {
				poco_free_generic_module(loaded->generic_module, true);
			}
			else if (loaded->exe != NULL && loaded->exe->hdr.cleanup != NULL) {
				loaded->exe->hdr.cleanup((void*)loaded->exe);
			}

			poco_call_module_unload_hook(&loaded->hooks, &loaded->info);
			if (loaded->handle != NULL) {
				poco_dlclose(loaded->handle);
			}

			poco_free_loaded_module(loaded);
		}
	}
	*libs = NULL;
}
