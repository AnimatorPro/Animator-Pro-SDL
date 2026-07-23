#include <poco/poco.h>

#include <stdio.h>
#include <string.h>

#ifndef POCO_DEBUG_STEPPING_SOURCE
#error "POCO_DEBUG_STEPPING_SOURCE must name the stepping fixture"
#endif

enum {
	LEAF_FIRST_LINE = 5,
	LEAF_RETURN_LINE = 6,
	HELPER_FIRST_LINE = 11,
	HELPER_CALL_LINE = 12,
	MAIN_CALL_LINE = 22,
	MAIN_AFTER_CALL_LINE = 23,
	MAIN_FIRST_MAPPED_LINE = 17,
	EXPECTED_EVENT_COUNT = 5,
	MAX_PAUSES = 12
};

typedef enum Scenario {
	SCENARIO_STEP_NESTED,
	SCENARIO_NEXT_OVER,
	SCENARIO_INITIAL_STEP
} Scenario;

typedef struct PauseState {
	Scenario scenario;
	long lines[MAX_PAUSES];
	int calls;
	int location_failures;
	int command_failures;
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
	fprintf(stderr, "poco_debug_stepping: %s\n", message);
	return 0;
}

static void reset_events(void)
{
	memset(events, 0, sizeof(events));
	event_count = 0;
}

static int expected_output(void)
{
	return event_count == EXPECTED_EVENT_COUNT && events[0] == 10 && events[1] == 201 &&
		   events[2] == 101 && events[3] == 302 && events[4] == 23;
}

static void issue_command(PauseState* state, PocoStatus status)
{
	if (status != POCO_STATUS_OK) {
		++state->command_failures;
	}
}

static void stepping_pause(PocoDebugSession* session, void* user_data)
{
	PauseState* state = (PauseState*)user_data;
	PocoDebugLocation location;
	long line = 0;

	if (poco_debug_current_location(session, &location) != POCO_STATUS_OK) {
		++state->location_failures;
	} else {
		line = location.line;
	}
	if (state->calls < MAX_PAUSES) {
		state->lines[state->calls] = line;
	}
	++state->calls;

	if (state->scenario == SCENARIO_INITIAL_STEP) {
		issue_command(state, poco_debug_continue(session));
	} else if (line == MAIN_CALL_LINE) {
		issue_command(state, poco_debug_clear_line_breakpoint(session, MAIN_CALL_LINE));
		issue_command(state, state->scenario == SCENARIO_STEP_NESTED
								 ? poco_debug_step_into(session)
								 : poco_debug_next_over(session));
	} else if (state->scenario == SCENARIO_STEP_NESTED && line == HELPER_FIRST_LINE) {
		issue_command(state, poco_debug_step_into(session));
	} else if (state->scenario == SCENARIO_STEP_NESTED && line == HELPER_CALL_LINE &&
			   state->calls == 3) {
		issue_command(state, poco_debug_step_into(session));
	} else if (state->scenario == SCENARIO_STEP_NESTED &&
			   (line == LEAF_FIRST_LINE || line == LEAF_RETURN_LINE)) {
		issue_command(state, poco_debug_next_over(session));
	} else {
		issue_command(state, poco_debug_continue(session));
	}
}

static int check_lines(const PauseState* state, const long* expected, int count,
					   const char* message)
{
	int index;

	if (state->calls != count) {
		fprintf(stderr, "poco_debug_stepping: %s (got %d pauses)", message, state->calls);
		for (index = 0; index < state->calls && index < MAX_PAUSES; ++index) {
			fprintf(stderr, " %ld", state->lines[index]);
		}
		fputc('\n', stderr);
		return 0;
	}
	for (index = 0; index < count; ++index) {
		if (state->lines[index] != expected[index]) {
			fprintf(stderr, "poco_debug_stepping: %s (pause %d got line %ld, expected %ld)\n",
					message, index, state->lines[index], expected[index]);
			return 0;
		}
	}
	return 1;
}

