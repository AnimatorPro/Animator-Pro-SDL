#include <poco/poco.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>

typedef struct vec3d {
	double x;
	double y;
	double z;
} Vec3d;

static int blend_calls;

static Vec3d blend(Vec3d left, Vec3d right)
{
	Vec3d result;

	++blend_calls;
	result.x = left.x + right.x;
	result.y = left.y - right.y;
	result.z = left.z * right.z;
	return result;
}

static int check(int condition, const char* message)
{
	if (condition)
		return 1;
	fprintf(stderr, "poco_ffi_struct_return: %s\n", message);
	return 0;
}

int main(void)
{
	static const PocoBinding bindings[] = {
		{
			"struct Vec3d { double x; double y; double z; } "
			"blend(struct Vec3d left, struct Vec3d right);",
			(PocoNativeFunction)blend,
		},
	};
	static const PocoLibrary library = {
		"ffi-struct-return",
		bindings,
		sizeof(bindings) / sizeof(bindings[0]),
	};
	static const char source[] =
		"main()\n"
		"{\n"
		"  struct Vec3d values[3];\n"
		"  struct Vec3d left;\n"
		"  struct Vec3d right;\n"
		"  values[0].x = -100.0; values[2].z = 200.0;\n"
		"  left.x = 1.25; left.y = 8.0; left.z = -2.0;\n"
		"  right.x = 3.75; right.y = 2.5; right.z = 4.0;\n"
		"  values[1] = blend(left, right);\n"
		"  if (values[1].x != 5.0 || values[1].y != 5.5 || values[1].z != -8.0)\n"
		"    return 10;\n"
		"  if (values[0].x != -100.0 || values[2].z != 200.0)\n"
		"    return 20;\n"
		"  values[2] = blend(left, right);\n"
		"  if (values[2].x != 5.0 || values[2].y != 5.5 || values[2].z != -8.0)\n"
		"    return 30;\n"
		"  return 0;\n"
		"}\n";
	static const char out_of_bounds_source[] =
		"main()\n"
		"{\n"
		"  struct Vec3d values[1];\n"
		"  struct Vec3d left;\n"
		"  struct Vec3d right;\n"
		"  values[1] = blend(left, right);\n"
		"  return 0;\n"
		"}\n";
	PocoVmOptions options = {0};
	PocoVm* vm = NULL;
	PocoProgram* program = NULL;
	int32_t result = -1;
	int ok = 1;

	ok &= check(poco_vm_create(&options, &vm) == POCO_STATUS_OK, "VM creation failed");
	if (vm != NULL)
		ok &= check(poco_vm_register_library(vm, &library) == POCO_STATUS_OK,
			"library registration failed");
	if (ok)
		ok &= check(poco_vm_compile_buffer(vm, "ffi-struct-return.poc", source,
			sizeof(source) - 1, &program) == POCO_STATUS_OK,
			poco_get_last_error(vm));
	if (program != NULL)
		ok &= check(poco_vm_run(vm, program, NULL, &result) == POCO_STATUS_OK,
			poco_get_last_error(vm));
	ok &= check(result == 0, "struct return was not assigned into the expected array element");
	ok &= check(blend_calls == 2, "native struct-return binding was not called twice");
	poco_program_destroy(program);
	program = NULL;
	if (ok)
		ok &= check(poco_vm_compile_buffer(vm, "ffi-struct-return-oob.poc",
			out_of_bounds_source, sizeof(out_of_bounds_source) - 1,
			&program) == POCO_STATUS_OK, poco_get_last_error(vm));
	if (program != NULL)
		ok &= check(poco_vm_run(vm, program, NULL, &result) ==
			POCO_STATUS_ERROR_FILE,
			"out-of-span struct destination was not rejected");
	ok &= check(blend_calls == 3,
		"out-of-span assignment did not stop after producing the native temp");
	poco_program_destroy(program);
	poco_vm_destroy(vm);
	return ok ? 0 : 1;
}
