#include <poco/poco.h>

#include <stdint.h>
#include <stdio.h>

#ifndef POCO_CALL_BUILDER_FIXTURE_DIR
#error "POCO_CALL_BUILDER_FIXTURE_DIR must name the call-builder fixture directory"
#endif

static PocoActivation* active_activation;
static int library_active;
static int failures;

static int check(int condition, const char* context)
{
	if (condition) {
		return 1;
	}
	fprintf(stderr, "poco_call_builder: %s\n", context);
	++failures;
	return 0;
}

static PocoStatus initialize_library(PocoLibrary* library)
{
	(void)library;
	library_active = 1;
	return POCO_STATUS_OK;
}

static void cleanup_library(PocoLibrary* library)
{
	(void)library;
	library_active = 0;
}

static int library_is_active(void)
{
	return library_active;
}

static int invoke_nested_call(void)
{
	PocoCallbackValue result;
	PocoCall* call = NULL;
	int value = -1000;

	if (poco_call_begin(active_activation, "add_ints", &call) == POCO_STATUS_OK &&
		poco_call_push_int(call, 1) == POCO_STATUS_OK &&
		poco_call_push_int(call, 2) == POCO_STATUS_OK &&
		poco_call_invoke(call, &result) == POCO_STATUS_OK &&
		result.kind == POCO_CALLBACK_VALUE_INT) {
		value = result.value.int_value;
	}
	poco_call_end(call);
	return value;
}