int main(void)
{
	static const PocoBinding bindings[] = {
		{"void record(int value);", (PocoNativeFunction)record, NULL, 0},
	};
	static const PocoLibrary library = {
		"test.debug_stepping", bindings, sizeof(bindings) / sizeof(bindings[0]), NULL, NULL, NULL,
	};
	static const long step_lines[] = {MAIN_CALL_LINE,  HELPER_FIRST_LINE, HELPER_CALL_LINE,
									  LEAF_FIRST_LINE, LEAF_RETURN_LINE,  HELPER_CALL_LINE};
	static const long next_lines[] = {MAIN_CALL_LINE, MAIN_AFTER_CALL_LINE};
	static const long initial_step_lines[] = {MAIN_FIRST_MAPPED_LINE};
	PocoVm* vm = NULL;
	PocoProgram* program = NULL;
	PocoActivation* activation = NULL;
	PocoDebugSession* session = NULL;
	PauseState state = {0};
	int32_t result = -1;
	int ok = 1;

	ok &= check(poco_debug_step_into(NULL) == POCO_STATUS_NULL_REFERENCE &&
					poco_debug_next_over(NULL) == POCO_STATUS_NULL_REFERENCE &&
					poco_debug_continue(NULL) == POCO_STATUS_NULL_REFERENCE,
				"null stepping sessions are rejected");
	ok &= check(poco_vm_create(NULL, &vm) == POCO_STATUS_OK, "create VM");
	ok &=
		check(poco_vm_register_library(vm, &library) == POCO_STATUS_OK, "register record binding");
	ok &= check(poco_vm_compile_file(vm, POCO_DEBUG_STEPPING_SOURCE, &program) == POCO_STATUS_OK,
				"compile stepping fixture");
	ok &= check(poco_activation_acquire(program, &activation) == POCO_STATUS_OK,
				"acquire activation");
	ok &= check(poco_debug_attach(activation, stepping_pause, &state, &session) == POCO_STATUS_OK,
				"attach debug session");
	ok &= check(poco_debug_continue(session) == POCO_STATUS_PARAMETER_RANGE,
				"continue requires a synchronous pause");
	state.scenario = SCENARIO_INITIAL_STEP;
	ok &= check(poco_debug_step_into(session) == POCO_STATUS_OK,
				"step can arm a fresh activation");
	reset_events();
	ok &= check(poco_activation_run(activation, NULL, &result) == POCO_STATUS_OK && result == 3 &&
					expected_output(),
				"initial step run preserves result/output");
	ok &= check(state.location_failures == 0 && state.command_failures == 0,
				"initial step callback and continue succeed");
	ok &= check_lines(&state, initial_step_lines, 1,
					  "initial step stops at the first mapped source line");

	ok &= check(poco_activation_reset(activation) == POCO_STATUS_OK,
				"reset after initial step run");
	memset(&state, 0, sizeof(state));
	state.scenario = SCENARIO_INITIAL_STEP;
	ok &= check(poco_debug_next_over(session) == POCO_STATUS_OK,
				"next can arm a reset activation");
	reset_events();
	result = -1;
	ok &= check(poco_activation_run(activation, NULL, &result) == POCO_STATUS_OK && result == 3 &&
					expected_output(),
				"initial next run preserves result/output");
	ok &= check(state.location_failures == 0 && state.command_failures == 0,
				"initial next callback and continue succeed");
	ok &= check_lines(&state, initial_step_lines, 1,
					  "initial next stops at the first mapped source line");
	ok &= check(poco_activation_reset(activation) == POCO_STATUS_OK,
				"reset after initial next run");
	memset(&state, 0, sizeof(state));
	state.scenario = SCENARIO_STEP_NESTED;

	ok &= check(poco_debug_add_line_breakpoint(session, MAIN_CALL_LINE) == POCO_STATUS_OK,
				"add nested-step start breakpoint");
	reset_events();
	ok &= check(poco_activation_run(activation, NULL, &result) == POCO_STATUS_OK && result == 3 &&
					expected_output(),
				"nested stepping run preserves result/output");
	ok &= check(state.location_failures == 0 && state.command_failures == 0,
				"nested stepping callbacks and commands succeed");
	ok &= check_lines(&state, step_lines, (int)(sizeof(step_lines) / sizeof(step_lines[0])),
					  "step enters nested calls and next stops at the caller return boundary");

	ok &= check(poco_activation_reset(activation) == POCO_STATUS_OK, "reset before next-over run");
	memset(&state, 0, sizeof(state));
	state.scenario = SCENARIO_NEXT_OVER;
	ok &= check(poco_debug_add_line_breakpoint(session, MAIN_CALL_LINE) == POCO_STATUS_OK,
				"add next-over start breakpoint");
	reset_events();
	result = -1;
	ok &= check(poco_activation_run(activation, NULL, &result) == POCO_STATUS_OK && result == 3 &&
					expected_output(),
				"next-over run preserves result/output through continue");
	ok &= check(state.location_failures == 0 && state.command_failures == 0,
				"next-over callbacks and commands succeed");
	ok &= check_lines(&state, next_lines, (int)(sizeof(next_lines) / sizeof(next_lines[0])),
					  "next runs nested calls to completion and stops in caller");

	poco_debug_detach(session);
	poco_activation_release(activation);
	poco_program_destroy(program);
	poco_vm_destroy(vm);
	if (!ok) {
		return 1;
	}
	printf("gdb-like debug stepping passed\n");
	return 0;
}
