#include <poco/poco.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef POCO_DEBUG_INSPECTION_SOURCE
#error "POCO_DEBUG_INSPECTION_SOURCE must name the inspection fixture"
#endif

enum {
	BREAKPOINT_LINE = 11,
	LEAF_LINE = 11,
	HELPER_LINE = 18,
	MAIN_LINE = 24,
	EXPECTED_FRAME_COUNT = 3
};

typedef struct SerializedProgram {
	unsigned char* bytes;
	size_t size;
} SerializedProgram;

typedef struct PauseState {
	PocoActivation* activation;
	int expect_locals;
	int calls;
	int failures;
	int reentered;
} PauseState;

static int recorded_value;

static void record(int value)
{
	recorded_value = value;
}

static int check(int condition, const char* message)
{
	if (condition) {
		return 1;
	}
	fprintf(stderr, "poco_debug_inspection: %s\n", message);
	return 0;
}

static int serialize_program(const PocoProgram* program, PocoDebugLevel level,
							 SerializedProgram* serialized)
{
	PocoStatus status;

	serialized->bytes = NULL;
	serialized->size = 0;
	status = poco_program_serialize_buffer(program, level, NULL, 0, &serialized->size);
	if (status != POCO_STATUS_BUFFER_TOO_SMALL || serialized->size == 0) {
		return 0;
	}
	serialized->bytes = malloc(serialized->size);
	if (serialized->bytes == NULL) {
		return 0;
	}
	status = poco_program_serialize_buffer(program, level, serialized->bytes, serialized->size,
										   &serialized->size);
	if (status != POCO_STATUS_OK) {
		free(serialized->bytes);
		serialized->bytes = NULL;
		serialized->size = 0;
		return 0;
	}
	return 1;
}

static void invoke_reentrant_probe(PauseState* state)
{
	PocoCall* call = NULL;
	PocoCallbackValue result;

	state->failures += poco_call_begin(state->activation, "probe", &call) != POCO_STATUS_OK;
	state->failures += call == NULL || poco_call_push_int(call, 40) != POCO_STATUS_OK;
	state->failures += call == NULL || poco_call_invoke(call, &result) != POCO_STATUS_OK ||
					   result.kind != POCO_CALLBACK_VALUE_INT || result.value.int_value != 42;
	poco_call_end(call);
}

static void inspection_pause(PocoDebugSession* session, void* user_data)
{
	PauseState* state = user_data;
	PocoCallbackValue value = {POCO_CALLBACK_VALUE_INT, {.int_value = -1}};
	PocoDebugFrame frames[EXPECTED_FRAME_COUNT];
	PocoDebugFrame canary[2] = {{"unchanged", -1}, {"unchanged", -1}};
	size_t frame_count = 0;
	PocoStatus status;

	++state->calls;
	if (!state->reentered) {
		state->reentered = 1;
		invoke_reentrant_probe(state);
	}
	if (state->expect_locals) {
		status = poco_debug_read_variable(session, "input", &value);
		state->failures += status != POCO_STATUS_OK || value.kind != POCO_CALLBACK_VALUE_INT ||
						   value.value.int_value != 4;
		status = poco_debug_read_variable(session, "local_int", &value);
		state->failures += status != POCO_STATUS_OK || value.kind != POCO_CALLBACK_VALUE_INT ||
						   value.value.int_value != 7;
		status = poco_debug_read_variable(session, "local_long", &value);
		state->failures += status != POCO_STATUS_OK || value.kind != POCO_CALLBACK_VALUE_LONG ||
						   value.value.long_value != 123460L;
		status = poco_debug_read_variable(session, "local_double", &value);
		state->failures += status != POCO_STATUS_OK || value.kind != POCO_CALLBACK_VALUE_DOUBLE ||
						   value.value.double_value != 6.5;
		status = poco_debug_read_variable(session, "local_pointer", &value);
		state->failures += status != POCO_STATUS_OK || value.kind != POCO_CALLBACK_VALUE_POPOT ||
						   value.value.popot_value.pt == NULL ||
						   strcmp(value.value.popot_value.pt, "debug") != 0;
		status = poco_debug_read_variable(session, "shadowed", &value);
		state->failures += status != POCO_STATUS_OK || value.kind != POCO_CALLBACK_VALUE_INT ||
						   value.value.int_value != 22;
		status = poco_debug_read_variable(session, "persistent", &value);
		state->failures += status != POCO_STATUS_OK || value.kind != POCO_CALLBACK_VALUE_INT ||
						   value.value.int_value != 9;
		status = poco_debug_read_variable(session, "caller_only", &value);
		state->failures +=
			status != POCO_STATUS_NOT_FOUND || value.kind != POCO_CALLBACK_VALUE_INVALID;
	} else {
		status = poco_debug_read_variable(session, "local_int", &value);
		state->failures +=
			status != POCO_STATUS_NOT_FOUND || value.kind != POCO_CALLBACK_VALUE_INVALID;
	}

	status = poco_debug_backtrace(session, NULL, 0, &frame_count);
	state->failures +=
		status != POCO_STATUS_BUFFER_TOO_SMALL || frame_count != EXPECTED_FRAME_COUNT;
	status = poco_debug_backtrace(session, NULL, EXPECTED_FRAME_COUNT, &frame_count);
	state->failures +=
		status != POCO_STATUS_BUFFER_TOO_SMALL || frame_count != EXPECTED_FRAME_COUNT;
	status = poco_debug_backtrace(session, canary, 2, &frame_count);
	state->failures += status != POCO_STATUS_BUFFER_TOO_SMALL ||
					   frame_count != EXPECTED_FRAME_COUNT || canary[0].line != -1 ||
					   canary[1].line != -1;
	status = poco_debug_backtrace(session, frames, EXPECTED_FRAME_COUNT, &frame_count);
	state->failures += status != POCO_STATUS_OK || frame_count != EXPECTED_FRAME_COUNT ||
					   strcmp(frames[0].function, "leaf") != 0 || frames[0].line != LEAF_LINE ||
					   strcmp(frames[1].function, "helper") != 0 || frames[1].line != HELPER_LINE ||
					   strcmp(frames[2].function, "main") != 0 || frames[2].line != MAIN_LINE;
	state->failures += poco_debug_continue(session) != POCO_STATUS_OK;
}

