/*****************************************************************************
 * src/pocorex.c - Library loading for main Animator Pro application
 *
 * NOTE: This file contains code similar to poco/src/pocoload.c. The
 * duplication is intentional because:
 * - This version is compiled into the main 'ani' executable
 * - It uses ani-specific functions like make_resource_name()
 * - The standalone poco executable has its own version in poco/src/pocoload.c
 *
 * If you modify library loading logic, consider updating both files.
 ****************************************************************************/

#include "errcodes.h"
#include "filepath.h"
#include "resource.h"
#include "pocorex.h"
#include "pocolib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#include <limits.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
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

#endif

/*****************************************************************************
 * Format and output a library loading error message.
 *
 * This utility function provides consistent error messaging for library
 * loading failures across the codebase.
 ****************************************************************************/
void format_poco_lib_error(Errcode err, const char* libname, const char* lib_path,
						   const char* sys_error, int expected_version, int actual_version,
						   int count, bool verbose)
{
	switch (err) {
		case Err_poco_lib_not_found:
			fprintf(stderr, "Error: Poco library '%s' not found in search paths\n", libname);
			fprintf(stderr,
					"  Searched: current working directory, resource directory, and executable "
					"directory\n");
			break;

		case Err_poco_lib_load_failed:
			fprintf(stderr, "Error: Failed to load poco library '%s'", libname);
			if (lib_path != NULL) {
				fprintf(stderr, " from '%s'", lib_path);
			}
			fprintf(stderr, "\n");
			if (sys_error != NULL) {
#ifdef _WIN32
				fprintf(stderr, "  Windows error code: %s\n", sys_error);
#else
				fprintf(stderr, "  System error: %s\n", sys_error);
#endif
			}
			break;

		case Err_poco_lib_no_entry:
			fprintf(stderr, "Error: Poco library '%s' is missing entry point 'poco_rexlib_get'\n",
					libname);
			fprintf(stderr, "  Make sure the library exports this symbol\n");
			break;

		case Err_poco_lib_invalid:
			fprintf(stderr, "Error: Poco library '%s' entry point returned NULL\n", libname);
			fprintf(stderr, "  Library structure is invalid\n");
			break;

		case Err_poco_lib_version:
			fprintf(stderr, "Error: Poco library '%s' version mismatch\n", libname);
			if (expected_version > 0 && actual_version > 0) {
				fprintf(stderr, "  Expected version: %d, Library version: %d\n", expected_version,
						actual_version);
			}
			break;

		case Err_poco_lib_empty:
			fprintf(stderr, "Error: Poco library '%s' contains no functions\n", libname);
			if (count >= 0) {
				fprintf(stderr, "  Library count: %d, Library pointer: %s\n", count,
						(count == 0) ? "NULL or empty" : "valid");
			}
			break;

		default:
			fprintf(stderr, "Error: Unknown library loading error for '%s' (code %d)\n", libname,
					err);
			break;
	}
}

typedef struct {
	void* handle;
	Pocorex* exe;
} Poco_lib_loaded;

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

static char* try_load_path(const char* base_dir, const char* libname, char* out_path)
{
	const char* extensions[] = {".poe", get_platform_extension(), NULL};
	const char* ext_ptr;
	int ext_idx = 0;

	if (libname == NULL || strlen(libname) == 0) {
		return NULL;
	}

	const char* existing_ext = strrchr(libname, '.');
	int has_extension =
		(existing_ext != NULL && existing_ext != libname && strlen(existing_ext) > 1);

	if (has_extension) {
		snprintf(out_path, PATH_SIZE, "%s%s", base_dir, libname);
		FILE* test_file = fopen(out_path, "r");
		if (test_file != NULL) {
			fclose(test_file);
			return out_path;
		}
		return NULL;
	}

	while ((ext_ptr = extensions[ext_idx++]) != NULL) {
		snprintf(out_path, PATH_SIZE, "%s%s%s", base_dir, libname, ext_ptr);

		FILE* test_file = fopen(out_path, "r");
		if (test_file != NULL) {
			fclose(test_file);
			return out_path;
		}
	}

	return NULL;
}

Errcode pj_load_pocorex(Poco_lib** lib, const char* script_path, char* name, char* id_string,
						bool verbose)
