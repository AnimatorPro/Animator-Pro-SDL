/*
 * poco_call_invoke() honours an activation-installed cancel callback.
 *
 * Without the fix this test does not fail with a bad answer, it hangs:
 * spin_forever() never returns and the callback is never consulted.  The
 * registered ctest timeout is the assertion for that case.
 */

#include <poco/poco.h>

#include <stdint.h>
#include <stdio.h>

#ifndef POCO_CALL_CANCEL_FIXTURE_DIR
#error "POCO_CALL_CANCEL_FIXTURE_DIR must name the call-cancel fixture directory"
#endif

static int failures;

/* Ask for cancellation once the callback has been consulted this many times;
 * a negative budget never cancels. */
static long cancel_budget = -1;
static long cancel_calls;

static int check(int condition, const char* context)
{
	if (condition) {
		return 1;
	}
	fprintf(stderr, "poco_call_cancel: %s\n", context);
	++failures;
	return 0;
}

static int cancel_after_budget(void* user_data)
{
	long* calls = user_data;

	++*calls;
	return cancel_budget >= 0 && *calls >= cancel_budget;
}

int main(void)
{
	PocoVm* vm = NULL;
	PocoProgram* program = NULL;
	PocoActivation* activation = NULL;
	PocoCall* call = NULL;
	PocoCallbackValue result;
	PocoRunOptions run_options = {NULL, NULL, NULL};
	char path[1024];
	long calls_before_clear;
	int32_t main_result = -1;

	if (snprintf(path, sizeof(path), "%s/call_cancel.poc", POCO_CALL_CANCEL_FIXTURE_DIR) >=
		(int)sizeof(path)) {
		return 1;
	}

	check(poco_activation_set_cancel_callback(NULL, cancel_after_budget, &cancel_calls) ==
			  POCO_STATUS_NULL_REFERENCE,
		  "a null activation is rejected by the setter");

	if (!check(poco_vm_create(NULL, &vm) == POCO_STATUS_OK, "create VM") ||
		!check(poco_vm_compile_file(vm, path, &program) == POCO_STATUS_OK,
			   "compile cancellation fixture") ||
		!check(poco_activation_acquire(program, &activation) == POCO_STATUS_OK,
			   "acquire activation") ||
		!check(poco_activation_init(activation) == POCO_STATUS_OK, "initialize activation")) {
		poco_program_destroy(program);
		poco_vm_destroy(vm);
		return 1;
	}

	/* No callback installed: a loop runs to completion and nothing is
	 * consulted. */
	cancel_budget = -1;
	cancel_calls = 0;
	check(poco_call_begin(activation, "bounded_loop", &call) == POCO_STATUS_OK &&
			  poco_call_push_int(call, 100) == POCO_STATUS_OK &&
			  poco_call_invoke(call, &result) == POCO_STATUS_OK &&
			  result.kind == POCO_CALLBACK_VALUE_INT && result.value.int_value == 4950,
		  "an uninstrumented call returns its own answer");
	poco_call_end(call);
	call = NULL;
	check(cancel_calls == 0, "no callback is consulted before one is installed");

	/* Installed but never asking to stop: same answer, and the check really
	 * does run on the loop's backward branch. */
	check(poco_activation_set_cancel_callback(activation, cancel_after_budget, &cancel_calls) ==
			  POCO_STATUS_OK,
		  "install the cancel callback");
	check(poco_call_begin(activation, "bounded_loop", &call) == POCO_STATUS_OK &&
			  poco_call_push_int(call, 100) == POCO_STATUS_OK &&
			  poco_call_invoke(call, &result) == POCO_STATUS_OK &&
			  result.kind == POCO_CALLBACK_VALUE_INT && result.value.int_value == 4950,
		  "an uncancelled call is unaffected by an installed callback");
	poco_call_end(call);
	call = NULL;
	check(cancel_calls > 0, "the callback is consulted while a loop runs");

	/* The case that used to hang. */
	cancel_calls = 0;
	cancel_budget = 1000;
	check(poco_call_begin(activation, "spin_forever", &call) == POCO_STATUS_OK &&
			  poco_call_invoke(call, &result) == POCO_STATUS_ABORTED &&
			  result.kind == POCO_CALLBACK_VALUE_INVALID,
		  "a non-terminating by-name call is aborted");
	poco_call_end(call);
	call = NULL;
	check(cancel_calls >= cancel_budget, "the callback was consulted until it said stop");

	/* The activation survives the abort. */
	cancel_budget = -1;
	check(poco_call_begin(activation, "add_ints", &call) == POCO_STATUS_OK &&
			  poco_call_push_int(call, 2) == POCO_STATUS_OK &&
			  poco_call_push_int(call, 3) == POCO_STATUS_OK &&
			  poco_call_invoke(call, &result) == POCO_STATUS_OK &&
			  result.kind == POCO_CALLBACK_VALUE_INT && result.value.int_value == 5,
		  "a call after a cancelled call still returns the right answer");
	poco_call_end(call);
	call = NULL;
	check(poco_call_begin(activation, "bounded_loop", &call) == POCO_STATUS_OK &&
			  poco_call_push_int(call, 100) == POCO_STATUS_OK &&
			  poco_call_invoke(call, &result) == POCO_STATUS_OK &&
			  result.value.int_value == 4950,
		  "a looping call after a cancelled call still completes");
	poco_call_end(call);
	call = NULL;

	/* main still runs through the run-options path on the same activation. */
	check(poco_activation_reset(activation) == POCO_STATUS_OK &&
			  poco_activation_run(activation, &run_options, &main_result) == POCO_STATUS_OK &&
			  main_result == 7,
		  "poco_activation_run still works after a cancelled by-name call");

	/* A run with its own options borrows the hook and gives it back: the
	 * installed callback still cancels a by-name call afterwards. */
	check(poco_activation_reset(activation) == POCO_STATUS_OK &&
			  poco_activation_init(activation) == POCO_STATUS_OK,
		  "reset and re-initialize the activation");
	cancel_calls = 0;
	cancel_budget = 1000;
	check(poco_call_begin(activation, "spin_forever", &call) == POCO_STATUS_OK &&
			  poco_call_invoke(call, &result) == POCO_STATUS_ABORTED,
		  "the installed callback survives a poco_activation_run");
	poco_call_end(call);
	call = NULL;

	/* Clearing the callback stops the consultation again. */
	cancel_budget = -1;
	check(poco_activation_reset(activation) == POCO_STATUS_OK &&
			  poco_activation_init(activation) == POCO_STATUS_OK,
		  "reset and re-initialize the activation again");
	check(poco_activation_set_cancel_callback(activation, NULL, NULL) == POCO_STATUS_OK,
		  "clear the cancel callback");
	calls_before_clear = cancel_calls;
	check(poco_call_begin(activation, "bounded_loop", &call) == POCO_STATUS_OK &&
			  poco_call_push_int(call, 100) == POCO_STATUS_OK &&
			  poco_call_invoke(call, &result) == POCO_STATUS_OK &&
			  result.value.int_value == 4950,
		  "a call with the callback cleared completes");
	poco_call_end(call);
	call = NULL;
	check(cancel_calls == calls_before_clear, "a cleared callback is not consulted");

	poco_activation_release(activation);
	poco_program_destroy(program);
	poco_vm_destroy(vm);
	if (failures != 0) {
		fprintf(stderr, "poco_call_cancel: %d check(s) failed\n", failures);
		return 1;
	}
	return 0;
}
