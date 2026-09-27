/*
 * A run-time error is reported at the line of the statement that failed, not
 * the one after it.
 */
#include <poco/poco.h>

#include <stdio.h>

static long reported_line;
static int failures;

static void record_line(void* user_data, const PocoDiagnostic* diagnostic)
{
	(void)user_data;
	reported_line = diagnostic->line;
}

static void expect_line(const char* name, long expected)
{
	PocoVm* vm = NULL;
	PocoVmOptions options = {0};
	PocoProgram* program = NULL;
	PocoRunOptions run_options = {0};
	int32_t result = 0;
	char path[512];

	options.diagnostic_callback = record_line;
	if (poco_vm_create(&options, &vm) != POCO_STATUS_OK) {
		fprintf(stderr, "poco_trace_line: create VM\n");
		++failures;
		return;
	}
	if (poco_vm_register_standard_library(vm) != POCO_STATUS_OK) {
		fprintf(stderr, "poco_trace_line: register standard library\n");
		++failures;
	}
	snprintf(path, sizeof(path), "%s/%s", POCO_TRACE_LINE_FIXTURE_DIR, name);
	run_options.trace_file = POCO_TRACE_LINE_TRACE_FILE;
	reported_line = -1;
	if (poco_vm_compile_file(vm, path, &program) != POCO_STATUS_OK) {
		fprintf(stderr, "poco_trace_line: compiling %s: %s\n", name, poco_get_last_error(vm));
		++failures;
	} else if (poco_vm_run(vm, program, &run_options, &result) == POCO_STATUS_OK) {
		fprintf(stderr, "poco_trace_line: %s ran without an error\n", name);
		++failures;
	} else if (reported_line != expected) {
		fprintf(stderr, "poco_trace_line: %s reported line %ld, expected %ld\n", name,
				reported_line, expected);
		++failures;
	}
	poco_program_destroy(program);
	poco_vm_destroy(vm);
}

int main(void)
{
	expect_line("index_past_end.poc", 6);
	expect_line("null_in_callee.poc", 3);
	expect_line("first_statement.poc", 3);
	remove(POCO_TRACE_LINE_TRACE_FILE);
	return failures != 0 ? 1 : 0;
}
