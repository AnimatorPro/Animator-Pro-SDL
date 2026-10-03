/*
 * A run-time error reaches the host's diagnostic callback with its specific
 * reason, the function it happened in, and the file and line of that function's
 * own source unit -- for a freshly compiled program and for one loaded back
 * from a minimal-debug image, and whether it is run as main or called by name.
 */
#include <poco/poco.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
static int reports;
static PocoStatus reported_status;
static char reported_source[256];
static long reported_line;
static char reported_message[512];

static void record(void* user_data, const PocoDiagnostic* diagnostic)
{
	(void)user_data;
	++reports;
	reported_status = diagnostic->status;
	snprintf(reported_source, sizeof(reported_source), "%s",
			 diagnostic->source_name != NULL ? diagnostic->source_name : "(null)");
	reported_line = diagnostic->line;
	snprintf(reported_message, sizeof(reported_message), "%s",
			 diagnostic->message != NULL ? diagnostic->message : "(null)");
}

static void forget(void)
{
	reports = 0;
	reported_status = POCO_STATUS_OK;
	reported_source[0] = '\0';
	reported_line = -1;
	reported_message[0] = '\0';
}

static void expect_report(const char* what, const char* source, long line, const char* message)
{
	if (reports != 1) {
		fprintf(stderr, "runtime_error_report: %s: %d reports, expected 1\n", what, reports);
		++failures;
	}
	if (strcmp(reported_source, source) != 0 || reported_line != line ||
		strcmp(reported_message, message) != 0) {
		fprintf(stderr,
				"runtime_error_report: %s: reported %s:%ld: %s\n"
				"                      expected %s:%ld: %s\n",
				what, reported_source, reported_line, reported_message, source, line, message);
		++failures;
	}
}

static void call_and_expect(PocoProgram* program, const char* what, const char* function,
							const char* source, long line, const char* message)
{
	PocoActivation* activation = NULL;
	PocoCall* call = NULL;
	PocoCallbackValue result;

	if (poco_activation_acquire(program, &activation) != POCO_STATUS_OK ||
		poco_activation_init(activation) != POCO_STATUS_OK) {
		fprintf(stderr, "runtime_error_report: %s: acquire/init failed\n", what);
		++failures;
		poco_activation_release(activation);
		return;
	}
	if (poco_call_begin(activation, function, &call) != POCO_STATUS_OK) {
		fprintf(stderr, "runtime_error_report: %s: cannot call %s\n", what, function);
		++failures;
	} else {
		if (strcmp(function, "door_tick") == 0) {
			poco_call_push_int(call, 7);
		}
		forget();
		if (poco_call_invoke(call, &result) == POCO_STATUS_OK) {
			fprintf(stderr, "runtime_error_report: %s: call succeeded\n", what);
			++failures;
		} else {
			expect_report(what, source, line, message);
		}
	}
	poco_call_end(call);
	poco_activation_release(activation);
}

static void check_program(PocoVm* vm, PocoProgram* program, const char* label)
{
	PocoRunOptions run_options = {0};
	int32_t result = 0;
	char what[128];

	run_options.trace_file = POCO_RUNTIME_ERROR_TRACE_FILE;
	snprintf(what, sizeof(what), "%s: main", label);
	forget();
	if (poco_vm_run(vm, program, &run_options, &result) == POCO_STATUS_OK) {
		fprintf(stderr, "runtime_error_report: %s ran without an error\n", what);
		++failures;
	} else {
		expect_report(what, "doors.poc", 13, "Attempt to divide by zero (in door_tick)");
	}

	snprintf(what, sizeof(what), "%s: door_tick", label);
	call_and_expect(program, what, "door_tick", "doors.poc", 13,
					"Attempt to divide by zero (in door_tick)");
	snprintf(what, sizeof(what), "%s: null_tick", label);
	call_and_expect(program, what, "null_tick", "root.poc", 8,
					"Trying to use a NULL pointer (in null_tick)");
	snprintf(what, sizeof(what), "%s: library_tick", label);
	call_and_expect(program, what, "library_tick", "root.poc", 16,
					"Poco native binding exceeded a pointer span "
					"(in library_tick, detected by strlen())");
}

int main(void)
{
	PocoVm* vm = NULL;
	PocoVmOptions options = {0};
	PocoProgram* program = NULL;
	PocoProgram* loaded = NULL;
	void* image = NULL;
	size_t image_size = 0;

	options.diagnostic_callback = record;
	if (poco_vm_create(&options, &vm) != POCO_STATUS_OK ||
		poco_vm_register_standard_library(vm) != POCO_STATUS_OK) {
		fprintf(stderr, "runtime_error_report: create VM\n");
		return 1;
	}
	if (poco_vm_compile_file(vm, POCO_RUNTIME_ERROR_ROOT, &program) != POCO_STATUS_OK) {
		fprintf(stderr, "runtime_error_report: compile: %s\n", poco_get_last_error(vm));
		poco_vm_destroy(vm);
		return 1;
	}
	check_program(vm, program, "compiled");

	/* A minimal-debug image keeps the source table and each function's unit, so
	 * a loaded program names the same file and line. */
	if (poco_program_serialize_buffer(program, POCO_DEBUG_LEVEL_MINIMAL, NULL, 0, &image_size) !=
			POCO_STATUS_BUFFER_TOO_SMALL ||
		(image = malloc(image_size)) == NULL ||
		poco_program_serialize_buffer(program, POCO_DEBUG_LEVEL_MINIMAL, image, image_size,
									  &image_size) != POCO_STATUS_OK ||
		poco_vm_deserialize_buffer(vm, image, image_size, &loaded) != POCO_STATUS_OK) {
		fprintf(stderr, "runtime_error_report: serialize/deserialize round trip failed\n");
		++failures;
	} else {
		check_program(vm, loaded, "loaded");
	}

	free(image);
	poco_program_destroy(loaded);
	poco_program_destroy(program);
	poco_vm_destroy(vm);
	remove(POCO_RUNTIME_ERROR_TRACE_FILE);
	return failures != 0 ? 1 : 0;
}
