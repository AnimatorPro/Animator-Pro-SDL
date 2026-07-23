#include <poco/poco.h>

#include <stdio.h>
#include <string.h>

#define FIXTURE_PATH(name) POCO_STANDARD_CATALOG_FIXTURE_DIR "/" name
#define SUCCESS_OUTPUT "poco-standard-catalog-success.txt"
#define FAILURE_OUTPUT "poco-standard-catalog-failure.txt"

static int check(int condition, const char* message)
{
	if (!condition) {
		fprintf(stderr, "poco_standard_catalog: %s\n", message);
		return 0;
	}
	return 1;
}

static int check_output(const char* path, const char* expected)
{
	char contents[128] = {0};
	FILE* file = fopen(path, "r");
	size_t length;

	if (file == NULL) {
		return check(0, "open output written by Poco program");
	}
	length = fread(contents, 1, sizeof(contents) - 1, file);
	fclose(file);
	contents[length] = '\0';
	return check(strcmp(contents, expected) == 0, "unclosed Poco file was flushed and closed");
}

int main(void)
{
	PocoVm* vm = NULL;
	PocoProgram* program = NULL;
	int32_t result = 0;
	PocoStatus failure_status;
	int ok = 1;

	remove(SUCCESS_OUTPUT);
	remove(FAILURE_OUTPUT);
	if (!check(poco_vm_create(NULL, &vm) == POCO_STATUS_OK, "create VM") ||
		!check(poco_vm_register_standard_library(vm) == POCO_STATUS_OK,
			   "register host-neutral standard catalog") ||
		!check(poco_vm_compile_file(vm, FIXTURE_PATH("success.poc"), &program) == POCO_STATUS_OK,
			   "compile full standard catalog fixture") ||
		!check(poco_vm_run(vm, program, NULL, &result) == POCO_STATUS_OK,
			   "run full standard catalog fixture") ||
		!check(result == 0, "full standard catalog fixture result")) {
		poco_program_destroy(program);
		poco_vm_destroy(vm);
		return 1;
	}
	ok &= check_output(SUCCESS_OUTPUT, "success");
	poco_program_destroy(program);
	program = NULL;

	if (!check(poco_vm_compile_file(vm, FIXTURE_PATH("failure.poc"), &program) == POCO_STATUS_OK,
			   "compile failure-cleanup fixture")) {
		poco_program_destroy(program);
		poco_vm_destroy(vm);
		return 1;
	}
	failure_status = poco_vm_run(vm, program, NULL, NULL);
	if (!check(failure_status != POCO_STATUS_OK, "report a runtime failure")) {
		poco_program_destroy(program);
		poco_vm_destroy(vm);
		return 1;
	}
	ok &= check_output(FAILURE_OUTPUT, "failure");
	poco_program_destroy(program);
	poco_vm_destroy(vm);
	remove(SUCCESS_OUTPUT);
	remove(FAILURE_OUTPUT);
	return ok ? 0 : 1;
}
