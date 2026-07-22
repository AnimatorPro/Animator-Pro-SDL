#include "poco/poco.h"

#include "poco.h"
#include "program_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct DiagnosticRecord {
	char source_name[1024];
	long line;
} DiagnosticRecord;

typedef struct DebugProbe {
	int saw_link0;
	int saw_link1;
	int command_failed;
} DebugProbe;

static void record_debug_location(PocoDebugSession* session, void* user_data)
{
	DebugProbe* probe = user_data;
	PocoDebugLocation location;

	if (poco_debug_current_location(session, &location) != POCO_STATUS_OK) {
		probe->command_failed = 1;
		return;
	}
	if (strstr(location.file, "link0.poc") != NULL) {
		probe->saw_link0 = 1;
	}
	if (strstr(location.file, "link1.poc") != NULL) {
		probe->saw_link1 = 1;
	}
	if (poco_debug_step_into(session) != POCO_STATUS_OK) {
		probe->command_failed = 1;
	}
}

static int check(int condition, const char* message)
{
	if (!condition) {
		fprintf(stderr, "multi-unit compile: %s\n", message);
		return 0;
	}
	return 1;
}

static void record_diagnostic(void* user_data, const PocoDiagnostic* diagnostic)
{
	DiagnosticRecord* record = user_data;

	snprintf(record->source_name, sizeof(record->source_name), "%s", diagnostic->source_name);
	record->line = diagnostic->line;
}

static int serialize_level(PocoProgram* program, PocoDebugLevel level, unsigned char** out_bytes,
						   size_t* out_size)
{
	PocoStatus status;

	*out_bytes = NULL;
	*out_size = 0;
	status = poco_program_serialize_buffer(program, level, NULL, 0, out_size);
	if (status != POCO_STATUS_BUFFER_TOO_SMALL) {
		return 0;
	}
	*out_bytes = malloc(*out_size);
	if (*out_bytes == NULL) {
		return 0;
	}
	status = poco_program_serialize_buffer(program, level, *out_bytes, *out_size, out_size);
	return status == POCO_STATUS_OK;
}

static int serialize(PocoProgram* program, unsigned char** out_bytes, size_t* out_size)
{
	return serialize_level(program, POCO_DEBUG_LEVEL_MINIMAL, out_bytes, out_size);
}

static const Func_frame* find_frame(const PocoProgram* program, const char* name)
{
	const Func_frame* frame;

	for (frame = program->code.functions; frame != NULL; frame = frame->next) {
		if (strcmp(frame->name, name) == 0) {
			return frame;
		}
	}
	return NULL;
}

static int rejects_link(PocoVm* vm, const char* const* names, const char* message,
						const char* required_text)
{
	PocoProgram* program = NULL;
	PocoStatus status = poco_vm_compile_files(vm, names, 2, &program);
	const char* error = poco_get_last_error(vm);
	int ok = check(status == POCO_STATUS_REPORTED && program == NULL, message);

	if (required_text != NULL) {
		ok &= check(error != NULL && strstr(error, required_text) != NULL,
					"link diagnostic identifies the rejected symbol");
	}
	return ok;
}

