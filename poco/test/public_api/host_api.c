#include <poco/poco.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define FIXTURE_PATH(name) POCO_PUBLIC_API_FIXTURE_DIR "/" name

static int lifecycle_events[8];
static int lifecycle_event_count;
static int diagnostic_count;
static char diagnostic_message[512];

static int twice(int value)
{
	return value * 2;
}

static int with_run_context(int value, PocoVm* vm)
{
	return vm != NULL ? value : -1000;
}

static int with_variadic_run_context(int value, PocoVm* vm, ...)
{
	va_list args;
	int first;
	int second;

	if (vm == NULL) {
		return -1000;
	}
	va_start(args, vm);
	first = va_arg(args, int);
	second = va_arg(args, int);
	va_end(args);
	return value + first + second;
}

static int unused_one(void)
{
	return 1;
}

static PocoStatus first_library_init(PocoLibrary* library)
{
	(void)library;
	lifecycle_events[lifecycle_event_count++] = 1;
	return POCO_STATUS_OK;
}

static void first_library_cleanup(PocoLibrary* library)
{
	(void)library;
	lifecycle_events[lifecycle_event_count++] = 3;
}

static PocoStatus second_library_init(PocoLibrary* library)
{
	(void)library;
	lifecycle_events[lifecycle_event_count++] = 2;
	return POCO_STATUS_OK;
}

static void second_library_cleanup(PocoLibrary* library)
{
	(void)library;
	lifecycle_events[lifecycle_event_count++] = 4;
}

static void collect_diagnostic(void* user_data, const PocoDiagnostic* diagnostic)
{
	int* last_status = user_data;
	*last_status = diagnostic->status;
	if (diagnostic->message != NULL) {
		strncpy(diagnostic_message, diagnostic->message, sizeof(diagnostic_message) - 1);
		diagnostic_message[sizeof(diagnostic_message) - 1] = '\0';
	}
	++diagnostic_count;
}

static int cancel_immediately(void* user_data)
{
	int* calls = user_data;
	++*calls;
	return 1;
}

static int check(int condition, const char* message)
{
	if (!condition) {
		fprintf(stderr, "poco_public_api: %s\n", message);
		return 0;
	}
	return 1;
}

int main(void)
{
	static const PocoBinding first_bindings[] = {
		{"int twice(int value);", (PocoNativeFunction)twice},
		{"int with_run_context(int value);", (PocoNativeFunction)with_run_context, NULL,
		 POCO_BINDING_RUN_CONTEXT},
		{"int with_variadic_run_context(int value, ...);",
		 (PocoNativeFunction)with_variadic_run_context, NULL, POCO_BINDING_RUN_CONTEXT},
	};
	static const PocoBinding invalid_bindings[] = {
		{"int invalid_flags();", (PocoNativeFunction)unused_one, NULL, UINT32_MAX},
	};
	static const PocoBinding second_bindings[] = {
		{"int unused_one();", (PocoNativeFunction)unused_one},
	};
	PocoLibrary first_library = {
		"test.first", first_bindings, 3, first_library_init, first_library_cleanup, NULL,
	};
	PocoLibrary invalid_library = {
		"test.invalid", invalid_bindings, 1, NULL, NULL, NULL,
	};
	PocoLibrary second_library = {
		"test.second", second_bindings, 1, second_library_init, second_library_cleanup, NULL,
	};
	const char* include_paths[] = {POCO_PUBLIC_API_FIXTURE_DIR "/include/"};
	int last_diagnostic_status = POCO_STATUS_OK;
	PocoVmOptions vm_options = {
		include_paths, 1, collect_diagnostic, &last_diagnostic_status, 0,
	};
	PocoVm* vm = NULL;
	PocoProgram* program = NULL;
	PocoProgram* cancel_program = NULL;
	PocoRunOptions run_options = {NULL, NULL, NULL};
	int32_t result = 0;
	int cancel_calls = 0;

	if (!check(poco_vm_create(&vm_options, &vm) == POCO_STATUS_OK, "create VM") ||
		!check(poco_vm_register_library(vm, &invalid_library) == POCO_STATUS_PARAMETER_RANGE,
			   "reject unknown binding flags") ||
		!check(poco_vm_register_library(vm, &first_library) == POCO_STATUS_OK,
			   "register fixed-argument library") ||
		!check(poco_vm_register_library(vm, &second_library) == POCO_STATUS_OK,
			   "register lifecycle library") ||
		!check(poco_vm_register_standard_library(vm) == POCO_STATUS_OK,
			   "register standard library") ||
		!check(poco_vm_compile_file(vm, FIXTURE_PATH("host_api.poc"), &program) == POCO_STATUS_OK,
			   "compile with custom binding and include path") ||
		!check(poco_vm_run(vm, program, &run_options, &result) == POCO_STATUS_OK,
			   "run custom binding") ||
		!check(result == 53, "fixed and variadic run-context result") ||
		!check(lifecycle_event_count == 4 && lifecycle_events[0] == 1 && lifecycle_events[1] == 2 &&
				   lifecycle_events[2] == 3 && lifecycle_events[3] == 4,
			   "deterministic lifecycle order")) {
		fprintf(stderr, "last diagnostic: status=%d message=%s\n", last_diagnostic_status,
				diagnostic_message);
		poco_program_destroy(program);
		poco_vm_destroy(vm);
		return 1;
	}

	poco_program_destroy(program);
	program = NULL;

	if (!check(
			poco_vm_compile_file(vm, FIXTURE_PATH("cancel.poc"), &cancel_program) == POCO_STATUS_OK,
			"compile cancellable program")) {
		poco_vm_destroy(vm);
		return 1;
	}
	run_options.cancel_callback = cancel_immediately;
	run_options.cancel_user_data = &cancel_calls;
	if (!check(poco_vm_run(vm, cancel_program, &run_options, NULL) == POCO_STATUS_ABORTED,
			   "cancel run") ||
		!check(cancel_calls > 0, "cancel callback invoked")) {
		poco_program_destroy(cancel_program);
		poco_vm_destroy(vm);
		return 1;
	}
	poco_program_destroy(cancel_program);

	if (!check(
			poco_vm_compile_file(vm, FIXTURE_PATH("invalid.poc"), &program) == POCO_STATUS_REPORTED,
			"compile error status") ||
		!check(diagnostic_count > 0 && last_diagnostic_status == POCO_STATUS_REPORTED,
			   "compile diagnostic")) {
		poco_vm_destroy(vm);
		return 1;
	}

	poco_vm_destroy(vm);
	return 0;
}
