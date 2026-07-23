#include <poco/poco.h>

#include <stdio.h>
#include <string.h>

#ifndef POCO_COMPILE_BUFFER_FIXTURE_DIR
#error "POCO_COMPILE_BUFFER_FIXTURE_DIR must name the compile-buffer fixture directory"
#endif

typedef struct DiagnosticRecord
{
	PocoStatus status;
	char source_name[4096];
	long line;
	int column;
	char message[1024];
} DiagnosticRecord;

static DiagnosticRecord diagnostic;

static void copy_text(char* destination, size_t capacity, const char* source)
{
	if (source == NULL)
		source = "";
	if (capacity == 0)
		return;
	strncpy(destination, source, capacity - 1);
	destination[capacity - 1] = '\0';
}

static void collect_diagnostic(void* user_data, const PocoDiagnostic* value)
{
	DiagnosticRecord* record = user_data;

	record->status = value->status;
	copy_text(record->source_name, sizeof(record->source_name), value->source_name);
	record->line = value->line;
	record->column = value->column;
	copy_text(record->message, sizeof(record->message), value->message);
}

static int check(int condition, const char* context)
{
	if (condition)
		return 1;
	fprintf(stderr, "poco_compile_buffer: %s\n", context);
	return 0;
}

static int run_program(PocoVm* vm, PocoProgram* program, int32_t* result)
{
	PocoStatus status = poco_vm_run(vm, program, NULL, result);
	return check(status == POCO_STATUS_OK, "program run failed");
}

