#include <poco/poco.h>

#ifndef POCO_CONSUMER_SCRIPT
#error "POCO_CONSUMER_SCRIPT must name the package consumer script"
#endif

static int consumer_answer(void)
{
	return 42;
}

int main(void)
{
	static const PocoBinding bindings[] = {
		{"int ConsumerAnswer();", (PocoNativeFunction)consumer_answer, NULL},
	};
	PocoLibrary library = {
		"package-consumer", bindings, 1, NULL, NULL, NULL,
	};
	PocoVm* vm = NULL;
	PocoProgram* program = NULL;
	int32_t result = 0;
	PocoStatus status;

	if (POCO_API_VERSION_MAJOR != 1) {
		return 10;
	}
	status = poco_vm_create(NULL, &vm);
	if (status != POCO_STATUS_OK) {
		return 11;
	}
	status = poco_vm_register_library(vm, &library);
	if (status != POCO_STATUS_OK) {
		poco_vm_destroy(vm);
		return 12;
	}
	status = poco_vm_compile_file(vm, POCO_CONSUMER_SCRIPT, &program);
	if (status != POCO_STATUS_OK) {
		poco_vm_destroy(vm);
		return 13;
	}
	status = poco_vm_run(vm, program, NULL, &result);
	poco_program_destroy(program);
	poco_vm_destroy(vm);
	if (status != POCO_STATUS_OK) {
		return 14;
	}
	return result == 42 ? 0 : 15;
}
