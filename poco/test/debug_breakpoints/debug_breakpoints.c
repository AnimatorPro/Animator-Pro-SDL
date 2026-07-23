#include <poco/poco.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef POCO_DEBUG_BREAKPOINT_SOURCE
#error "POCO_DEBUG_BREAKPOINT_SOURCE must name the breakpoint fixture"
#endif

enum { BREAKPOINT_LINE = 9, EXPECTED_EVENT_COUNT = 3 };

typedef struct PauseState {
	int calls;
	int location_ok;
	int pre_line_output_ok;
	PocoStatus clear_status;
	long line;
	const char* file;
} PauseState;

static int events[EXPECTED_EVENT_COUNT];
static size_t event_count;

static void record(int value)
{
	if (event_count < EXPECTED_EVENT_COUNT) {
		events[event_count] = value;
	}
	++event_count;
}

static int check(int condition, const char* message)
{
	if (condition) {
		return 1;
	}
	fprintf(stderr, "poco_debug_breakpoints: %s\n", message);
	return 0;
}

static void reset_events(void)
{
	memset(events, 0, sizeof(events));
	event_count = 0;
}

static int expected_output(void)
{
	return event_count == EXPECTED_EVENT_COUNT && events[0] == 10 && events[1] == 7 &&
		   events[2] == 14;
}

static void breakpoint_pause(PocoDebugSession* session, void* user_data)
{
	PauseState* state = (PauseState*)user_data;
	PocoDebugLocation location;

	++state->calls;
	state->pre_line_output_ok = event_count == 1 && events[0] == 10;
	state->location_ok = poco_debug_current_location(session, &location) == POCO_STATUS_OK;
	if (state->location_ok) {
		state->line = location.line;
		state->file = location.file;
	}
	state->clear_status = poco_debug_clear_line_breakpoint(session, BREAKPOINT_LINE);
}

