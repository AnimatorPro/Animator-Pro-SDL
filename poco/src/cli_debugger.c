/*******************************************************************************
 * cli_debugger.c - stdin-driven interactive debugger for the standalone CLI.
 ******************************************************************************/

#include "cli_debugger.h"

#include "poco_hash.h"
#include "program_internal.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct PocoCliSource {
	const char* identity;
	const char* path;
	char* source;
	size_t source_size;
	size_t* line_offsets;
	size_t line_count;
} PocoCliSource;

typedef struct PocoCliDebugger {
	PocoDebugSession* session;
	PocoCliSource* sources;
	size_t source_count;
	PocoCliSource* current_source;
	int paused;
	int quit_requested;
	int command_failed;
} PocoCliDebugger;

static PocoStatus cli_read_source(const char* path, char** out_source, size_t* out_size)
{
	FILE* file;
	long file_size;
	char* source;
	size_t size;

	*out_source = NULL;
	*out_size = 0;
	file = fopen(path, "rb");
	if (file == NULL) {
		fprintf(stderr, "poco: debugger cannot open source '%s'\n", path);
		return POCO_STATUS_NO_FILE;
	}
	if (fseek(file, 0, SEEK_END) != 0 || (file_size = ftell(file)) < 0 ||
		fseek(file, 0, SEEK_SET) != 0) {
		fclose(file);
		fprintf(stderr, "poco: debugger cannot determine source size for '%s'\n", path);
		return POCO_STATUS_SEEK_FAILED;
	}
	if ((unsigned long)file_size > SIZE_MAX - 1) {
		fclose(file);
		return POCO_STATUS_OVERFLOW;
	}
	size = (size_t)file_size;
	source = malloc(size + 1);
	if (source == NULL) {
		fclose(file);
		return POCO_STATUS_OUT_OF_MEMORY;
	}
	if (size != 0 && fread(source, 1, size, file) != size) {
		free(source);
		fclose(file);
		fprintf(stderr, "poco: debugger cannot read source '%s'\n", path);
		return POCO_STATUS_READ_FAILED;
	}
	if (fclose(file) != 0) {
		free(source);
		return POCO_STATUS_READ_FAILED;
	}
	source[size] = '\0';
	*out_source = source;
	*out_size = size;
	return POCO_STATUS_OK;
}

static PocoStatus cli_index_source(PocoCliSource* source)
{
	size_t line_count = 1;
	size_t index;
	size_t write_index = 1;

	for (index = 0; index < source->source_size; ++index) {
		if (source->source[index] == '\n') {
			++line_count;
		}
	}
	if (line_count > SIZE_MAX / sizeof(*source->line_offsets)) {
		return POCO_STATUS_OVERFLOW;
	}
	source->line_offsets = malloc(line_count * sizeof(*source->line_offsets));
	if (source->line_offsets == NULL) {
		return POCO_STATUS_OUT_OF_MEMORY;
	}
	source->line_offsets[0] = 0;
	for (index = 0; index < source->source_size; ++index) {
		if (source->source[index] == '\n') {
			source->line_offsets[write_index++] = index + 1;
		}
	}
	source->line_count = line_count;
	return POCO_STATUS_OK;
}

static void cli_print_source_line(const PocoCliDebugger* debugger, long line)
{
	size_t start;
	size_t end;
	const PocoCliSource* source = debugger->current_source;

	if (source == NULL || line <= 0 || (size_t)line > source->line_count) {
		return;
	}
	start = source->line_offsets[(size_t)line - 1];
	end = (size_t)line < source->line_count ? source->line_offsets[(size_t)line] - 1
											: source->source_size;
	if (end > start && source->source[end - 1] == '\r') {
		--end;
	}
	printf("%5ld  ", line);
	if (end != start) {
		fwrite(source->source + start, 1, end - start, stdout);
	}
	fputc('\n', stdout);
}

