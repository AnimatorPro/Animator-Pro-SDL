/*****************************************************************************
 * POCOLoad.H - Header file for POE (Poco Extension) modules.
 *
 * This header defines the entry point function signature and structure
 * definitions required for creating loadable Poco libraries.
 *
 * POE modules should include this header to ensure compatibility with
 * the Poco library loading system.
 ****************************************************************************/

#ifndef POCOLoad_H
#define POCOLoad_H

#ifndef STDTYPES_H
#include "stdtypes.h"
#endif

#ifndef POCOLIB_H
#include "pocolib.h"
#endif

/*****************************************************************************
 * Forward declarations
 ****************************************************************************/

typedef struct pocorex Pocorex;

/*****************************************************************************
 * Entry point function signature for POE modules
 *
 * Each POE module MUST export a function with this exact signature:
 *
 *   Pocorex* poco_rexlib_get(void);
 *
 * This function should return a pointer to a statically allocated Pocorex
 * structure that contains the library definition (see pocorex.h for details).
 *
 * Example:
 *
 *   Pocorex* poco_rexlib_get(void) {
 *       return &rexlib_header;
 *   }
 *
 * IMPORTANT: This function must be exported as a public symbol:
 *   - Linux/macOS: Use __attribute__((visibility("default"))) or compile with -fvisibility=default
 *   - Windows: Use __declspec(dllexport)
 ****************************************************************************/

typedef Pocorex* (*Poco_rexlib_get_func)(void);

/*****************************************************************************
 * POE Module Requirements
 *
 * To create a loadable Poco library (POE module), you must:
 *
 * 1. Include the necessary headers:
 *    #include "pocoload.h"
 *    #include "pocorex.h"  (for Pocorex structure definition)
 *
 * 2. Define your library functions and prototypes:
 *    Lib_proto lib_calls[] = {
 *        {my_function1, "void MyFunc1(void);"},
 *        {my_function2, "int MyFunc2(int x);"},
 *    };
 *
 * 3. Set up the Pocorex structure using Setup_Pocorex macro:
 *    Setup_Pocorex(init_func, cleanup_func, "My Library Name", lib_calls);
 *
 * 4. Build as a shared library:
 *    - Linux: .so extension
 *    - macOS: .dylib extension
 *    - Windows: .dll extension
 *    - Or use .poe extension for cross-platform compatibility
 ****************************************************************************/

/*****************************************************************************
 * Library loading utilities
 ****************************************************************************/

/**
 * Find a Poco library file by searching standard locations.
 *
 * @param script_path Path to the script requesting the library (can be NULL)
 * @param libname Name of the library to find
 * @param library_dirs Host-added directories, searched in list order
 * @param verbose Whether to print search paths
 * @return Newly allocated path to the library file, or NULL if not found
 */
char* poco_find_library_file(const char* script_path, const char* libname,
							 const Names* library_dirs, bool verbose);

/**
 * Format and output a library loading error message.
 *
 * This utility function provides consistent error messaging for library
 * loading failures across the codebase.
 *
 * @param err Error code
 * @param libname Name of the library
 * @param lib_path Optional: resolved path to library file (can be NULL)
 * @param sys_error Optional: system error message from dlerror/GetLastError (can be NULL)
 * @param expected_version Optional: expected version number (0 if not applicable)
 * @param actual_version Optional: actual version number (0 if not applicable)
 * @param count Optional: function count for empty library error (-1 if not applicable)
 * @param verbose Whether verbose mode is enabled (affects some messages)
 */
void format_poco_lib_error(Errcode err, const char* libname, const char* lib_path,
						   const char* sys_error, int expected_version, int actual_version,
						   int count, bool verbose);

/* Loader entry points used by poco and by Animator when WITH_POCO */
Errcode pj_load_pocorex(Poco_lib** lib, const char* script_path, char* name, char* id_string,
						const Names* library_dirs, bool verbose, const PocoModuleHooks* hooks);
void pj_free_pocorexes(Poco_lib** libs);

/* Return the unresolved name from the source #pragma, never the resolved path. */
const char* poco_loaded_library_requested_name(const Poco_lib* library);

/* Activation-local wrapper state for an already loaded program module. */
Errcode poco_clone_loaded_library_runtime(const Poco_lib* source, Poco_lib* runtime);
void poco_reset_loaded_library_runtime(const Poco_lib* source, Poco_lib* runtime,
									   void* retained_local_data);
void poco_free_loaded_library_runtime(Poco_lib* runtime);

#endif /* POCOLoad_H */
