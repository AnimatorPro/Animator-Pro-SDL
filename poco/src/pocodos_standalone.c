/*
 * Standalone DOS library for Poco: fnsplit and fnmerge.
 *
 * Provides real implementations for standalone Poco (no Animator host).
 * Both operations accept Unix/macOS and Windows path spellings.
 */

#include <ctype.h>
#include <stddef.h>
#include <string.h>

#include "pocolib.h"
#include "standard_library.h"
#include "path_operations.h"

/*
 * The script ABI predates sized pointers.  Its fnsplit outputs therefore use
 * the historical DOS component contract: device[3], dir[67], file[9], and
 * suffix[5].  The capacity-aware Poco operations above are used internally
 * and by native hosts that need arbitrary modern path component sizes.
 */
#define POCO_LEGACY_DEVICE_CAPACITY 3
#define POCO_LEGACY_DIR_CAPACITY 67
#define POCO_LEGACY_FILE_CAPACITY 9
#define POCO_LEGACY_SUFFIX_CAPACITY 5

static int poco_is_separator(char character)
{
	return character == '/' || character == '\\';
}

static int poco_component_fits(size_t length, size_t capacity)
{
	return capacity > length;
}

static int poco_merge_length(const char* component, size_t* total)
{
	size_t component_length = component != NULL ? strlen(component) : 0;

	if (component_length > (size_t)PATH_SIZE - 1 - *total)
		return Err_dir_too_long;
	*total += component_length;
	return Success;
}

int poco_path_split(const char* path,
	char* device, size_t device_capacity,
	char* dir, size_t dir_capacity,
	char* file, size_t file_capacity,
	char* suffix, size_t suffix_capacity)
{
	const char* component_start;
	const char* name_start;
	const char* suffix_start;
	const char* cursor;
	size_t path_length;
	size_t device_length = 0;
	size_t dir_length;
	size_t file_length;
	size_t suffix_length;

	if (path == NULL || *path == '\0' || device == NULL || dir == NULL
		|| file == NULL || suffix == NULL)
		return Err_null_ref;

	path_length = strlen(path);
	if (path_length >= PATH_SIZE)
		return Err_dir_too_long;

	if (path[1] == ':') {
		if (!isalpha((unsigned char)path[0]))
			return Err_no_device;
		device_length = 2;
	}

	component_start = path + device_length;
	name_start = component_start;
	for (cursor = component_start; *cursor != '\0'; ++cursor) {
		if (poco_is_separator(*cursor))
			name_start = cursor + 1;
	}
	dir_length = (size_t)(name_start - component_start);

	suffix_start = name_start;
	for (cursor = name_start; *cursor != '\0'; ++cursor) {
		if (*cursor == '.') {
			suffix_start = cursor;
			break;
		}
		suffix_start = cursor + 1;
	}
	file_length = (size_t)(suffix_start - name_start);
	suffix_length = path_length - device_length - dir_length - file_length;

	if (!poco_component_fits(device_length, device_capacity)
		|| !poco_component_fits(dir_length, dir_capacity)
		|| !poco_component_fits(file_length, file_capacity)
		|| !poco_component_fits(suffix_length, suffix_capacity))
		return Err_buf_too_small;

	memcpy(device, path, device_length);
	device[device_length] = '\0';
	memcpy(dir, component_start, dir_length);
	dir[dir_length] = '\0';
	memcpy(file, name_start, file_length);
	file[file_length] = '\0';
	memcpy(suffix, suffix_start, suffix_length);
	suffix[suffix_length] = '\0';
	return Success;
}

