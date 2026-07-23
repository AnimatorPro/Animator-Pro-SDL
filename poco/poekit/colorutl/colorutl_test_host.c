#include <poco/poco.h>

#include "ani_poco_adapter.h"

#include <stdio.h>

int main(int argc, char** argv)
{
	PocoVmOptions options = {0};
	PocoVm* vm = NULL;
	PocoProgram* program = NULL;
	PocoStatus status;
	int32_t result = 0;

	if (argc != 2) {
		fprintf(stderr, "usage: %s <script.poc>\n", argv[0]);
		return 2;
	}
	ani_poco_configure_legacy_poe(&options);
	status = poco_vm_create(&options, &vm);
	if (status == POCO_STATUS_OK) {
		status = poco_vm_compile_file(vm, argv[1], &program);
	}
	if (status == POCO_STATUS_OK) {
		status = poco_vm_run(vm, program, NULL, &result);
	}
	poco_program_destroy(program);
	poco_vm_destroy(vm);
	if (status != POCO_STATUS_OK) {
		fprintf(stderr, "Ani module policy host failed: %d\n", status);
		return 1;
	}
	if (result != 0) {
		fprintf(stderr, "Ani module policy script reported %d failure(s)\n", result);
		return 1;
	}
	return 0;
}