/*****************************************************************************
 *
 ****************************************************************************/
{
	Errcode err = Success;
	char* lib_path = NULL;
	void* handle = NULL;
	Poco_rexlib_get_func get_func = NULL;
	Pocorex* exe = NULL;
	Poco_lib_loaded* loaded = NULL;
	bool init_called = false; /* Track whether init() was successfully called */

	if (lib == NULL || name == NULL) {
		return Err_null_ref;
	}

	/* Use local resolver that tries known locations; prefer script directory first */
	{
		char dir_path[PATH_MAX];
		char test_path[PATH_MAX];
		const char* candidate = NULL;
		if (script_path != NULL) {
			get_directory_from_path(script_path, dir_path, sizeof(dir_path));
			candidate = try_load_path(dir_path, name, test_path);
		}
		if (candidate == NULL) {
			if (getcwd(dir_path, sizeof(dir_path)) != NULL) {
				size_t len = strlen(dir_path);
				if (len > 0 && dir_path[len - 1] != '/' && dir_path[len - 1] != '\\') {
					strcat(dir_path, "/");
				}
				candidate = try_load_path(dir_path, name, test_path);
			}
		}
#ifdef _WIN32
		if (candidate == NULL) {
			char exe_path[PATH_MAX];
			DWORD elen = GetModuleFileNameA(NULL, exe_path, sizeof(exe_path));
			if (elen > 0) {
				get_directory_from_path(exe_path, dir_path, sizeof(dir_path));
				candidate = try_load_path(dir_path, name, test_path);
			}
		}
#else
		if (candidate == NULL) {
			char exe_path[PATH_MAX];
			ssize_t elen = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
			if (elen > 0) {
				exe_path[elen] = '\0';
				get_directory_from_path(exe_path, dir_path, sizeof(dir_path));
				candidate = try_load_path(dir_path, name, test_path);
			}
		}
#ifdef __APPLE__
		if (candidate == NULL) {
			char bundle_path[PATH_MAX * 2];
			uint32_t bundle_size = sizeof(bundle_path);
			if (_NSGetExecutablePath(bundle_path, &bundle_size) == 0) {
				get_directory_from_path(bundle_path, dir_path, sizeof(dir_path));
				candidate = try_load_path(dir_path, name, test_path);
			}
		}
#endif
#endif
		lib_path = (char*)candidate;
	}
	if (lib_path == NULL) {
		format_poco_lib_error(Err_poco_lib_not_found, name, NULL, NULL, 0, 0, -1, verbose);
		return Err_poco_lib_not_found;
	}

#ifdef _WIN32
	handle = poco_dlopen(lib_path, 0);
#else
	handle = poco_dlopen(lib_path, RTLD_LAZY);
#endif
	if (handle == NULL) {
#ifdef _WIN32
		DWORD err_code = GetLastError();
		char err_buf[32];
		snprintf(err_buf, sizeof(err_buf), "%lu", err_code);
		format_poco_lib_error(Err_poco_lib_load_failed, name, lib_path, err_buf, 0, 0, -1, verbose);
#else
		const char* err_msg = dlerror();
		format_poco_lib_error(Err_poco_lib_load_failed, name, lib_path, err_msg, 0, 0, -1, verbose);
#endif
		return Err_poco_lib_load_failed;
	}

	get_func = (Poco_rexlib_get_func)poco_dlsym(handle, "poco_rexlib_get");
	if (get_func == NULL) {
		format_poco_lib_error(Err_poco_lib_no_entry, name, lib_path, NULL, 0, 0, -1, verbose);
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
		format_poco_lib_error(Err_poco_lib_version, name, lib_path, NULL, POCOREX_VERSION,
							  exe->hdr.version, -1, verbose);
		err = Err_poco_lib_version;
		goto error;
	}

	if (exe->lib.lib == NULL || exe->lib.count == 0) {
		format_poco_lib_error(Err_poco_lib_empty, name, lib_path, NULL, 0, 0, exe->lib.count,
							  verbose);
		err = Err_poco_lib_empty;
		goto error;
	}

	if (id_string != NULL && exe->hdr.id_string != NULL) {
		if (strcmp(id_string, exe->hdr.id_string) != 0) {
			err = Err_rexlib_usertype;
			goto error;
		}
	}

	if (exe->hdr.init != NULL) {
		err = exe->hdr.init((void*)exe, NULL);
		if (err < Success) {
			/* Init failed - cleanup will be called in error path */
			init_called = true; /* Mark init as called even though it failed */
			goto error;
		}
		init_called = true; /* Init succeeded */
	}

	loaded = (Poco_lib_loaded*)malloc(sizeof(Poco_lib_loaded));
	if (loaded == NULL) {
		err = Err_no_memory;
		/* If init was called and malloc fails, we need cleanup before goto error */
		if (init_called && exe->hdr.cleanup != NULL) {
			exe->hdr.cleanup((void*)exe);
			init_called = false; /* Cleanup done, don't do it again in error path */
		}
		goto error;
	}

	loaded->handle = handle;
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
	if (handle != NULL) {
		poco_dlclose(handle);
	}
	return err;
}

void pj_free_pocorexes(Poco_lib** libs)
/*****************************************************************************
 * Free a singly linked list of loaded poco REX libraries.
 ****************************************************************************/
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

			if (loaded->exe != NULL && loaded->exe->hdr.cleanup != NULL) {
				loaded->exe->hdr.cleanup((void*)loaded->exe);
			}

			if (loaded->handle != NULL) {
				poco_dlclose(loaded->handle);
			}

			free(loaded);
		}
	}
	*libs = NULL;
}