static int cli_current_location(PocoCliDebugger* debugger, PocoDebugLocation* location)
{
	size_t source_index;
	if (!debugger->paused ||
		poco_debug_current_location(debugger->session, location) != POCO_STATUS_OK) {
		fprintf(stderr, "poco: debugger has no current source location\n");
		return 0;
	}
	debugger->current_source = NULL;
	for (source_index = 0; source_index < debugger->source_count; ++source_index) {
		if (strcmp(debugger->sources[source_index].identity, location->file) == 0) {
			debugger->current_source = &debugger->sources[source_index];
			break;
		}
	}
	if (debugger->current_source == NULL) {
		fprintf(stderr, "poco: debugger has no verified source for '%s'\n", location->file);
		return 0;
	}
	return 1;
}

static void cli_show_location(PocoCliDebugger* debugger)
{
	PocoDebugLocation location;

	if (!cli_current_location(debugger, &location)) {
		return;
	}
	printf("Stopped at %s:%ld\n", location.file, location.line);
	cli_print_source_line(debugger, location.line);
}

static void cli_list(PocoCliDebugger* debugger)
{
	PocoDebugLocation location;
	long first;
	long last;
	long line;

	if (!cli_current_location(debugger, &location)) {
		return;
	}
	first = location.line > 2 ? location.line - 2 : 1;
	last = location.line + 2;
	if ((size_t)last > debugger->current_source->line_count) {
		last = (long)debugger->current_source->line_count;
	}
	for (line = first; line <= last; ++line) {
		cli_print_source_line(debugger, line);
	}
}

static int cli_parse_line_number(const char* text, long* out_line)
{
	char* end;
	long line;

	while (isspace((unsigned char)*text)) {
		++text;
	}
	errno = 0;
	line = strtol(text, &end, 10);
	if (text == end || errno == ERANGE || line <= 0) {
		return 0;
	}
	while (isspace((unsigned char)*end)) {
		++end;
	}
	if (*end != '\0') {
		return 0;
	}
	*out_line = line;
	return 1;
}

static void cli_print_value(const PocoCallbackValue* value)
{
	switch (value->kind) {
		case POCO_CALLBACK_VALUE_INT:
			printf("%d\n", value->value.int_value);
			break;
		case POCO_CALLBACK_VALUE_LONG:
			printf("%ld\n", value->value.long_value);
			break;
		case POCO_CALLBACK_VALUE_DOUBLE:
			printf("%.17g\n", value->value.double_value);
			break;
		case POCO_CALLBACK_VALUE_POPOT:
			printf("%p\n", value->value.popot_value.pt);
			break;
		case POCO_CALLBACK_VALUE_INVALID:
		default:
			fprintf(stderr, "poco: debugger cannot display this value\n");
			break;
	}
}

static void cli_print_variable(PocoCliDebugger* debugger, const char* name)
{
	PocoCallbackValue value;
	PocoStatus status;

	if (!debugger->paused) {
		fprintf(stderr, "poco: 'print' requires a stopped program\n");
		return;
	}
	while (isspace((unsigned char)*name)) {
		++name;
	}
	if (*name == '\0') {
		fprintf(stderr, "poco: usage: print <variable>\n");
		return;
	}
	status = poco_debug_read_variable(debugger->session, name, &value);
	if (status != POCO_STATUS_OK) {
		fprintf(stderr, "poco: variable '%s' is not available (%d)\n", name, (int)status);
		return;
	}
	printf("%s = ", name);
	cli_print_value(&value);
}

static void cli_backtrace(PocoCliDebugger* debugger)
{
	PocoDebugFrame* frames;
	size_t frame_count = 0;
	size_t index;
	PocoStatus status;

	if (!debugger->paused) {
		fprintf(stderr, "poco: 'backtrace' requires a stopped program\n");
		return;
	}
	status = poco_debug_backtrace(debugger->session, NULL, 0, &frame_count);
	if (status != POCO_STATUS_BUFFER_TOO_SMALL || frame_count == 0 ||
		frame_count > SIZE_MAX / sizeof(*frames)) {
		fprintf(stderr, "poco: debugger cannot read the call stack (%d)\n", (int)status);
		return;
	}
	frames = malloc(frame_count * sizeof(*frames));
	if (frames == NULL) {
		fprintf(stderr, "poco: debugger is out of memory\n");
		return;
	}
	status = poco_debug_backtrace(debugger->session, frames, frame_count, &frame_count);
	if (status != POCO_STATUS_OK) {
		fprintf(stderr, "poco: debugger cannot read the call stack (%d)\n", (int)status);
		free(frames);
		return;
	}
	for (index = 0; index < frame_count; ++index) {
		printf("#%zu %s at line %ld\n", index, frames[index].function, frames[index].line);
	}
	free(frames);
}

