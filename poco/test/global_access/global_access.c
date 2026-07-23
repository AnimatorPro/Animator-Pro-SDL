#include <poco/poco.h>

#include "pocoface.h"

#include <float.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef POCO_GLOBAL_ACCESS_FIXTURE_DIR
#error "POCO_GLOBAL_ACCESS_FIXTURE_DIR must name the global-access fixture directory"
#endif

static int check(int condition, const char* context)
{
	if (condition) {
		return 1;
	}
	fprintf(stderr, "poco_global_access: %s\n", context);
	return 0;
}

static PocoCallbackValue int_value(int value)
{
	PocoCallbackValue result = {POCO_CALLBACK_VALUE_INT, {.int_value = value}};
	return result;
}

static PocoCallbackValue long_value(long value)
{
	PocoCallbackValue result = {POCO_CALLBACK_VALUE_LONG, {.long_value = value}};
	return result;
}

static PocoCallbackValue double_value(double value)
{
	PocoCallbackValue result = {POCO_CALLBACK_VALUE_DOUBLE, {.double_value = value}};
	return result;
}

static PocoCallbackValue pointer_value(Popot value)
{
	PocoCallbackValue result = {POCO_CALLBACK_VALUE_POPOT, {.popot_value = value}};
	return result;
}

static int exercise_program(PocoProgram* program)
{
	PocoActivation* activation = NULL;
	PocoCallbackValue value;
	PocoStatus status;
	int external = 7;
	Popot bounded = {&external, &external, (char*)&external + sizeof(external) - 1};
	Popot unbounded = {&external, NULL, NULL};
	int ok = 1;

	ok &= check(poco_activation_acquire(program, &activation) == POCO_STATUS_OK,
				"acquire activation");
	if (!ok) {
		return 0;
	}
	value = poco_activation_get_global(activation, "seeded");
	ok &= check(value.kind == POCO_CALLBACK_VALUE_INT && value.value.int_value == 0,
				"pre-init global reflects zeroed activation data");
	ok &= check(poco_activation_init(activation) == POCO_STATUS_OK, "initialize activation");
	value = poco_activation_get_global(activation, "seeded");
	ok &= check(value.kind == POCO_CALLBACK_VALUE_INT && value.value.int_value == 5,
				"read initialized int global");
	value = poco_activation_get_global(activation, "pointer_value");
	ok &= check(value.kind == POCO_CALLBACK_VALUE_POPOT && value.value.popot_value.pt == NULL &&
					value.value.popot_value.min == NULL && value.value.popot_value.max == NULL,
				"read initialized null pointer global");

	ok &= check(poco_activation_set_global(activation, "seeded", int_value(21)) == POCO_STATUS_OK,
				"set int global");
	ok &= check(
		poco_activation_set_global(activation, "char_value", int_value(-12)) == POCO_STATUS_OK,
		"set char global through int kind");
	ok &= check(
		poco_activation_set_global(activation, "short_value", int_value(1234)) == POCO_STATUS_OK,
		"set short global through int kind");
	ok &= check(poco_activation_set_global(activation, "long_value", long_value(1234567L)) ==
					POCO_STATUS_OK,
				"set long global");
	ok &= check(
		poco_activation_set_global(activation, "float_value", double_value(1.5)) == POCO_STATUS_OK,
		"set float global through double kind");
	ok &= check(poco_activation_set_global(activation, "double_value", double_value(2.25)) ==
					POCO_STATUS_OK,
				"set double global");
	ok &= check(poco_activation_set_global(activation, "pointer_value", pointer_value(bounded)) ==
					POCO_STATUS_OK,
				"set bounded pointer global");

	value = poco_activation_get_global(activation, "char_value");
	ok &= check(value.kind == POCO_CALLBACK_VALUE_INT && value.value.int_value == -12,
				"round-trip char global");
	value = poco_activation_get_global(activation, "short_value");
	ok &= check(value.kind == POCO_CALLBACK_VALUE_INT && value.value.int_value == 1234,
				"round-trip short global");
	value = poco_activation_get_global(activation, "long_value");
	ok &= check(value.kind == POCO_CALLBACK_VALUE_LONG && value.value.long_value == 1234567L,
				"round-trip long global");
	value = poco_activation_get_global(activation, "float_value");
	ok &= check(value.kind == POCO_CALLBACK_VALUE_DOUBLE && value.value.double_value == 1.5,
				"round-trip float global");
	value = poco_activation_get_global(activation, "double_value");
	ok &= check(value.kind == POCO_CALLBACK_VALUE_DOUBLE && value.value.double_value == 2.25,
				"round-trip double global");
	value = poco_activation_get_global(activation, "pointer_value");
	ok &= check(
		value.kind == POCO_CALLBACK_VALUE_POPOT && value.value.popot_value.pt == &external &&
			value.value.popot_value.min == &external && value.value.popot_value.max == bounded.max,
		"round-trip bounded pointer global");

	ok &= check(
		poco_activation_set_global(activation, "missing", int_value(1)) == POCO_STATUS_NOT_FOUND,
		"reject unknown global set");
	ok &=
		check(poco_activation_get_global(activation, "missing").kind == POCO_CALLBACK_VALUE_INVALID,
			  "unknown global get returns invalid sentinel");
	ok &= check(poco_activation_set_global(activation, "long_value", int_value(1)) ==
					POCO_STATUS_PARAMETER_RANGE,
				"reject wrong scalar kind");
	ok &= check(poco_activation_set_global(activation, "char_value", int_value(1000)) ==
					POCO_STATUS_PARAMETER_RANGE,
				"reject narrowing overflow");
	ok &= check(poco_activation_set_global(activation, "float_value", double_value(DBL_MAX)) ==
					POCO_STATUS_PARAMETER_RANGE,
				"reject float overflow");
	value = poco_activation_get_global(activation, "float_value");
	ok &= check(value.kind == POCO_CALLBACK_VALUE_DOUBLE && value.value.double_value == 1.5,
				"rejected float overflow leaves the global unchanged");
	ok &= check(poco_activation_set_global(activation, "pointer_value", pointer_value(unbounded)) ==
					POCO_STATUS_PARAMETER_RANGE,
				"reject unbounded pointer span");
	ok &= check(
		poco_activation_get_global(activation, "array_value").kind == POCO_CALLBACK_VALUE_INVALID,
		"array get returns invalid sentinel");
	ok &= check(
		poco_activation_get_global(activation, "struct_value").kind == POCO_CALLBACK_VALUE_INVALID,
		"struct get returns invalid sentinel");
	ok &= check(poco_activation_set_global(activation, "array_value", int_value(1)) ==
					POCO_STATUS_PARAMETER_RANGE,
				"reject array set");
	ok &= check(poco_activation_set_global(activation, "struct_value", int_value(1)) ==
					POCO_STATUS_PARAMETER_RANGE,
				"reject struct set");

	status = (PocoStatus)po_activation_run_entry(activation, "main");
	ok &= check(status == POCO_STATUS_OK, "run main without reinitializing globals");
	value = poco_activation_get_global(activation, "computed");
	ok &= check(value.kind == POCO_CALLBACK_VALUE_INT && value.value.int_value == 49,
				"read main-computed global");
	value = poco_activation_get_global(activation, "seeded");
	ok &= check(value.kind == POCO_CALLBACK_VALUE_INT && value.value.int_value == 22,
				"read main-mutated seeded global");

	ok &= check(poco_activation_reset(activation) == POCO_STATUS_OK, "reset activation");
	value = poco_activation_get_global(activation, "computed");
	ok &= check(value.kind == POCO_CALLBACK_VALUE_INT && value.value.int_value == 0,
				"reset discards global values without losing metadata");
	poco_activation_release(activation);
	return ok;
}