int main(void)
{
	static const PocoBinding bindings[] = {
		{"void record(int value);", (PocoNativeFunction)record, NULL, 0},
	};
	static const PocoLibrary library = {
		"test.debug_breakpoints",
		bindings,
		sizeof(bindings) / sizeof(bindings[0]),
		NULL,
		NULL,
		NULL,
	};
	PocoVm* vm = NULL;
	PocoProgram* compiled = NULL;
	PocoProgram* loaded = NULL;
	PocoActivation* activation = NULL;
	PocoDebugSession* session = NULL;
	PocoDebugSession* duplicate = (PocoDebugSession*)(uintptr_t)1;
	PocoDebugLocation location = {(const char*)(uintptr_t)1, -1};
	PauseState pause_state = {0};
	void* image = NULL;
	size_t image_size = 0;
	int32_t result = -1;
	int32_t baseline_result = -1;
	int ok = 1;

	ok &= check(poco_debug_attach(NULL, breakpoint_pause, &pause_state, &session) ==
						POCO_STATUS_NULL_REFERENCE &&
					session == NULL,
				"null activation is rejected");
	ok &= check(poco_vm_create(NULL, &vm) == POCO_STATUS_OK, "create VM");
	ok &=
		check(poco_vm_register_library(vm, &library) == POCO_STATUS_OK, "register record binding");
	ok &= check(poco_vm_compile_file(vm, POCO_DEBUG_BREAKPOINT_SOURCE, &compiled) == POCO_STATUS_OK,
				"compile fixture");
	reset_events();
	ok &= check(poco_vm_run(vm, compiled, NULL, &baseline_result) == POCO_STATUS_OK &&
					baseline_result == 14 && expected_output(),
				"unattached baseline result/output");
	ok &= check(poco_program_serialize_buffer(compiled, POCO_DEBUG_LEVEL_MINIMAL, NULL, 0,
											  &image_size) == POCO_STATUS_BUFFER_TOO_SMALL &&
					image_size > 0,
				"size minimal image");
	image = malloc(image_size);
	ok &= check(image != NULL &&
					poco_program_serialize_buffer(compiled, POCO_DEBUG_LEVEL_MINIMAL, image,
												  image_size, &image_size) == POCO_STATUS_OK &&
					poco_vm_deserialize_buffer(vm, image, image_size, &loaded) == POCO_STATUS_OK,
				"round-trip minimal image");
	ok &=
		check(poco_activation_acquire(loaded, &activation) == POCO_STATUS_OK, "acquire activation");
	ok &= check(
		poco_debug_attach(activation, NULL, &pause_state, &session) == POCO_STATUS_NULL_REFERENCE &&
			session == NULL,
		"null pause callback is rejected");
	ok &= check(
		poco_debug_attach(activation, breakpoint_pause, &pause_state, &session) == POCO_STATUS_OK &&
			session != NULL,
		"attach debug session");
	ok &= check(poco_debug_attach(activation, breakpoint_pause, &pause_state, &duplicate) ==
						POCO_STATUS_PARAMETER_RANGE &&
					duplicate == NULL,
				"second session is rejected");
	ok &= check(poco_debug_current_location(session, &location) == POCO_STATUS_NOT_FOUND &&
					location.file == NULL && location.line == 0,
				"location is unavailable before execution");
	ok &= check(poco_debug_add_line_breakpoint(session, 0) == POCO_STATUS_PARAMETER_RANGE &&
					poco_debug_add_line_breakpoint(session, 9999) == POCO_STATUS_NOT_FOUND &&
					poco_debug_clear_line_breakpoint(session, 9999) == POCO_STATUS_NOT_FOUND,
				"invalid and unmapped lines are rejected");
	ok &= check(poco_debug_add_line_breakpoint(session, BREAKPOINT_LINE) == POCO_STATUS_OK &&
					poco_debug_add_line_breakpoint(session, BREAKPOINT_LINE) == POCO_STATUS_OK,
				"breakpoint add is idempotent");
	reset_events();
	ok &= check(poco_activation_run(activation, NULL, &result) == POCO_STATUS_OK && result == 14 &&
					expected_output(),
				"breakpoint run completes with baseline result/output");
	ok &=
		check(pause_state.calls == 1 && pause_state.location_ok && pause_state.pre_line_output_ok &&
				  pause_state.line == BREAKPOINT_LINE && pause_state.file != NULL &&
				  strcmp(pause_state.file, "debug_breakpoints.poc") == 0 &&
				  pause_state.clear_status == POCO_STATUS_OK,
			  "pause occurs before the breakpoint line with minimal source location");

	ok &= check(poco_activation_reset(activation) == POCO_STATUS_OK, "reset after breakpoint run");
	reset_events();
	result = -1;
	ok &= check(poco_activation_run(activation, NULL, &result) == POCO_STATUS_OK && result == 14 &&
					expected_output() && pause_state.calls == 1,
				"cleared breakpoint does not pause and preserves output");

	ok &= check(poco_activation_reset(activation) == POCO_STATUS_OK, "reset before clear test");
	ok &= check(poco_debug_add_line_breakpoint(session, BREAKPOINT_LINE) == POCO_STATUS_OK &&
					poco_debug_clear_line_breakpoint(session, BREAKPOINT_LINE) == POCO_STATUS_OK,
				"clear configured breakpoint");
	reset_events();
	result = -1;
	ok &= check(poco_activation_run(activation, NULL, &result) == POCO_STATUS_OK && result == 14 &&
					expected_output() && pause_state.calls == 1,
				"pre-run clear does not pause");

	ok &= check(poco_activation_reset(activation) == POCO_STATUS_OK, "reset before detached run");
	poco_debug_detach(session);
	session = NULL;
	reset_events();
	result = -1;
	ok &= check(poco_activation_run(activation, NULL, &result) == POCO_STATUS_OK && result == 14 &&
					expected_output() && pause_state.calls == 1,
				"detached run is identical and does not pause");

	poco_debug_detach(session);
	poco_activation_release(activation);
	free(image);
	poco_program_destroy(loaded);
	poco_program_destroy(compiled);
	poco_vm_destroy(vm);
	if (!ok) {
		return 1;
	}
	printf("debug attach/location/breakpoints passed\n");
	return 0;
}
