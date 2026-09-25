/*
 * Regression test for OP_FI_VAR's interpreter stack slot.
 *
 * Reading a float member through a struct pointer reserved sizeof(float) for
 * a slot that was written as a double and popped as a double, so each such
 * read left the frame four bytes short. Three reads in one function - reading
 * a whole three-float vector, which is what an embedder passing a vec3 by
 * pointer does - drifted the frame far enough that OP_RET fetched a garbage
 * return address and took the process with it.
 */

#include <poco/poco.h>

#include <math.h>
#include <stdio.h>

#ifndef POCO_INDIRECT_FLOAT_MEMBER_FIXTURE_DIR
#error "POCO_INDIRECT_FLOAT_MEMBER_FIXTURE_DIR must name the fixture directory"
#endif

typedef struct vec3_t {
	float x, y, z;
} vec3_t;

typedef struct mixed_t {
	int tag;
	float value;
	int trailer;
} mixed_t;

static int failures;

static int check(int condition, const char* context)
{
	if (condition) {
		return 1;
	}
	fprintf(stderr, "poco_indirect_float_member: %s\n", context);
	++failures;
	return 0;
}

static int near(double actual, double expected)
{
	return fabs(actual - expected) < 1e-6;
}

/* Calls name with a pointer to span, plus an optional trailing int, and
 * reports the double the script returned. */
static int call_double(PocoActivation* activation, const char* name, void* span, size_t size,
					   int has_extra, int extra, double* out_value)
{
	PocoCall* call = NULL;
	PocoCallbackValue result;
	int ok = 1;

	*out_value = 0.0;
	if (poco_call_begin(activation, name, &call) != POCO_STATUS_OK) {
		return 0;
	}
	ok &= poco_call_push_pointer(call, span, size, POCO_POINTER_PERMISSION_READ) == POCO_STATUS_OK;
	if (ok && has_extra) {
		ok &= poco_call_push_int(call, extra) == POCO_STATUS_OK;
	}
	ok &= poco_call_invoke(call, &result) == POCO_STATUS_OK;
	if (ok) {
		if (result.kind == POCO_CALLBACK_VALUE_DOUBLE) {
			*out_value = result.value.double_value;
		} else if (result.kind == POCO_CALLBACK_VALUE_INT) {
			*out_value = (double)result.value.int_value;
		} else if (result.kind == POCO_CALLBACK_VALUE_LONG) {
			*out_value = (double)result.value.long_value;
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
	vec3_t point;
	mixed_t mixed;
	char path[1024];
	double value = 0.0;
	int ok = 1;
	int i;

	point.x = 1.5f;
	point.y = -2.25f;
	point.z = 8.0f;

	mixed.tag = 7;
	mixed.value = 2.0f;
	mixed.trailer = 11;

	if (snprintf(path, sizeof(path), "%s/indirect_float_member.poc",
				 POCO_INDIRECT_FLOAT_MEMBER_FIXTURE_DIR) >= (int)sizeof(path)) {
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

	ok &= check(call_double(activation, "read_one", &point, sizeof(point), 0, 0, &value)
					&& near(value, 1.5),
				"one indirect float read returns the member's value");

	ok &= check(call_double(activation, "read_three", &point, sizeof(point), 0, 0, &value)
					&& near(value, 7.25),
				"three indirect float reads in one function return the right sum");

	ok &= check(call_double(activation, "read_three_via_locals", &point, sizeof(point), 0, 0,
							&value)
					&& near(value, 7.25),
				"three indirect float reads stored into locals return the right sum");

	ok &= check(call_double(activation, "read_nested", &point, sizeof(point), 0, 0, &value)
					&& near(value, 14.5),
				"a caller of two float-reading functions returns to its own caller intact");

	ok &= check(call_double(activation, "read_looped", &point, sizeof(point), 1, 64, &value)
					&& near(value, 7.25 * 64.0),
				"sixty-four loop iterations of indirect float reads do not drift the frame");

	ok &= check(call_double(activation, "read_mixed", &mixed, sizeof(mixed), 0, 0, &value)
					&& near(value, 20.0),
				"a float read between two int reads leaves the int members readable");

	/* Repeat the three-read call: a frame that drifts by a fixed amount each
	 * time only becomes fatal after enough calls, so one pass is not proof. */
	for (i = 0; i < 256; ++i) {
		if (!call_double(activation, "read_three", &point, sizeof(point), 0, 0, &value)
			|| !near(value, 7.25)) {
			ok &= check(0, "repeated three-read calls stay correct");
			break;
		}
	}

	poco_activation_release(activation);
	poco_program_destroy(program);
	poco_vm_destroy(vm);
	return ok && failures == 0 ? 0 : 1;
}
