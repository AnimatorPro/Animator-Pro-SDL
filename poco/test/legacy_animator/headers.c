/*
 * Compile-only contract for Animator's temporary Poco compatibility headers.
 * New hosts must include <poco/poco.h> directly; this fixture protects the
 * legacy boundary until Animator finishes migrating its native libraries.
 */
#include <poco/poco.h>
#include "pocoface.h"
#include "pocolib.h"
#include "pocorex.h"

static void legacy_noop(void)
{
}

static Lib_proto legacy_bindings[] = {

	{legacy_noop, "void LegacyNoop(void);"},
};

static Poco_lib legacy_library = {

	.name = "legacy-header-fixture",
	.lib = legacy_bindings,
	.count = (int)(sizeof(legacy_bindings) / sizeof(legacy_bindings[0])),
};

_Static_assert(sizeof(Popot) == sizeof(void*) * 3, "the legacy headers must use canonical Popot");

int main(void)
{
	PocoLibrary canonical_library = {

		.identity = "canonical-header-fixture",
		.bindings = NULL,
		.binding_count = 0,
	};
	Errcode (*legacy_compile)(void**, char*, char*, char*, Poco_lib*, char*, long*, int*, Names*,
							  bool) = compile_poco;
	Errcode (*legacy_run)(void**, char*, bool (*)(void*), void*, long*) = run_poco;
	void (*legacy_free)(void**) = free_poco;
	Poco_rexlib_get_func legacy_entry = NULL;

	(void)canonical_library;
	(void)legacy_library;
	(void)legacy_compile;
	(void)legacy_run;
	(void)legacy_free;
	(void)legacy_entry;
	return 0;
}