int main(void)
{
	static const char valid_source[] =
		"main()\n"
		"{\n"
		"\treturn 73;\n"
		"}\n";
	static const char invalid_source[] =
		"main()\n"
		"{\n"
		"\treturn missing_value;\n"
		"}\n";
	static const char unterminated_conditional[] =
		"#if 1\n"
		"main() { return 0; }\n";
	static const char embedded_nul[] = {'m', 'a', 'i', 'n', '\0', '(', ')'};
	static const char include_source[] =
		"#include \"buffer_value.h\"\n"
		"main()\n"
		"{\n"
		"\treturn BUFFER_INCLUDE_VALUE;\n"
		"}\n";
	static const char missing_include_source[] =
		"#include \"missing_value.h\"\n"
		"main() { return 0; }\n";
	PocoVmOptions options = {0};
	PocoVm* vm = NULL;
	PocoProgram* file_program = NULL;
	PocoProgram* buffer_program = NULL;
	PocoProgram* virtual_program = NULL;
	PocoProgram* include_program = NULL;
	PocoProgram* sibling_include_program = NULL;
	PocoProgram* rejected_program = NULL;
	PocoStatus file_status;
	PocoStatus buffer_status;
	PocoStatus valid_file_status;
	PocoStatus valid_buffer_status;
	DiagnosticRecord file_diagnostic;
	char valid_path[1024];
	char invalid_path[1024];
	char sibling_include_path[1024];
	char include_directory[1024];
	char second_include_directory[1024];
	const char* include_paths[2];
	char long_source_name[2048];
	int32_t file_result = 0;
	int32_t buffer_result = 0;
	int32_t include_result = 0;
	int32_t sibling_include_result = 0;
	int ok = 1;

	options.diagnostic_callback = collect_diagnostic;
	options.diagnostic_user_data = &diagnostic;
	if (snprintf(valid_path, sizeof(valid_path), "%s/valid.poc",
		POCO_COMPILE_BUFFER_FIXTURE_DIR) >= (int)sizeof(valid_path) ||
		snprintf(invalid_path, sizeof(invalid_path), "%s/invalid.poc",
		POCO_COMPILE_BUFFER_FIXTURE_DIR) >= (int)sizeof(invalid_path) ||
		snprintf(sibling_include_path, sizeof(sibling_include_path),
			"%s/file_include.poc", POCO_COMPILE_BUFFER_FIXTURE_DIR) >=
			(int)sizeof(sibling_include_path) ||
		snprintf(include_directory, sizeof(include_directory), "%s/include/",
			POCO_COMPILE_BUFFER_FIXTURE_DIR) >= (int)sizeof(include_directory) ||
		snprintf(second_include_directory, sizeof(second_include_directory),
			"%s/include_second/", POCO_COMPILE_BUFFER_FIXTURE_DIR) >=
			(int)sizeof(second_include_directory)) {
		return check(0, "fixture path too long") ? 0 : 1;
	}

	ok &= check(poco_vm_create(&options, &vm) == POCO_STATUS_OK, "VM creation failed");
	if (!ok)
		return 1;
	ok &= check(poco_vm_register_standard_library(vm) == POCO_STATUS_OK,
		"standard library registration failed");
	include_paths[0] = include_directory;
	include_paths[1] = second_include_directory;
	ok &= check(poco_vm_set_include_paths(vm, include_paths, 2) ==
		POCO_STATUS_OK, "host include-directory seeding failed");
	ok &= check(poco_vm_compile_buffer(vm, "virtual/include-script.poc", include_source,
		sizeof(include_source) - 1, &include_program) == POCO_STATUS_OK,
		"buffer compilation did not search host include directories");
	if (include_program != NULL)
		ok &= run_program(vm, include_program, &include_result);
	ok &= check(include_result == 91,
		"buffer include did not preserve host directory order");

	memset(&diagnostic, 0, sizeof(diagnostic));
	ok &= check(poco_vm_compile_buffer(vm, "virtual/missing-include.poc",
		missing_include_source, sizeof(missing_include_source) - 1,
		&rejected_program) == POCO_STATUS_REPORTED && rejected_program == NULL,
		"missing buffer include was not rejected");
	ok &= check(strcmp(diagnostic.source_name, "virtual/missing-include.poc") == 0 &&
		diagnostic.line == 1 && strstr(diagnostic.message, "missing_value.h") != NULL,
		"missing buffer include lost its diagnostic context");

	ok &= check(poco_vm_compile_file(vm, sibling_include_path,
		&sibling_include_program) == POCO_STATUS_OK,
		"file compilation did not search the script directory");
	if (sibling_include_program != NULL)
		ok &= run_program(vm, sibling_include_program, &sibling_include_result);
	ok &= check(sibling_include_result == 37,
		"script-directory include produced the wrong value");

	valid_file_status = poco_vm_compile_file(vm, valid_path, &file_program);
	if (valid_file_status != POCO_STATUS_OK)
		fprintf(stderr, "file status=%d diagnostic=%s", valid_file_status,
			poco_get_last_error(vm));
	ok &= check(valid_file_status == POCO_STATUS_OK, "file compilation failed");
	valid_buffer_status = poco_vm_compile_buffer(vm, valid_path, valid_source,
		sizeof(valid_source) - 1, &buffer_program);
	if (valid_buffer_status != POCO_STATUS_OK)
		fprintf(stderr, "buffer status=%d diagnostic=%s", valid_buffer_status,
			poco_get_last_error(vm));
	ok &= check(valid_buffer_status == POCO_STATUS_OK, "buffer compilation failed");
	if (file_program != NULL)
		ok &= run_program(vm, file_program, &file_result);
	if (buffer_program != NULL)
		ok &= run_program(vm, buffer_program, &buffer_result);
	ok &= check(file_result == 73 && buffer_result == file_result,
		"file and buffer programs produced different results");

	/* A diagnostic-only name must not be opened as a source file. */
	ok &= check(poco_vm_compile_buffer(vm, "virtual/in-memory-script.poc", valid_source,
		sizeof(valid_source) - 1, &virtual_program) == POCO_STATUS_OK,
		"pathless buffer compilation attempted file access");

	memset(&diagnostic, 0, sizeof(diagnostic));
	file_status = poco_vm_compile_file(vm, invalid_path, &rejected_program);
	file_diagnostic = diagnostic;
	ok &= check(rejected_program == NULL, "invalid file produced a program");

	memset(&diagnostic, 0, sizeof(diagnostic));
	buffer_status = poco_vm_compile_buffer(vm, invalid_path, invalid_source,
		sizeof(invalid_source) - 1, &rejected_program);
	ok &= check(rejected_program == NULL, "invalid buffer produced a program");
	ok &= check(file_status == POCO_STATUS_REPORTED && buffer_status == file_status,
		"file and buffer compile statuses differ");
	ok &= check(strcmp(file_diagnostic.source_name, diagnostic.source_name) == 0 &&
		strcmp(diagnostic.source_name, invalid_path) == 0,
		"file and buffer diagnostic names differ");
	ok &= check(file_diagnostic.line == diagnostic.line && diagnostic.line == 3,
		"file and buffer diagnostic lines differ");
	ok &= check(file_diagnostic.column == diagnostic.column,
		"file and buffer diagnostic columns differ");
	ok &= check(strcmp(file_diagnostic.message, diagnostic.message) == 0,
		"file and buffer diagnostic messages differ");

	/* Virtual diagnostic names are not constrained to filesystem path limits. */
	memset(long_source_name, 'x', sizeof(long_source_name) - 1);
	long_source_name[sizeof(long_source_name) - 1] = '\0';
	memset(&diagnostic, 0, sizeof(diagnostic));
	ok &= check(poco_vm_compile_buffer(vm, long_source_name, invalid_source,
		sizeof(invalid_source) - 1, &rejected_program) == POCO_STATUS_REPORTED &&
		rejected_program == NULL, "long diagnostic name compile was not rejected cleanly");
	ok &= check(strcmp(diagnostic.source_name, long_source_name) == 0,
		"long diagnostic name was truncated");

	memset(&diagnostic, 0, sizeof(diagnostic));
	ok &= check(poco_vm_compile_buffer(vm, "unterminated-conditional.poc",
		unterminated_conditional, sizeof(unterminated_conditional) - 1,
		&rejected_program) == POCO_STATUS_REPORTED && rejected_program == NULL,
		"unterminated conditional was not diagnosed cleanly");
	ok &= check(strcmp(diagnostic.source_name, "unterminated-conditional.poc") == 0 &&
		diagnostic.line == 2, "unterminated conditional lost source location");

	ok &= check(poco_vm_compile_buffer(vm, "embedded-nul.poc", embedded_nul,
		sizeof(embedded_nul), &rejected_program) == POCO_STATUS_PARAMETER_RANGE &&
		rejected_program == NULL, "embedded NUL source was not rejected");

	poco_program_destroy(virtual_program);
	poco_program_destroy(sibling_include_program);
	poco_program_destroy(include_program);
	poco_program_destroy(buffer_program);
	poco_program_destroy(file_program);
	poco_vm_destroy(vm);
	return ok ? 0 : 1;
}
