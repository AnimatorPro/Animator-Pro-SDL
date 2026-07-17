#include <poco/poco.h>

int main()
{
    PocoVm *vm = nullptr;
    PocoProgram *program = nullptr;
    PocoModule *module = nullptr;
    Popot pointer{};

    (void)vm;
    (void)program;
    (void)module;
    (void)pointer;
    return POCO_STATUS_OK == 0 ? 0 : 1;
}