int poco_path_merge(char* path, size_t path_capacity,
	const char* device, const char* dir,
	const char* file, const char* suffix)
{
	const char* components[] = { device, dir, file, suffix };
	size_t component_count = sizeof(components) / sizeof(components[0]);
	size_t index;
	size_t total = 0;
	char* output;

	if (path == NULL)
		return Err_null_ref;

	for (index = 0; index < component_count; ++index) {
		int status = poco_merge_length(components[index], &total);
		if (status < Success)
			return status;
	}
	if (path_capacity <= total)
		return Err_buf_too_small;

	output = path;
	for (index = 0; index < component_count; ++index) {
		const char* component = components[index];
		if (component != NULL) {
			size_t component_length = strlen(component);
			memcpy(output, component, component_length);
			output += component_length;
		}
	}
	*output = '\0';
	return Success;
}

/*****************************************************************************
 * ErrCode fnsplit(char *path, char *device, char *dir, char *file, char *suf);
 *
 * Splits a path into device, dir, file, and suffix.  This legacy script entry
 * preserves the historical DOS-sized output contract documented above.
 ****************************************************************************/
static int po_fnsplit(char* path, char* device, char* dir, char* file, char* suffix)
{
	char split_device[POCO_LEGACY_DEVICE_CAPACITY];
	char split_dir[POCO_LEGACY_DIR_CAPACITY];
	char split_file[PATH_SIZE];
	char split_suffix[PATH_SIZE];
	int status;

	if (path == NULL || device == NULL || dir == NULL || file == NULL || suffix == NULL)
		return Err_null_ref;

	status = poco_path_split(path,
		split_device, sizeof(split_device),
		split_dir, sizeof(split_dir),
		split_file, sizeof(split_file),
		split_suffix, sizeof(split_suffix));
	if (status < Success)
		return status;

	/* Original fnsplit retained only 8.3-compatible file and suffix fields. */
	memcpy(device, split_device, strlen(split_device) + 1);
	memcpy(dir, split_dir, strlen(split_dir) + 1);
	strncpy(file, split_file, POCO_LEGACY_FILE_CAPACITY - 1);
	file[POCO_LEGACY_FILE_CAPACITY - 1] = '\0';
	strncpy(suffix, split_suffix, POCO_LEGACY_SUFFIX_CAPACITY - 1);
	suffix[POCO_LEGACY_SUFFIX_CAPACITY - 1] = '\0';
	return Success;
}

/*****************************************************************************
 * ErrCode fnmerge(char *path, char *device, char *dir, char *file, char *suf);
 *
 * Merges device, dir, file, and suffix into path.  The historical script ABI
 * cannot carry a pointer capacity, so callers must provide a destination that
 * can hold the merged string.  The capacity-aware poco_path_merge() contract
 * is available to native callers; this compatibility entry caps output at
 * PATH_SIZE and never uses unbounded concatenation.
 ****************************************************************************/
static int po_fnmerge(char* path, char* device, char* dir, char* file, char* suffix)
{
	return poco_path_merge(path, PATH_SIZE, device, dir, file, suffix);
}


//*****************************************************************************

static Lib_proto dos_lib[] = {
	{(void*)po_fnsplit, "ErrCode fnsplit(char *path, char *device, char *dir, char *file, char *suf);"},
	{(void*)po_fnmerge, "ErrCode fnmerge(char *path, char *device, char *dir, char *file, char *suf);"},
};

Poco_lib po_dos_standalone_lib = {
	NULL,
	"DOS",
	dos_lib,
	sizeof(dos_lib) / sizeof(dos_lib[0]),
};

const PocoLibrary *poco_standard_path_library(void)
{
	static PocoBinding bindings[Array_els(dos_lib)];
	static const PocoLibrary library = {
		POCO_STANDARD_PATH_LIBRARY_ID,
		bindings,
		Array_els(bindings),
		NULL,
		NULL,
		NULL,
	};
	static int initialized;
	size_t index;

	if (!initialized) {
		for (index = 0; index < Array_els(dos_lib); ++index) {
			bindings[index].prototype = dos_lib[index].proto;
			bindings[index].function = (PocoNativeFunction)dos_lib[index].func;
			bindings[index].contract = dos_lib[index].contract;
		}
		initialized = 1;
	}
	return &library;
}
