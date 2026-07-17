#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <poco/poco.h>

#define FIXTURE_PATH(name) POCO_UDQNUMBER_CALLBACK_FIXTURE_DIR "/" name

extern int po_UdSlider(int *inum, int min, int max, void *update,
	void *data, char *fmt, ...);

static int requester_failure;

bool show_mouse(void)
{
	return false;
}

bool hide_mouse(void)
{
	return false;
}

bool varg_qreq_number(int16_t *value, int16_t min, int16_t max,
	int (*update)(void *data, int16_t value), void *update_data,
	char *formats, char *text, va_list args)
{
	int update_result;
	int format_value;

	if (value == NULL || min != 1 || max != 99 || update == NULL ||
		formats != NULL || text == NULL || strcmp(text, "frame %d") != 0) {
		requester_failure = 1;
		return false;
	}
	format_value = va_arg(args, int);
	if (format_value != 73) {
		requester_failure = 1;
		return false;
	}
	update_result = update(update_data, 17);
	if (update_result != 19) {
		requester_failure = 1;
		return false;
	}
	*value = 17;
	return true;
}

int main(void)
{
	static const PocoBinding bindings[] = {
		{"Boolean UdQnumber(int *num, int min, int max,"
			" int (*update)(void *data, int num), void *data, char *fmt,...);",
			(PocoNativeFunction)po_UdSlider},
	};
	static const PocoLibrary library = {
		"udqnumber-callback", bindings, 1, NULL, NULL, NULL,
	};
	PocoVm *vm = NULL;
	PocoProgram *program = NULL;
	int32_t result = -1;
	int status = 1;

	if (poco_vm_create(NULL, &vm) != POCO_STATUS_OK ||
		poco_vm_register_library(vm, &library) != POCO_STATUS_OK ||
		poco_vm_compile_file(vm, FIXTURE_PATH("udqnumber_callback.poc"), &program) !=
			POCO_STATUS_OK ||
		poco_vm_run(vm, program, NULL, &result) != POCO_STATUS_OK ||
		result != 0 || requester_failure) {
		fprintf(stderr, "poco_udqnumber_callback: fixture failed\n");
		goto CLEANUP;
	}
	status = 0;

CLEANUP:
	poco_program_destroy(program);
	poco_vm_destroy(vm);
	return status;
}
