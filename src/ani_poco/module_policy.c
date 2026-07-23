#include <poco/poco.h>

#include "jimk.h"
#include "ani_poco_adapter.h"

/* Legacy Animator bindings still communicate through this host-owned slot.
 * Full Animator provides the strong definition in pjhigh/startup.c; adapter
 * fixtures use this weak fallback.  Poco core neither defines nor reads it. */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak)) Errcode builtin_err;
#endif

/* This Animator-private fallback remains invisible to Poco core. */
extern void animhost_ensure_pocolib(void);

static PocoStatus install_legacy_poe_policy(void* user_data, const PocoModuleInfo* module)
{
	(void)user_data;
	if (module == NULL || module->resolved_path == NULL) {
		return POCO_STATUS_NULL_REFERENCE;
	}

	animhost_ensure_pocolib();
	return POCO_STATUS_OK;
}

static const PocoModuleHooks legacy_poe_module_hooks = {
	.on_load = install_legacy_poe_policy,
	.on_unload = NULL,
	.user_data = NULL,
	.allow_legacy_poe = 1,
};

void ani_poco_configure_legacy_poe(PocoVmOptions* options)
{
	if (options != NULL) {
		options->module_hooks = &legacy_poe_module_hooks;
	}
}
