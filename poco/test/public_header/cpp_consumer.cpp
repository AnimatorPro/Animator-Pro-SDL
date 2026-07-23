#include <poco/poco.h>

int main()
{
    PocoVm *vm = nullptr;
    PocoProgram *program = nullptr;
    PocoActivation *activation = nullptr;
    PocoModule *module = nullptr;
    Popot pointer{};

    (void)vm;
    (void)program;
    (void)activation;
    (void)module;
    (void)pointer;
    return POCO_STATUS_OK == 0 ? 0 : 1;
}