static void cli_help(void)
{
	puts("Commands: break <line>, clear <line>, step, next, continue,");
	puts("          print <variable>, list, backtrace, quit");
}

/* Return non-zero when execution should start/resume. */
static int cli_command_loop(PocoCliDebugger* debugger)
{
	char command[512];

	for (;;) {
		char* name;
		char* argument;
		char* end;
		PocoStatus status;
		long line;

		fputs("(poco-debug) ", stdout);
		fflush(stdout);
		if (fgets(command, sizeof(command), stdin) == NULL) {
			debugger->quit_requested = 1;
			return debugger->paused;
		}
		end = command + strlen(command);
		while (end > command && isspace((unsigned char)end[-1])) {
			*--end = '\0';
		}
		name = command;
		while (isspace((unsigned char)*name)) {
			++name;
		}
		argument = name;
		while (*argument != '\0' && !isspace((unsigned char)*argument)) {
			++argument;
		}
		if (*argument != '\0') {
			*argument++ = '\0';
		}
		while (isspace((unsigned char)*argument)) {
			++argument;
		}
		if (*name == '\0') {
			continue;
		}
		if (strcmp(name, "break") == 0 || strcmp(name, "clear") == 0) {
			if (!cli_parse_line_number(argument, &line)) {
				fprintf(stderr, "poco: usage: %s <line>\n", name);
				continue;
			}
			status = strcmp(name, "break") == 0
						 ? poco_debug_add_line_breakpoint(debugger->session, line)
						 : poco_debug_clear_line_breakpoint(debugger->session, line);
			if (status != POCO_STATUS_OK) {
				fprintf(stderr, "poco: cannot %s breakpoint at line %ld (%d)\n",
						strcmp(name, "break") == 0 ? "set" : "clear", line, (int)status);
			} else {
				printf("Breakpoint %s at line %ld\n",
					   strcmp(name, "break") == 0 ? "set" : "cleared", line);
			}
		} else if (strcmp(name, "step") == 0 || strcmp(name, "next") == 0 ||
				   strcmp(name, "continue") == 0) {
			if (*argument != '\0') {
				fprintf(stderr, "poco: '%s' takes no arguments\n", name);
				continue;
			}
			if (!debugger->paused) {
				if (strcmp(name, "continue") == 0) {
					return 1;
				}
			}
			status = strcmp(name, "step") == 0   ? poco_debug_step_into(debugger->session)
					 : strcmp(name, "next") == 0 ? poco_debug_next_over(debugger->session)
												 : poco_debug_continue(debugger->session);
			if (status != POCO_STATUS_OK) {
				fprintf(stderr, "poco: debugger command '%s' failed (%d)\n", name, (int)status);
				continue;
			}
			return 1;
		} else if (strcmp(name, "print") == 0) {
			cli_print_variable(debugger, argument);
		} else if (strcmp(name, "list") == 0) {
			cli_list(debugger);
		} else if (strcmp(name, "backtrace") == 0) {
			cli_backtrace(debugger);
		} else if (strcmp(name, "quit") == 0) {
			debugger->quit_requested = 1;
			if (debugger->paused && poco_debug_continue(debugger->session) != POCO_STATUS_OK) {
				debugger->command_failed = 1;
			}
			return debugger->paused;
		} else if (strcmp(name, "help") == 0) {
			cli_help();
		} else {
			fprintf(stderr, "poco: unknown debugger command '%s'\n", name);
			cli_help();
		}
	}
}

static void cli_pause(PocoDebugSession* session, void* user_data)
{
	PocoCliDebugger* debugger = user_data;

	debugger->session = session;
	debugger->paused = 1;
	cli_show_location(debugger);
	(void)cli_command_loop(debugger);
	debugger->paused = 0;
}

static int cli_cancel(void* user_data)
{
	const PocoCliDebugger* debugger = user_data;

	return debugger->quit_requested;
}