int main(void)
{
	static const PocoBinding bindings[] = {
		{"int invoke_nested_call();", (PocoNativeFunction)invoke_nested_call, NULL, 0},
		{"int library_is_active();", (PocoNativeFunction)library_is_active, NULL, 0},
	};
	static const PocoLibrary library = {
		"test.call_builder", bindings,        sizeof(bindings) / sizeof(bindings[0]),
		initialize_library,  cleanup_library, NULL,
	};
	PocoVm* vm = NULL;
	PocoProgram* program = NULL;
	PocoActivation* activation = NULL;
	PocoCall* call = NULL;
	PocoCall* missing = (PocoCall*)(uintptr_t)1;
	PocoCallbackValue result;
	char path[1024];
	int32_t main_result = -1;
	int ok = 1;

	ok &= check(
		poco_call_begin(NULL, "add_ints", &call) == POCO_STATUS_NULL_REFERENCE && call == NULL,
		"null activation begin is rejected and clears output");
	ok &= check(poco_call_begin(NULL, "add_ints", NULL) == POCO_STATUS_NULL_REFERENCE,
				"null call output is rejected");
	ok &= check(poco_call_push_int(NULL, 1) == POCO_STATUS_NULL_REFERENCE, "null push is rejected");
	ok &= check(poco_call_invoke(NULL, &result) == POCO_STATUS_NULL_REFERENCE &&
					result.kind == POCO_CALLBACK_VALUE_INVALID,
				"null invoke is rejected with invalid result");
	poco_call_end(NULL);
	if (snprintf(path, sizeof(path), "%s/call_builder.poc", POCO_CALL_BUILDER_FIXTURE_DIR) >=
		(int)sizeof(path)) {
		return 1;
	}
	ok &= check(poco_vm_create(NULL, &vm) == POCO_STATUS_OK, "create VM");
	ok &= check(poco_vm_register_library(vm, &library) == POCO_STATUS_OK,
				"register reentrant fixture library");
	ok &= check(poco_vm_compile_file(vm, path, &program) == POCO_STATUS_OK,
				"compile call-builder fixture");
	ok &= check(poco_activation_acquire(program, &activation) == POCO_STATUS_OK,
				"acquire activation");
	active_activation = activation;
	ok &= check(poco_activation_init(activation) == POCO_STATUS_OK && library_active,
				"initialize activation globals and library state");

	ok &= check(poco_call_begin(activation, "missing", &missing) == POCO_STATUS_NOT_FOUND &&
					missing == NULL,
				"unknown function is rejected without allocating a handle");
	ok &= check(poco_call_begin(activation, "add_ints", &call) == POCO_STATUS_OK,
				"begin int-returning call");
	ok &= check(poco_call_push_double(call, 3.9) == POCO_STATUS_OK,
				"push truncating double argument");
	ok &= check(poco_call_push_int(call, 4) == POCO_STATUS_OK, "push matching int argument");
	ok &= check(poco_call_invoke(call, &result) == POCO_STATUS_OK &&
					result.kind == POCO_CALLBACK_VALUE_INT && result.value.int_value == 17,
				"double-to-int truncation produces typed int result");
	ok &= check(poco_call_invoke(call, &result) == POCO_STATUS_PARAMETER_RANGE &&
					result.kind == POCO_CALLBACK_VALUE_INVALID,
				"a second invoke is rejected without rerunning");
	ok &= check(poco_call_push_int(call, 1) == POCO_STATUS_PARAMETER_RANGE,
				"push after invoke is rejected");
	poco_call_end(call);
	call = NULL;

	ok &= check(poco_call_begin(activation, "mix_values", &call) == POCO_STATUS_OK,
				"begin double-returning call");
	ok &= check(poco_call_push_int(call, 2) == POCO_STATUS_OK, "push widening int argument");
	ok &= check(poco_call_push_double(call, 3.9) == POCO_STATUS_OK,
				"push truncating double-to-int argument");
	ok &= check(poco_call_invoke(call, &result) == POCO_STATUS_OK &&
					result.kind == POCO_CALLBACK_VALUE_DOUBLE && result.value.double_value == 5.5,
				"int-to-double widening and double-to-int truncation are exact");
	poco_call_end(call);
	call = NULL;

	ok &= check(poco_call_begin(activation, "add_longs", &call) == POCO_STATUS_OK,
				"begin long-returning call");
	ok &=
		check(poco_call_push_int(call, 5) == POCO_STATUS_OK, "push widening int-to-long argument");
	ok &= check(poco_call_push_double(call, 6.9) == POCO_STATUS_OK,
				"push truncating double-to-long argument");
	ok &= check(poco_call_invoke(call, &result) == POCO_STATUS_OK &&
					result.kind == POCO_CALLBACK_VALUE_LONG && result.value.long_value == 11,
				"numeric coercion produces typed long result");
	poco_call_end(call);
	call = NULL;

	ok &= check(poco_call_begin(activation, "store_value", &call) == POCO_STATUS_OK &&
					poco_call_push_long(call, 27) == POCO_STATUS_OK &&
					poco_call_invoke(call, &result) == POCO_STATUS_OK &&
					result.kind == POCO_CALLBACK_VALUE_INVALID,
				"void return yields invalid sentinel");
	poco_call_end(call);
	call = NULL;

	ok &=
		check(poco_call_begin(activation, "return_pointer", &call) == POCO_STATUS_OK &&
				  poco_call_invoke(call, &result) == POCO_STATUS_OK &&
				  result.kind == POCO_CALLBACK_VALUE_POPOT && result.value.popot_value.pt != NULL &&
				  result.value.popot_value.min == result.value.popot_value.pt &&
				  (char*)result.value.popot_value.max - (char*)result.value.popot_value.pt + 1 ==
					  (int)sizeof(int),
			  "pointer return preserves its bounded Popot");
	poco_call_end(call);
	call = NULL;

	ok &= check(poco_call_begin(activation, "add_ints", &call) == POCO_STATUS_OK &&
					poco_call_push_int(call, 1) == POCO_STATUS_OK &&
					poco_call_invoke(call, &result) == POCO_STATUS_PARAMETER_RANGE,
				"wrong argument count is rejected at invoke");
	ok &= check(poco_call_invoke(call, NULL) == POCO_STATUS_PARAMETER_RANGE,
				"failed invoke still consumes the one-shot handle");
	poco_call_end(call);
	call = NULL;

	ok &= check(poco_call_begin(activation, "read_pointer", &call) == POCO_STATUS_OK &&
					poco_call_push_int(call, 1) == POCO_STATUS_OK &&
					poco_call_invoke(call, &result) == POCO_STATUS_PARAMETER_RANGE,
				"numeric argument cannot coerce to pointer parameter");
	poco_call_end(call);
	call = NULL;

	ok &= check(poco_call_begin(activation, "outer_reentrant_call", &call) == POCO_STATUS_OK &&
					poco_call_invoke(call, &result) == POCO_STATUS_OK &&
					result.kind == POCO_CALLBACK_VALUE_INT && result.value.int_value == 31 &&
					!library_active,
				"reentrant builder call preserves outer library state and balances cleanup");
	poco_call_end(call);
	call = NULL;

	ok &= check(poco_activation_reset(activation) == POCO_STATUS_OK &&
					poco_activation_init(activation) == POCO_STATUS_OK,
				"reset and reinitialize before main");
	ok &= check(poco_activation_run_main(activation, 0, NULL, &main_result) == POCO_STATUS_OK &&
					main_result == 20,
				"run main before a named call");
	ok &= check(poco_call_begin(activation, "add_ints", &call) == POCO_STATUS_OK &&
					poco_call_push_int(call, 1) == POCO_STATUS_OK &&
					poco_call_push_int(call, 2) == POCO_STATUS_OK &&
					poco_call_invoke(call, &result) == POCO_STATUS_OK &&
					result.kind == POCO_CALLBACK_VALUE_INT && result.value.int_value == 23,
				"named call can inspect globals after a completed main run");
	poco_call_end(call);

	active_activation = NULL;
	poco_activation_release(activation);
	poco_program_destroy(program);
	poco_vm_destroy(vm);
	return ok && failures == 0 ? 0 : 1;
}
