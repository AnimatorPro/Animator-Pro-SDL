#include <stdio.h>

#include <poco/poco.h>
#include <pocoface.h>
#include <pocolib.h>

#define FIXTURE_PATH(name) POCO_CALLBACK_INVOKE_FIXTURE_DIR "/" name

static int callback_failure;

static Popot callback_popot(void* pointer)
{
	Popot value;

	value.pt = pointer;
	value.min = pointer;
	value.max = pointer;
	return value;
}

static int fail_callback(const char* message)
{
	if (!callback_failure) {
		fprintf(stderr, "poco_callback_invoke: %s\n", message);
	}
	callback_failure = 1;
	return -1;
}

static int invoke_callbacks(void* abort_handler, void* update_handler, void* overtime_handler,
							void* flic_handler)
{
	int abort_marker = 1;
	int update_marker = 2;
	int overtime_marker = 3;
	int flic_marker = 4;
	int userdata_marker = 5;
	Pt_num result;
	PocoCallbackValue abort_args[] = {
		{POCO_CALLBACK_VALUE_POPOT, {.popot_value = callback_popot(&abort_marker)}},
	};
	PocoCallbackValue update_args[] = {
		{POCO_CALLBACK_VALUE_POPOT, {.popot_value = callback_popot(&update_marker)}},
		{POCO_CALLBACK_VALUE_INT, {.int_value = 41}},
	};
	PocoCallbackValue overtime_args[] = {
		{POCO_CALLBACK_VALUE_DOUBLE, {.double_value = 0.25}},
		{POCO_CALLBACK_VALUE_POPOT, {.popot_value = callback_popot(&overtime_marker)}},
	};
	PocoCallbackValue flic_args[] = {
		{POCO_CALLBACK_VALUE_POPOT, {.popot_value = callback_popot(&flic_marker)}},
		{POCO_CALLBACK_VALUE_POPOT, {.popot_value = callback_popot(&userdata_marker)}},
		{POCO_CALLBACK_VALUE_LONG, {.long_value = 7}},
		{POCO_CALLBACK_VALUE_LONG, {.long_value = 8}},
		{POCO_CALLBACK_VALUE_LONG, {.long_value = 9}},
	};
	PocoCallbackValue invalid_arg = {
		(PocoCallbackValueKind)99,
		{.int_value = 0},
	};

	if (poco_invoke_callback(po_fuf_code(abort_handler), &result, abort_args, 1) != Success ||
		result.i != 1) {
		return fail_callback("abort callback result");
	}
	if (poco_invoke_callback(po_fuf_code(update_handler), &result, update_args, 2) != Success ||
		result.i != 42) {
		return fail_callback("UdQnumber callback result");
	}
	if (poco_invoke_callback(po_fuf_code(overtime_handler), &result, overtime_args, 2) != Success ||
		result.i != 43) {
		return fail_callback("OverTime callback result");
	}
	if (poco_invoke_callback(po_fuf_code(flic_handler), &result, flic_args, 5) != Success ||
		result.i != 1) {
		return fail_callback("FLIC callback result");
	}
	if (poco_invoke_callback(po_fuf_code(abort_handler), &result, &invalid_arg, 1) !=
		Err_parameter_range) {
		return fail_callback("invalid callback value must fail");
	}
	return 0;
}

int main(void)
{
	static const PocoBinding bindings[] = {
		{"int InvokeCallbacks(int (*abort_handler)(void *data),"
		 " int (*update_handler)(void *data, int value),"
		 " int (*overtime_handler)(double time, void *data),"
		 " int (*flic_handler)(void *flic, void *userdata, long loop,"
		 " long frame, long count));",
		 (PocoNativeFunction)invoke_callbacks},
	};
	static const PocoLibrary library = {
		"callback-invoke", bindings, 1, NULL, NULL, NULL,
	};
	PocoVm* vm = NULL;
	PocoProgram* program = NULL;
	int32_t result = -1;
	int status = 1;

	if (poco_vm_create(NULL, &vm) != POCO_STATUS_OK ||
		poco_vm_register_library(vm, &library) != POCO_STATUS_OK ||
		poco_vm_compile_file(vm, FIXTURE_PATH("callback_invoke.poc"), &program) != POCO_STATUS_OK ||
		poco_vm_run(vm, program, NULL, &result) != POCO_STATUS_OK || result != 0 ||
		callback_failure) {
		fprintf(stderr, "poco_callback_invoke: fixture failed: %s\n", poco_get_last_error(vm));
		goto CLEANUP;
	}
	status = 0;

CLEANUP:
	poco_program_destroy(program);
	poco_vm_destroy(vm);
	return status;
}