int main(void)
{
	const char* valid_names[] = {POCO_MULTI_UNIT_0, POCO_MULTI_UNIT_1};
	const char* invalid_names[] = {POCO_MULTI_UNIT_0, POCO_MULTI_UNIT_INVALID};
	const char* link_names[] = {POCO_MULTI_UNIT_LINK_0, POCO_MULTI_UNIT_LINK_1};
	const char* duplicate_function_names[] = {POCO_MULTI_UNIT_DUP_FUNCTION_0,
											  POCO_MULTI_UNIT_DUP_FUNCTION_1};
	const char* duplicate_global_names[] = {POCO_MULTI_UNIT_DUP_GLOBAL_0,
											POCO_MULTI_UNIT_DUP_GLOBAL_1};
	const char* duplicate_kind_names[] = {POCO_MULTI_UNIT_DUP_KIND_0, POCO_MULTI_UNIT_DUP_KIND_1};
	const char* multiple_main_names[] = {POCO_MULTI_UNIT_MAIN_0, POCO_MULTI_UNIT_MAIN_1};
	const char* missing_prototype_names[] = {POCO_MULTI_UNIT_MISSING_PROTO_0,
											 POCO_MULTI_UNIT_MISSING_PROTO_1};
	const char* use_root_names[] = {POCO_MULTI_UNIT_USE_ROOT};
	const char* use_matching_names[] = {POCO_MULTI_UNIT_USE_MATCH};
	const char* use_mismatch_names[] = {POCO_MULTI_UNIT_USE_MISMATCH};
	const char* use_hidden_names[] = {POCO_MULTI_UNIT_USE_HIDDEN};
	const char* use_cycle_names[] = {POCO_MULTI_UNIT_USE_CYCLE};
	const char* use_host_names[] = {POCO_MULTI_UNIT_USE_HOST_ROOT};
	const char* use_host_paths[] = {POCO_MULTI_UNIT_USE_HOST_DIR};
	const char* use_dedup_names[] = {POCO_MULTI_UNIT_USE_LEAF, POCO_MULTI_UNIT_USE_MATCH};
	const char* single_name[] = {POCO_MULTI_UNIT_0};
	PocoVmOptions options = {0};
	DiagnosticRecord diagnostic = {{0}, 0};
	PocoVm* vm = NULL;
	PocoProgram* program = NULL;
	PocoProgram* loaded = NULL;
	PocoProgram* single_file = NULL;
	PocoProgram* single_list = NULL;
	Poco_run_env* executable;
	const Func_frame* frame;
	unsigned char* file_bytes = NULL;
	unsigned char* list_bytes = NULL;
	unsigned char* multi_bytes = NULL;
	size_t file_size = 0;
	size_t list_size = 0;
	size_t multi_size = 0;
	int saw_first = 0;
	int saw_main = 0;
	int saw_first_global = 0;
	int saw_second_global = 0;
	int32_t result = 0;
	PocoStatus file_status;
	PocoStatus list_status;
	int ok = 1;

	options.diagnostic_callback = record_diagnostic;
	options.diagnostic_user_data = &diagnostic;
	ok &= check(poco_vm_create(&options, &vm) == POCO_STATUS_OK, "create VM");
	ok &= check(poco_vm_register_standard_library(vm) == POCO_STATUS_OK,
				"register standard native prototypes");
	ok &= check(poco_vm_compile_files(vm, valid_names, 0, &program) == POCO_STATUS_PARAMETER_RANGE,
				"reject an empty unit list");
	{
		PocoStatus status = poco_vm_compile_files(vm, valid_names, 2, &program);
		if (status != POCO_STATUS_OK) {
			fprintf(stderr, "multi-unit compile status %d: %s\n", status, poco_get_last_error(vm));
		}
		ok &= check(status == POCO_STATUS_OK, "compile two units");
	}
	if (program != NULL) {
		executable = program->executable;
		ok &= check(program->source_count == 2 && program->sources != NULL,
					"retain one source-identity table entry per unit");
		if (program->source_count == 2 && program->sources != NULL) {
			ok &= check(strcmp(program->sources[0].name, "unit0.poc") == 0 &&
							strcmp(program->sources[1].name, "unit1.poc") == 0 &&
							strcmp(program->sources[0].path, valid_names[0]) == 0 &&
							strcmp(program->sources[1].path, valid_names[1]) == 0 &&
							memcmp(program->sources[0].hash, program->sources[1].hash,
								   sizeof(program->sources[0].hash)) != 0,
						"retain distinct per-file names, paths, and hashes");
		}
		ok &= check(executable->data_size == (long)(2 * sizeof(int)),
					"allocate non-overlapping global storage");
		for (frame = executable->fff; frame != NULL; frame = frame->next) {
			const Symbol* symbol;
			const char* expected_name;

			ok &= check(frame->unit_index < 2, "frame unit index is in range");
			if (frame->unit_index >= 2) {
				continue;
			}
			expected_name = valid_names[frame->unit_index];
			ok &= check(frame->unit_name != NULL && strcmp(frame->unit_name, expected_name) == 0,
						"frame retains its source unit name");
			if (strcmp(frame->name, "first") == 0) {
				saw_first = frame->unit_index == 0;
			} else if (strcmp(frame->name, "main") == 0) {
				saw_main = frame->unit_index == 1;
			}
			for (symbol = frame->parameters; symbol != NULL; symbol = symbol->link) {
				ok &= check(symbol->unit_name != NULL && symbol->unit_index == frame->unit_index &&
								strcmp(symbol->unit_name, frame->unit_name) == 0,
							"global retains its source unit identity");
				if (strcmp(symbol->name, "first_global") == 0) {
					saw_first_global = symbol->unit_index == 0;
				} else if (strcmp(symbol->name, "second_global") == 0) {
					saw_second_global = symbol->unit_index == 1;
				}
			}
		}
		ok &= check(saw_first && saw_main, "retain functions from both units");
		ok &= check(saw_first_global && saw_second_global, "retain globals from both units");
		ok &= check(poco_vm_run(vm, program, NULL, &result) == POCO_STATUS_OK && result == 12,
					"run the merged program");
		ok &= check(serialize(program, &multi_bytes, &multi_size) &&
						poco_vm_deserialize_buffer(vm, multi_bytes, multi_size, &loaded) ==
							POCO_STATUS_OK &&
						poco_vm_run(vm, loaded, NULL, &result) == POCO_STATUS_OK && result == 12,
					"round-trip and run the merged program image");
		if (loaded != NULL) {
			const Func_frame* loaded_first = find_frame(loaded, "first");
			const Func_frame* loaded_main = find_frame(loaded, "main");
			ok &= check(loaded->source_count == 2 && loaded_first != NULL && loaded_main != NULL &&
							loaded_first->unit_index == 0 && loaded_main->unit_index == 1,
						"round-trip the file-indexed opcode maps and source table");
		}
	}
	free(multi_bytes);
	poco_program_destroy(loaded);
	poco_program_destroy(program);
	program = NULL;

	{
		PocoStatus status = poco_vm_compile_files(vm, link_names, 2, &program);
		if (status != POCO_STATUS_OK) {
			fprintf(stderr, "flat-link compile status %d: %s\n", status, poco_get_last_error(vm));
		}
		ok &= check(status == POCO_STATUS_OK,
					"link cross-unit functions and globals with private static helpers");
	}
	if (program != NULL) {
		ok &= check(poco_vm_run(vm, program, NULL, &result) == POCO_STATUS_OK && result == 25,
					"run per-unit initializers in link order");
		ok &= check(
			serialize_level(program, POCO_DEBUG_LEVEL_EXTENDED, &multi_bytes, &multi_size) &&
				poco_vm_deserialize_buffer(vm, multi_bytes, multi_size, &loaded) == POCO_STATUS_OK,
			"round-trip extended per-file debug metadata");
		if (loaded != NULL) {
			PocoActivation* activation = NULL;
			PocoDebugSession* session = NULL;
			DebugProbe probe = {0};
			PocoStatus debug_status = poco_activation_acquire(loaded, &activation);

			if (debug_status == POCO_STATUS_OK) {
				debug_status =
					poco_debug_attach(activation, record_debug_location, &probe, &session);
			}
			if (debug_status == POCO_STATUS_OK) {
				debug_status = poco_debug_step_into(session);
			}
			if (debug_status == POCO_STATUS_OK) {
				debug_status = poco_activation_run(activation, NULL, &result);
			}
			ok &= check(debug_status == POCO_STATUS_OK && probe.saw_link0 && probe.saw_link1 &&
							!probe.command_failed,
						"debugger resolves the owning source while stepping across files");
			if (session != NULL) {
				poco_debug_detach(session);
			}
			if (activation != NULL) {
				poco_activation_release(activation);
			}
		}
		free(multi_bytes);
		multi_bytes = NULL;
		poco_program_destroy(loaded);
		loaded = NULL;
	}
	poco_program_destroy(program);
	program = NULL;

	ok &= rejects_link(vm, duplicate_function_names, "reject duplicate external functions",
					   "collision");
	ok &= check(strstr(poco_get_last_error(vm), duplicate_function_names[0]) != NULL &&
					strstr(poco_get_last_error(vm), duplicate_function_names[1]) != NULL,
				"duplicate-function diagnostic names both units");
	ok &= rejects_link(vm, duplicate_global_names, "reject duplicate external globals",
					   "collision_global");
	ok &= check(strstr(poco_get_last_error(vm), duplicate_global_names[0]) != NULL &&
					strstr(poco_get_last_error(vm), duplicate_global_names[1]) != NULL,
				"duplicate-global diagnostic names both units");
	ok &= rejects_link(vm, duplicate_kind_names, "reject function/global namespace collisions",
					   "collision_kind");
	ok &= check(strstr(poco_get_last_error(vm), duplicate_kind_names[0]) != NULL &&
					strstr(poco_get_last_error(vm), duplicate_kind_names[1]) != NULL,
				"function/global collision diagnostic names both units");
	ok &= rejects_link(vm, multiple_main_names, "reject multiple main definitions", "main()");
	ok &= check(strstr(poco_get_last_error(vm), multiple_main_names[0]) != NULL &&
					strstr(poco_get_last_error(vm), multiple_main_names[1]) != NULL,
				"multiple-main diagnostic names every offending unit");
	ok &= rejects_link(vm, missing_prototype_names,
					   "reject a bare-list call without a visible prototype", "late_definition");

	{
		PocoStatus status = poco_vm_compile_files(vm, use_root_names, 1, &program);
		if (status != POCO_STATUS_OK) {
			fprintf(stderr, "transitive pragma-use status %d: %s\n", status,
					poco_get_last_error(vm));
		}
		ok &= check(status == POCO_STATUS_OK,
					"compile transitive pragma use with auto-imported functions and globals");
		if (program != NULL) {
			ok &= check(strcmp(program->source_name, "use_root.poc") == 0 &&
							strcmp(program->source_path, POCO_MULTI_UNIT_USE_ROOT) == 0,
						"retain the explicit root as program source metadata");
			ok &= check(poco_vm_run(vm, program, NULL, &result) == POCO_STATUS_OK && result == 15,
						"run used units after depth-first global initialization");
		}
		poco_program_destroy(program);
		program = NULL;
	}
	{
		PocoStatus status = poco_vm_compile_files(vm, use_matching_names, 1, &program);
		if (status != POCO_STATUS_OK) {
			fprintf(stderr, "matching pragma-use status %d: %s\n", status, poco_get_last_error(vm));
		}
		ok &= check(status == POCO_STATUS_OK, "allow use plus matching header declarations");
		if (program != NULL) {
			ok &= check(poco_vm_run(vm, program, NULL, &result) == POCO_STATUS_OK && result == 15,
						"call used function and access used global without relying on the header");
		}
		poco_program_destroy(program);
		program = NULL;
	}
	ok &=
		check(poco_vm_compile_files(vm, use_mismatch_names, 1, &program) == POCO_STATUS_REPORTED &&
				  strstr(poco_get_last_error(vm), "redeclaration of used_add") != NULL,
			  "reject a header prototype that mismatches the used definition");
	poco_program_destroy(program);
	program = NULL;
	ok &= check(poco_vm_compile_files(vm, use_hidden_names, 1, &program) == POCO_STATUS_REPORTED,
				"keep static symbols private to a used file");
	poco_program_destroy(program);
	program = NULL;
	ok &= check(poco_vm_compile_files(vm, use_cycle_names, 1, &program) == POCO_STATUS_REPORTED &&
					strstr(poco_get_last_error(vm), "use cycle") != NULL,
				"diagnose a pragma-use cycle");
	poco_program_destroy(program);
	program = NULL;
	{
		PocoStatus status = poco_vm_compile_files(vm, use_dedup_names, 2, &program);
		int used_add_definitions = 0;

		ok &=
			check(status == POCO_STATUS_OK, "deduplicate a file named and used in the same build");
		if (program != NULL) {
			for (frame = program->code.functions; frame != NULL; frame = frame->next) {
				if (frame->got_code && strcmp(frame->name, "used_add") == 0) {
					++used_add_definitions;
				}
			}
			ok &= check(used_add_definitions == 1, "compile each canonical used file only once");
		}
		poco_program_destroy(program);
		program = NULL;
	}
	ok &= check(poco_vm_set_include_paths(vm, use_host_paths, 1) == POCO_STATUS_OK,
				"configure pragma-use host search path");
	ok &= check(poco_vm_compile_files(vm, use_host_names, 1, &program) == POCO_STATUS_OK,
				"resolve a used file through the host include path fallback");
	if (program != NULL) {
		ok &= check(poco_vm_run(vm, program, NULL, &result) == POCO_STATUS_OK && result == 23,
					"run a host-path-resolved used source");
	}
	poco_program_destroy(program);
	program = NULL;
	ok &= check(poco_vm_set_include_paths(vm, NULL, 0) == POCO_STATUS_OK,
				"clear pragma-use host search path");

	file_status = poco_vm_compile_file(vm, single_name[0], &single_file);
	list_status = poco_vm_compile_files(vm, single_name, 1, &single_list);
	if (file_status != POCO_STATUS_OK || list_status != POCO_STATUS_OK) {
		fprintf(stderr, "single compile statuses: %d %d (%s)\n", file_status, list_status,
				poco_get_last_error(vm));
	}
	ok &= check(file_status == POCO_STATUS_OK, "compile through single-file wrapper");
	ok &= check(list_status == POCO_STATUS_OK, "compile a one-unit list");
	{
		const Func_frame* file_first = find_frame(single_file, "first");
		const Func_frame* list_first = find_frame(single_list, "first");

		ok &= check(serialize(single_file, &file_bytes, &file_size) &&
						serialize(single_list, &list_bytes, &list_size) && file_size == list_size,
					"single-file wrapper preserves the one-unit image shape");
		ok &= check(file_first != NULL && list_first != NULL &&
						file_first->code_size == list_first->code_size &&
						memcmp(file_first->code_pt, list_first->code_pt,
							   (size_t)file_first->code_size) == 0 &&
						memcmp(single_file->source_hash, single_list->source_hash,
							   sizeof(single_file->source_hash)) == 0,
					"single-file wrapper preserves emitted bytecode and source identity");
	}
	free(file_bytes);
	free(list_bytes);
	poco_program_destroy(single_file);
	poco_program_destroy(single_list);

	ok &= check(poco_vm_compile_files(vm, invalid_names, 2, &program) == POCO_STATUS_REPORTED,
				"reject an invalid second unit");
	ok &= check(strcmp(diagnostic.source_name, invalid_names[1]) == 0 && diagnostic.line == 3,
				"diagnostic identifies the failing unit and line");
	poco_program_destroy(program);
	poco_vm_destroy(vm);
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
