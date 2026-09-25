/*
 * Regression test for float parameters on script functions.
 *
 * Every caller - a script call or a host poco_call - promotes a float
 * argument to double and pushes a double-sized slot. The callee read that
 * slot as a float, so it saw the low half of the double: 25.0 arrived as 0
 * and 0.1 as -1.08e-19. The callee now narrows each float parameter in place
 * at entry.
 */

#include <poco/poco.h>

#include <math.h>
#include <stdio.h>

#ifndef POCO_FLOAT_PARAM_FIXTURE_DIR
#error "POCO_FLOAT_PARAM_FIXTURE_DIR must name the fixture directory"
#endif

static int failures;

static int check(int condition, const char* context)
{
	if (condition) {
		return 1;
	}
	fprintf(stderr, "poco_float_param: %s\n", context);
	++failures;
	return 0;
}

static int near(double actual, double expected)
{
	return fabs(actual - expected) < 1e-6;
}

/* Calls name with an optional leading int and optional double, and reports
 * the double the script returned. */
static int call_double(PocoActivation* activation, const char* name, int has_int, int int_arg,
					   int has_double, double double_arg, double* out_value)
{
	PocoCall* call = NULL;
	PocoCallbackValue result;
	int ok = 1;

	*out_value = 0.0;
	if (poco_call_begin(activation, name, &call) != POCO_STATUS_OK) {
		return 0;
	}
	if (has_int) {
		ok &= poco_call_push_int(call, int_arg) == POCO_STATUS_OK;
	}
	if (ok && has_double) {
		ok &= poco_call_push_double(call, double_arg) == POCO_STATUS_OK;
	}
	ok &= poco_call_invoke(call, &result) == POCO_STATUS_OK;
	if (ok) {
		if (result.kind == POCO_CALLBACK_VALUE_DOUBLE) {
			*out_value = result.value.double_value;
		} else {
			ok = 0;
		}
	}
	poco_call_end(call);
	return ok;
}

int main(void)
{
	PocoVm* vm = NULL;
	PocoProgram* program = NULL;
	PocoActivation* activation = NULL;
	char path[1024];
	double value = 0.0;
	int ok = 1;

	if (snprintf(path, sizeof(path), "%s/float_param.poc", POCO_FLOAT_PARAM_FIXTURE_DIR) >=
		(int)sizeof(path)) {
		return 1;
	}

	ok &= check(poco_vm_create(NULL, &vm) == POCO_STATUS_OK, "create VM");
	ok &= check(poco_vm_compile_file(vm, path, &program) == POCO_STATUS_OK, "compile fixture");
	ok &= check(poco_activation_acquire(program, &activation) == POCO_STATUS_OK,
				"acquire activation");
	ok &= check(poco_activation_init(activation) == POCO_STATUS_OK, "initialize activation");
	if (!ok) {
		return 1;
	}

	ok &= check(call_double(activation, "via_literal", 0, 0, 0, 0.0, &value) && near(value, 25.0),
				"a literal passed to a float parameter arrives intact");

	ok &= check(call_double(activation, "via_float_var", 0, 0, 0, 0.0, &value) &&
					near(value, (double)0.1f),
				"a float variable passed to a float parameter arrives intact");

	ok &= check(call_double(activation, "via_double_var", 0, 0, 0, 0.0, &value) &&
					near(value, (double)(float)(1.0 / 3.0)),
				"a double variable passed to a float parameter is narrowed, not truncated");

	ok &= check(call_double(activation, "via_mixed", 0, 0, 0, 0.0, &value) && near(value, 32.25),
				"float parameters interleaved with int and double keep their offsets");

	ok &= check(call_double(activation, "via_address", 0, 0, 0, 0.0, &value) && near(value, 3.5),
				"the address of a float parameter points at a float");

	ok &= check(call_double(activation, "via_reassign", 0, 0, 0, 0.0, &value) && near(value, 2.5),
				"assigning to a float parameter reads back correctly");

	ok &= check(call_double(activation, "host_echo", 0, 0, 1, 25.0, &value) && near(value, 25.0),
				"a host double pushed into a float parameter arrives intact");

	ok &= check(call_double(activation, "host_pair", 1, 7, 1, 0.1, &value) &&
					near(value, 7.0 + (double)0.1f),
				"a host float parameter after an int arrives intact");

	poco_activation_release(activation);
	poco_program_destroy(program);
	poco_vm_destroy(vm);
	return ok && failures == 0 ? 0 : 1;
}
