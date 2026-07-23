#include <poco/poco.h>

static int generic_module_initialized;

static int generic_answer(PocoVm* vm)
{
	return generic_module_initialized == 1 && vm != NULL ? 42 : -1;
}

static PocoStatus generic_module_initialize(const PocoModuleHost* host, void** out_module_data)
{
	PocoDiagnostic diagnostic;

	if (host == NULL || out_module_data == NULL ||
		host->abi_version != POCO_MODULE_HOST_ABI_VERSION || host->report_diagnostic == NULL ||
		host->get_service == NULL) {
		return POCO_STATUS_INTERNAL_ERROR;
	}
	/* A generic module may query an optional service, but does not acquire an
	 * Animator table or rely on a host-specific global. */
	(void)host->get_service(host, "poco.module.fixture", 1u);
	diagnostic.status = POCO_STATUS_OK;
	diagnostic.source_name = "generic-module";
	diagnostic.line = 0;
	diagnostic.column = 0;
	diagnostic.message = "initialized";
	host->report_diagnostic(host, &diagnostic);
	generic_module_initialized = 1;
	*out_module_data = &generic_module_initialized;
	return POCO_STATUS_OK;
}

static void generic_module_cleanup(void* module_data)
{
	if (module_data == &generic_module_initialized) {
		generic_module_initialized = 0;
	}
}

static const PocoBinding generic_bindings[] = {
	{"int GenericAnswer(void);", (PocoNativeFunction)generic_answer, NULL,
	 POCO_BINDING_RUN_CONTEXT},
};

static const PocoLibrary generic_library = {
	"generic-module",
	generic_bindings,
	sizeof(generic_bindings) / sizeof(generic_bindings[0]),
	NULL,
	NULL,
	NULL,
};

static const PocoModuleDescriptor generic_module = {
	POCO_MODULE_ABI_VERSION,   "generic-module",       &generic_library,
	generic_module_initialize, generic_module_cleanup,
};

POCO_MODULE_EXPORT const PocoModuleDescriptor* poco_module_get(void)
{
	return &generic_module;
}
