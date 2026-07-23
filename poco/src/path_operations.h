#ifndef POCO_PATH_OPERATIONS_H
#define POCO_PATH_OPERATIONS_H

#include <stddef.h>

#include "filepath.h"
#include "poco_errcodes.h"
#include "pocolib.h"

/*
 * Capacity-aware, Poco-owned path operations.  Every destination capacity
 * includes room for the trailing NUL.  A failed operation leaves every
 * destination unchanged.
 */
int poco_path_split(const char* path, char* device, size_t device_capacity, char* dir,
					size_t dir_capacity, char* file, size_t file_capacity, char* suffix,
					size_t suffix_capacity);

int poco_path_merge(char* path, size_t path_capacity, const char* device, const char* dir,
					const char* file, const char* suffix);

/*
 * The DOS-contract fnsplit/fnmerge bindings.  The embeddable standard path
 * library and the standalone CLI's legacy library list both draw from this one
 * table so the two registration ABIs stay in step.
 */
#define POCO_PATH_LEGACY_BINDING_COUNT 2
extern Lib_proto poco_path_legacy_bindings[POCO_PATH_LEGACY_BINDING_COUNT];

#endif /* POCO_PATH_OPERATIONS_H */
