#include <poco/poco.h>

#include "ani_poco_adapter.h"
#include "poco_array.h"

#include <stdio.h>
#include <string.h>

#define FIXTURE_PATH(name) ANI_POCO_REGISTRATION_FIXTURE_DIR "/" name

static void report_diagnostic(void* user_data, const PocoDiagnostic* diagnostic)
{
	(void)user_data;
	if (diagnostic != NULL && diagnostic->message != NULL) {
		fprintf(stderr, "ani_poco_registration diagnostic: %s\n", diagnostic->message);
	}
}

/*
 * Sums the bytes of every string in a char** array, decoding each element with
 * po_array_str() (the same path the UI bindings use).  char_array_runtime.poc
 * checks the sum to catch char** marshalling regressions.
 */
static int ani_test_str_checksum(char** strs, int count)
{
	int sum = 0;
	int i;

	if (strs == NULL) {
		return -1;
	}
	for (i = 0; i < count; ++i) {
		const char* s = po_array_str(strs, i);
		if (s == NULL) {
			return -1;
		}
		while (*s != '\0') {
			sum += (unsigned char)*s++;
		}
	}
	return sum;
}

static const PocoBinding ani_test_bindings[] = {
	{"int TestStrChecksum(char **strs, int count);", (PocoNativeFunction)ani_test_str_checksum,
	 NULL},
};

static const PocoLibrary ani_test_library = {
	"Test String Array", ani_test_bindings, 1, NULL, NULL, NULL,
};

int main(int argc, char** argv)
{
	PocoVm* vm = NULL;
	PocoProgram* program = NULL;
	PocoStatus status;
	PocoVmOptions options = {0};
	int32_t result = -1;
	const char* stage = "create VM";
	const char* script_path = FIXTURE_PATH("animator_bindings.poc");

	if (argc == 3 && strcmp(argv[1], "--write-inventory") == 0) {
		status = ani_poco_write_library_inventory(argv[2]);
		if (status != POCO_STATUS_OK) {
			fprintf(stderr, "ani_poco_registration: write inventory: status=%d\n", status);
			return 1;
		}
		return 0;
	}
	if (argc > 2) {
		fprintf(stderr, "usage: %s [script.poc] | --write-inventory file\n", argv[0]);
		return 2;
	}
	if (argc == 2) {
		script_path = argv[1];
	}

	options.diagnostic_callback = report_diagnostic;
	/* Optional test scripts may load retained Animator-native POE modules. */
	ani_poco_configure_legacy_poe(&options);
	status = poco_vm_create(&options, &vm);
	if (status == POCO_STATUS_OK) {
		stage = "register Animator libraries";
		status = ani_poco_register_libraries(vm);
	}
	if (status == POCO_STATUS_OK) {
		stage = "register test bindings";
		status = poco_vm_register_library(vm, &ani_test_library);
	}
	if (status == POCO_STATUS_OK) {
		stage = "compile Animator script";
		status = poco_vm_compile_file(vm, script_path, &program);
	}
	if (status == POCO_STATUS_OK) {
		stage = "run Animator script";
		status = poco_vm_run(vm, program, NULL, &result);
	}
	if (status != POCO_STATUS_OK || result != 0) {
		fprintf(stderr, "ani_poco_registration: %s: status=%d result=%d\n", stage, status, result);
		poco_program_destroy(program);
		poco_vm_destroy(vm);
		return 1;
	}

	poco_program_destroy(program);
	poco_vm_destroy(vm);
	return 0;
}
