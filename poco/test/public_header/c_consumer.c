#include <poco/poco.h>

#if POCO_API_VERSION_MAJOR != 1
#error "Poco's first stable embedding API must be major version 1"
#endif

#if POCO_API_VERSION != POCO_MAKE_API_VERSION(1, 1, 0)
#error "Poco API version macros must encode the declared version"
#endif

static PocoVm *vm;
static PocoProgram *program;
static PocoModule *module;
static PocoBinding *binding;
static PocoLibrary *library;

int main(void)
{
    Popot pointer = {0};
    PocoStatus status = POCO_STATUS_OK;

    (void)vm;
    (void)program;
    (void)module;
    (void)binding;
    (void)library;
    (void)pointer;
    return status == POCO_STATUS_OK ? 0 : 1;
}