int main(void)
{
	PocoVm* vm = NULL;
	PocoProgram* compiled = NULL;
	PocoProgram* decoded = NULL;
	PocoStatus compile_status;
	uint8_t* bytes = NULL;
	size_t byte_count = 0;
	char script_path[1024];
	int ok = 1;

	ok &= check(
		poco_activation_set_global(NULL, "seeded", int_value(1)) == POCO_STATUS_NULL_REFERENCE,
		"null activation set is rejected");
	ok &= check(poco_activation_get_global(NULL, "seeded").kind == POCO_CALLBACK_VALUE_INVALID,
				"null activation get returns invalid sentinel");
	if (snprintf(script_path, sizeof(script_path), "%s/global_access.poc",
				 POCO_GLOBAL_ACCESS_FIXTURE_DIR) >= (int)sizeof(script_path)) {
		return 1;
	}
	ok &= check(poco_vm_create(NULL, &vm) == POCO_STATUS_OK, "create VM");
	compile_status = poco_vm_compile_file(vm, script_path, &compiled);
	if (compile_status != POCO_STATUS_OK) {
		fprintf(stderr, "poco_global_access: compile status=%d diagnostic=%s\n", compile_status,
				poco_get_last_error(vm));
	}
	ok &= check(compile_status == POCO_STATUS_OK, "compile global-access fixture");
	if (!ok || compiled == NULL) {
		goto CLEANUP;
	}
	ok &= exercise_program(compiled);
	ok &= check(poco_program_serialize_buffer(compiled, POCO_DEBUG_LEVEL_MINIMAL, NULL, 0,
											  &byte_count) == POCO_STATUS_BUFFER_TOO_SMALL &&
					byte_count != 0,
				"query serialized program size");
	bytes = (uint8_t*)malloc(byte_count);
	ok &= check(bytes != NULL, "allocate serialized program buffer");
	if (bytes != NULL) {
		size_t written = byte_count;
		ok &= check(poco_program_serialize_buffer(compiled, POCO_DEBUG_LEVEL_MINIMAL, bytes,
												  byte_count, &written) == POCO_STATUS_OK &&
						written == byte_count,
					"serialize program with retained globals");
		ok &= check(poco_vm_deserialize_buffer(vm, bytes, byte_count, &decoded) == POCO_STATUS_OK,
					"deserialize program with retained globals");
		if (decoded != NULL) {
			ok &= exercise_program(decoded);
		}
	}

CLEANUP:
	free(bytes);
	poco_program_destroy(decoded);
	poco_program_destroy(compiled);
	poco_vm_destroy(vm);
	return ok ? 0 : 1;
}