static int run_image(PocoVm* vm, const SerializedProgram* image, int expect_locals)
{
	PocoProgram* program = NULL;
	PocoActivation* activation = NULL;
	PocoDebugSession* session = NULL;
	PauseState state = {NULL, expect_locals, 0, 0, 0};
	PocoCallbackValue value;
	PocoDebugFrame frame;
	size_t frame_count = 99;
	int32_t result = -1;
	int ok = 1;

	ok &=
		check(poco_vm_deserialize_buffer(vm, image->bytes, image->size, &program) == POCO_STATUS_OK,
			  "deserialize program image");
	ok &= check(program != NULL && poco_activation_acquire(program, &activation) == POCO_STATUS_OK,
				"acquire image activation");
	state.activation = activation;
	ok &= check(activation != NULL && poco_debug_attach(activation, inspection_pause, &state,
														&session) == POCO_STATUS_OK,
				"attach inspection session");
	ok &= check(
		session != NULL &&
			poco_debug_read_variable(session, "local_int", &value) == POCO_STATUS_PARAMETER_RANGE &&
			value.kind == POCO_CALLBACK_VALUE_INVALID,
		"variable reads require a synchronous pause");
	ok &= check(
		poco_debug_backtrace(session, &frame, 1, &frame_count) == POCO_STATUS_PARAMETER_RANGE &&
			frame_count == 0,
		"backtrace requires a synchronous pause");
	ok &= check(poco_debug_add_line_breakpoint(session, BREAKPOINT_LINE) == POCO_STATUS_OK,
				"add inspection breakpoint");
	recorded_value = -1;
	ok &= check(poco_activation_run(activation, NULL, &result) == POCO_STATUS_OK && result == 7 &&
					recorded_value == 7,
				"run inspected image");
	ok &= check(state.calls == 1 && state.failures == 0,
				expect_locals ? "extended inspection results" : "minimal inspection results");

	poco_debug_detach(session);
	poco_activation_release(activation);
	poco_program_destroy(program);
	return ok;
}

int main(void)
{
	static const PocoBinding bindings[] = {
		{"void record(int value);", (PocoNativeFunction)record, NULL, 0},
	};
	static const PocoLibrary library = {
		"test.debug_inspection", bindings, sizeof(bindings) / sizeof(bindings[0]), NULL, NULL, NULL,
	};
	PocoVm* vm = NULL;
	PocoProgram* compiled = NULL;
	SerializedProgram extended = {0};
	SerializedProgram minimal = {0};
	PocoCallbackValue value;
	size_t frame_count;
	int ok = 1;

	ok &= check(poco_debug_read_variable(NULL, "value", &value) == POCO_STATUS_NULL_REFERENCE,
				"null variable session is rejected");
	ok &= check(poco_debug_backtrace(NULL, NULL, 0, &frame_count) == POCO_STATUS_NULL_REFERENCE,
				"null backtrace session is rejected");
	ok &= check(poco_vm_create(NULL, &vm) == POCO_STATUS_OK, "create VM");
	ok &=
		check(poco_vm_register_library(vm, &library) == POCO_STATUS_OK, "register record binding");
	ok &= check(poco_vm_compile_file(vm, POCO_DEBUG_INSPECTION_SOURCE, &compiled) == POCO_STATUS_OK,
				"compile inspection fixture");
	ok &=
		check(compiled != NULL && serialize_program(compiled, POCO_DEBUG_LEVEL_EXTENDED, &extended),
			  "serialize extended image");
	ok &= check(compiled != NULL && serialize_program(compiled, POCO_DEBUG_LEVEL_MINIMAL, &minimal),
				"serialize minimal image");
	poco_program_destroy(compiled);
	if (extended.bytes != NULL) {
		ok &= run_image(vm, &extended, 1);
	}
	if (minimal.bytes != NULL) {
		ok &= run_image(vm, &minimal, 0);
	}
	free(extended.bytes);
	free(minimal.bytes);
	poco_vm_destroy(vm);
	if (!ok) {
		return 1;
	}
	printf("debug variable inspection and backtrace passed\n");
	return 0;
}