PocoStatus poco_cli_debug_program(PocoVm* vm, PocoProgram* program,
								  const char* explicit_source_path, int32_t* out_result,
								  int* out_completed)
{
	PocoCliDebugger debugger = {0};
	PocoActivation* activation = NULL;
	PocoDebugSession* session = NULL;
	PocoRunOptions options = {cli_cancel, &debugger, NULL};
	const char* display_path;
	size_t source_index;
	PocoStatus status;

	if (vm == NULL || program == NULL) {
		return POCO_STATUS_NULL_REFERENCE;
	}
	if (out_completed != NULL) {
		*out_completed = 0;
	}
	if (program->sources == NULL || program->source_count == 0 ||
		program->primary_source_index >= program->source_count) {
		return POCO_STATUS_IMAGE_CORRUPT;
	}
	debugger.sources = calloc(program->source_count, sizeof(*debugger.sources));
	if (debugger.sources == NULL) {
		return POCO_STATUS_OUT_OF_MEMORY;
	}
	debugger.source_count = program->source_count;
	for (source_index = 0; source_index < program->source_count; ++source_index) {
		PocoCliSource* source = &debugger.sources[source_index];
		uint8_t source_hash[POCO_BLAKE3_HASH_SIZE];

		source->identity = program->sources[source_index].path != NULL
						   ? program->sources[source_index].path
						   : program->sources[source_index].name;
		source->path = explicit_source_path != NULL && source_index == program->primary_source_index
						   ? explicit_source_path
						   : program->sources[source_index].path;
		if (source->path == NULL || source->path[0] == '\0') {
			fprintf(stderr,
					"poco: debugger requires an extended (-g) binary with stored source paths\n");
			status = POCO_STATUS_NOT_FOUND;
			goto cleanup;
		}
		status = cli_read_source(source->path, &source->source, &source->source_size);
		if (status != POCO_STATUS_OK) {
			goto cleanup;
		}
		po_blake3_hash(source->source, source->source_size, source_hash);
		if (!poco_hash_equal(source_hash, program->sources[source_index].hash)) {
			fprintf(stderr, "poco: debugger source hash mismatch for '%s'; refusing to debug\n",
					source->path);
			status = POCO_STATUS_IMAGE_CORRUPT;
			goto cleanup;
		}
		status = cli_index_source(source);
		if (status != POCO_STATUS_OK) {
			goto cleanup;
		}
	}
	display_path = debugger.sources[program->primary_source_index].path;
	if (display_path == NULL || *display_path == '\0') {
		fprintf(stderr,
				"poco: debugger requires an extended (-g) binary with a stored source path\n");
		status = POCO_STATUS_NOT_FOUND;
		goto cleanup;
	}
	status = poco_activation_acquire(program, &activation);
	if (status != POCO_STATUS_OK) {
		goto cleanup;
	}
	status = poco_debug_attach(activation, cli_pause, &debugger, &session);
	if (status != POCO_STATUS_OK) {
		goto cleanup;
	}
	debugger.session = session;
	printf("Poco debugger: %s\n", display_path);
	cli_help();
	if (!cli_command_loop(&debugger) || debugger.quit_requested) {
		status = POCO_STATUS_OK;
		goto cleanup;
	}
	status = poco_activation_run(activation, &options, out_result);
	if (debugger.quit_requested && status == POCO_STATUS_ABORTED) {
		status = POCO_STATUS_OK;
	}
	if (status == POCO_STATUS_OK && !debugger.quit_requested) {
		puts("Program exited.");
		if (out_completed != NULL) {
			*out_completed = 1;
		}
	}
	if (debugger.command_failed && status == POCO_STATUS_OK) {
		status = POCO_STATUS_PARAMETER_RANGE;
	}

cleanup:
	if (session != NULL) {
		poco_debug_detach(session);
	}
	if (activation != NULL) {
		poco_activation_release(activation);
	}
	for (source_index = 0; source_index < debugger.source_count; ++source_index) {
		free(debugger.sources[source_index].line_offsets);
		free(debugger.sources[source_index].source);
	}
	free(debugger.sources);
	return status;
}
