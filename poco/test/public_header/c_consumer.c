#include <poco/poco.h>

#if POCO_API_VERSION_MAJOR != 2
#error "Poco's binding-context embedding API must be major version 2"
#endif

#if POCO_API_VERSION != POCO_MAKE_API_VERSION(2, 0, 0)
#error "Poco API version macros must encode the declared version"
#endif

static PocoVm* vm;
static PocoProgram* program;
static PocoActivation* activation;
static PocoCall* call;
static PocoModule* module;
static PocoBinding* binding;
static PocoLibrary* library;

int main(void)
{
	Popot pointer = {0};
	PocoCallbackValue callback_value = {POCO_CALLBACK_VALUE_INVALID, {0}};
	PocoStatus status = POCO_STATUS_OK;

	(void)vm;
	(void)program;
	(void)activation;
	(void)call;
	(void)module;
	(void)binding;
	(void)library;
	(void)pointer;
	(void)callback_value;
	(void)sizeof(&poco_activation_run_main);
	(void)sizeof(&poco_call_begin);
	(void)sizeof(&poco_call_push_int);
	(void)sizeof(&poco_call_push_long);
	(void)sizeof(&poco_call_push_double);
	(void)sizeof(&poco_call_push_pointer);
	(void)sizeof(&poco_call_invoke);
	(void)sizeof(&poco_call_end);
	return status == POCO_STATUS_OK ? 0 : 1;
}
