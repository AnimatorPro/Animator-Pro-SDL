#include <poco/poco.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>

#define ARRAY_COUNT(values) (sizeof(values) / sizeof((values)[0]))

static float received_value;
static float probe_value;
static int receive_calls;
static int probe_calls;
static int return_calls;

static float add_half(float value)
{
	received_value = value;
	++receive_calls;
	return value + 0.5f;
}

/* 0.2 has no exact float or double representation, and the two nearest values
 * differ.  Narrowing the double argument to a 4-byte float must therefore lose
 * precision that a mistaken double-width marshal would preserve. */
static float narrow_probe(float value)
{
	probe_value = value;
	++probe_calls;
	return value;
}

static float exact_fraction(void)
{
	++return_calls;
	return 3.125f;
}

static const PocoBinding bindings[] = {
	{"float add_half(float value);", (PocoNativeFunction)add_half},
	{"float narrow_probe(float value);", (PocoNativeFunction)narrow_probe},
	{"float exact_fraction();", (PocoNativeFunction)exact_fraction},
};

static const PocoLibrary library = {
	"ffi-float-marshaling",
	bindings,
	ARRAY_COUNT(bindings),
};

static const char source[] =
	"main()\n"
	"{\n"
	"  double value;\n"
	"  value = add_half(-17.625);\n"
	"  if (value != -17.125) return 10;\n"
	"  value = exact_fraction();\n"
	"  if (value != 3.125) return 20;\n"
	"  value = narrow_probe(0.2);\n"
	"  if (value == 0.2) return 30;\n"
	"  return 0;\n"
	"}\n";

static int check(int condition, const char* message)
{
	if (condition) {
		return 1;
	}
	fprintf(stderr, "poco_ffi_float_marshaling: %s\n", message);
	return 0;
}

int main(void)
{
	PocoVm* vm = NULL;
	PocoProgram* program = NULL;
	PocoVmOptions options = {0};
	PocoStatus status;
	int32_t result = -1;
	int ok = 1;

	status = poco_vm_create(&options, &vm);
	ok &= check(status == POCO_STATUS_OK, "VM creation failed");
	if (ok) {
		status = poco_vm_register_library(vm, &library);
		ok &= check(status == POCO_STATUS_OK, "float binding registration failed");
	}
	if (ok) {
		status = poco_vm_compile_buffer(vm, "ffi-float-marshaling.poc", source,
										sizeof(source) - 1, &program);
		ok &= check(status == POCO_STATUS_OK, poco_get_last_error(vm));
	}
	if (ok) {
		status = poco_vm_run(vm, program, NULL, &result);
		ok &= check(status == POCO_STATUS_OK, poco_get_last_error(vm));
	}
	ok &= check(result == 0, "float return did not widen into the double value model");
	ok &= check(receive_calls == 1, "float parameter binding was not called exactly once");
	ok &= check(received_value == -17.625f,
				"float parameter did not arrive as its exact 4-byte value");
	ok &= check(return_calls == 1, "float return binding was not called exactly once");
	ok &= check(probe_calls == 1, "narrowing probe was not called exactly once");
	ok &= check(probe_value == (float)0.2,
				"narrowing probe did not receive the exact float-rounded 0.2");
	ok &= check((double)probe_value != 0.2,
				"narrowing probe kept full double precision across the float boundary");

	poco_program_destroy(program);
	poco_vm_destroy(vm);
	return ok ? 0 : 1;
}
