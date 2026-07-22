/*
 * Standalone CLI legacy DOS library for Poco.
 *
 * The command-line driver registers its built-in functions through the legacy
 * Poco_lib list.  This exposes fnsplit/fnmerge to that list by wrapping the
 * host-neutral DOS-contract binding table defined in path_operations.c, so the
 * standalone shell and the embeddable standard path library share one source of
 * truth for the bindings.
 */

#include <stddef.h>

#include "pocolib.h"
#include "path_operations.h"

Poco_lib po_dos_standalone_lib = {
	NULL,
	"DOS",
	poco_path_legacy_bindings,
	POCO_PATH_LEGACY_BINDING_COUNT,
};
