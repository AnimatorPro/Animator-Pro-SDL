#include <poco/poco.h>

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define ARRAY_COUNT(values) (sizeof(values) / sizeof((values)[0]))
#define PARAMETER_NONE POCO_BINDING_PARAMETER_NONE

typedef struct vec3d {
	double x;
	double y;
	double z;
} Vec3d;

typedef struct quatd {
	double x;
	double y;
	double z;
	double w;
} Quatd;

typedef struct matrix4d {
	double values[16];
} Matrix4d;

static Vec3d vec_output;
static Vec3d read_only_vec_output;
static Quatd quat_output;
static Matrix4d matrix_output;
static int vec_transform_calls;
static int quat_transform_calls;
static int matrix_transform_calls;
static int vec_write_calls;
static int quat_write_calls;
static int matrix_write_calls;

static Vec3d vec_transform(Vec3d value)
{
	++vec_transform_calls;
	value.x += 10.0;
	value.y *= -2.0;
	value.z -= 0.5;
	return value;
}

static Quatd quat_transform(Quatd value)
{
	++quat_transform_calls;
	value.x = -value.x;
	value.y = -value.y;
	value.z = -value.z;
	return value;
}

static Matrix4d matrix_transform(Matrix4d value)
{
	size_t index;

	++matrix_transform_calls;
	for (index = 0; index < ARRAY_COUNT(value.values); ++index) {
		value.values[index] += (double)index;
	}
	return value;
}

static Vec3d* get_vec_output(void)
{
	return &vec_output;
}

static Quatd* get_quat_output(void)
{
	return &quat_output;
}

static Matrix4d* get_matrix_output(void)
{
	return &matrix_output;
}

static Vec3d* get_short_vec_output(void)
{
	return (Vec3d*)((unsigned char*)&vec_output + sizeof(vec_output) - sizeof(double));
}

static Vec3d* get_read_only_vec_output(void)
{
	return &read_only_vec_output;
}

static int write_vec(Vec3d* output, Vec3d value)
{
	++vec_write_calls;
	*output = value;
	return 1;
}

static int write_quat(Quatd* output, Quatd value)
{
	++quat_write_calls;
	*output = value;
	return 1;
}

static int write_matrix(Matrix4d* output, Matrix4d value)
{
	++matrix_write_calls;
	*output = value;
	return 1;
}

static int check(int condition, const char* message)
{
	if (condition) {
		return 1;
	}
	fprintf(stderr, "poco_ffi_aggregate_bindings: %s\n", message);
	return 0;
}

static int equal(double left, double right)
{
	return fabs(left - right) < 0.000001;
}

static const PocoBindingContract borrowed_write_contract = {
	.return_value =
		{
			.origin = POCO_POINTER_RETURN_BORROWED,
			.permissions = POCO_POINTER_PERMISSION_WRITE,
		},
};

#define WRITE_POINTER_CONTRACT(byte_size)             \
	{                                                 \
		.parameter_index = 0,                         \
		.permissions = POCO_POINTER_PERMISSION_WRITE, \
		.pointer_depth = 1,                           \
		.span_kind = POCO_BINDING_SPAN_BYTES,         \
		.byte_count = (byte_size),                    \
		.byte_count_parameter = PARAMETER_NONE,       \
		.element_count_parameter = PARAMETER_NONE,    \
		.string_parameter = PARAMETER_NONE,           \
	}

static const PocoBindingPointerContract vec_write_pointer[] = {
	WRITE_POINTER_CONTRACT(sizeof(Vec3d)),
};

static const PocoBindingPointerContract quat_write_pointer[] = {
	WRITE_POINTER_CONTRACT(sizeof(Quatd)),
};

static const PocoBindingPointerContract matrix_write_pointer[] = {
	WRITE_POINTER_CONTRACT(sizeof(Matrix4d)),
};

static const PocoBindingContract vec_write_contract = {
	.pointer_contracts = vec_write_pointer,
	.pointer_contract_count = ARRAY_COUNT(vec_write_pointer),
};

static const PocoBindingContract quat_write_contract = {
	.pointer_contracts = quat_write_pointer,
	.pointer_contract_count = ARRAY_COUNT(quat_write_pointer),
};

static const PocoBindingContract matrix_write_contract = {
	.pointer_contracts = matrix_write_pointer,
	.pointer_contract_count = ARRAY_COUNT(matrix_write_pointer),
};

static const PocoBinding bindings[] = {
	{
		"struct Vec3d { double x; double y; double z; } "
		"vec_transform(struct Vec3d value);",
		(PocoNativeFunction)vec_transform,
	},
	{"struct Vec3d *get_vec_output();", (PocoNativeFunction)get_vec_output,
	 &borrowed_write_contract},
	{"struct Vec3d *get_short_vec_output();", (PocoNativeFunction)get_short_vec_output,
	 &borrowed_write_contract},
	{"struct Vec3d *get_read_only_vec_output();", (PocoNativeFunction)get_read_only_vec_output,
	 &borrowed_write_contract},
	{"int write_vec(struct Vec3d *output, struct Vec3d value);", (PocoNativeFunction)write_vec,
	 &vec_write_contract},
	{
		"struct Quatd { double x; double y; double z; double w; } "
		"quat_transform(struct Quatd value);",
		(PocoNativeFunction)quat_transform,
	},
	{"struct Quatd *get_quat_output();", (PocoNativeFunction)get_quat_output,
	 &borrowed_write_contract},
	{"int write_quat(struct Quatd *output, struct Quatd value);", (PocoNativeFunction)write_quat,
	 &quat_write_contract},
	{
		"struct Matrix4d { double values[16]; } "
		"matrix_transform(struct Matrix4d value);",
		(PocoNativeFunction)matrix_transform,
	},
	{"struct Matrix4d *get_matrix_output();", (PocoNativeFunction)get_matrix_output,
	 &borrowed_write_contract},
	{"int write_matrix(struct Matrix4d *output, struct Matrix4d value);",
	 (PocoNativeFunction)write_matrix, &matrix_write_contract},
};

static const PocoLibrary library = {
	"ffi-aggregate-bindings",
	bindings,
	ARRAY_COUNT(bindings),
};

static const char success_source[] =
	"main()\n"
	"{\n"
	"  struct Vec3d vector; struct Vec3d vector_result;\n"
	"  struct Quatd quaternion; struct Quatd quaternion_result;\n"
	"  struct Matrix4d matrix; struct Matrix4d matrix_result;\n"
	"  struct Vec3d *vector_output; struct Quatd *quaternion_output;\n"
	"  struct Matrix4d *matrix_output;\n"
	"  int i;\n"
	"  vector.x = 1.5; vector.y = -2.0; vector.z = 4.25;\n"
	"  quaternion.x = 1.0; quaternion.y = -2.0;\n"
	"  quaternion.z = 3.0; quaternion.w = 4.0;\n"
	"  i = 0; while (i < 16) { matrix.values[i] = i * 2.0; ++i; }\n"
	"  vector_result = vec_transform(vector);\n"
	"  quaternion_result = quat_transform(quaternion);\n"
	"  matrix_result = matrix_transform(matrix);\n"
	"  vector_output = get_vec_output();\n"
	"  quaternion_output = get_quat_output();\n"
	"  matrix_output = get_matrix_output();\n"
	"  if (!write_vec(vector_output, vector_result)) return 10;\n"
	"  if (!write_quat(quaternion_output, quaternion_result)) return 20;\n"
	"  if (!write_matrix(matrix_output, matrix_result)) return 30;\n"
	"  return 0;\n"
	"}\n";

static const char out_of_span_source[] =
	"main()\n"
	"{\n"
	"  struct Vec3d vector; struct Vec3d vector_result;\n"
	"  struct Vec3d *output;\n"
	"  vector.x = 1.0; vector.y = 2.0; vector.z = 3.0;\n"
	"  vector_result = vec_transform(vector);\n"
	"  output = get_short_vec_output();\n"
	"  write_vec(output, vector_result);\n"
	"  return 0;\n"
	"}\n";

static const char read_only_source[] =
	"main()\n"
	"{\n"
	"  struct Vec3d vector; struct Vec3d vector_result;\n"
	"  struct Vec3d *output;\n"
	"  vector.x = 1.0; vector.y = 2.0; vector.z = 3.0;\n"
	"  vector_result = vec_transform(vector);\n"
	"  output = get_read_only_vec_output();\n"
	"  write_vec(output, vector_result);\n"
	"  return 0;\n"
	"}\n";

static int run_source(PocoVm* vm, const char* source_name, const char* source, size_t source_length,
					  PocoStatus expected_status, int32_t* out_result)
{
	PocoProgram* program = NULL;
	PocoStatus status;

	status = poco_vm_compile_buffer(vm, source_name, source, source_length, &program);
	if (status == POCO_STATUS_OK) {
		status = poco_vm_run(vm, program, NULL, out_result);
	}
	if (status != expected_status) {
		fprintf(stderr, "poco_ffi_aggregate_bindings: %s returned %d (expected %d): %s\n",
				source_name, status, expected_status, poco_get_last_error(vm));
	}
	poco_program_destroy(program);
	return status == expected_status;
}

int main(void)
{
	PocoVm* vm = NULL;
	PocoVmOptions options = {0};
	int32_t result = -1;
	int writes_before_rejection;
	int ok = 1;
	size_t index;

	ok &= check(poco_vm_create(&options, &vm) == POCO_STATUS_OK, "VM creation failed");
	if (vm != NULL) {
		ok &= check(poco_vm_register_library(vm, &library) == POCO_STATUS_OK,
					"aggregate library registration failed");
	}
	if (ok) {
		ok &= check(poco_vm_register_borrowed_span(vm, &vec_output, sizeof(vec_output),
												   POCO_POINTER_PERMISSION_WRITE) == POCO_STATUS_OK,
					"vec3d borrowed span registration failed");
		ok &= check(
			poco_vm_register_borrowed_span(vm, &read_only_vec_output, sizeof(read_only_vec_output),
										   POCO_POINTER_PERMISSION_READ) == POCO_STATUS_OK,
			"read-only vec3d borrowed span registration failed");
		ok &= check(poco_vm_register_borrowed_span(vm, &quat_output, sizeof(quat_output),
												   POCO_POINTER_PERMISSION_WRITE) == POCO_STATUS_OK,
					"quatd borrowed span registration failed");
		ok &= check(poco_vm_register_borrowed_span(vm, &matrix_output, sizeof(matrix_output),
												   POCO_POINTER_PERMISSION_WRITE) == POCO_STATUS_OK,
					"matrix borrowed span registration failed");
	}
	if (ok) {
		ok &= run_source(vm, "ffi-aggregate-success.poc", success_source,
						 sizeof(success_source) - 1, POCO_STATUS_OK, &result);
	}
	ok &= check(result == 0, "aggregate success script returned the wrong result");
	ok &=
		check(vec_transform_calls == 1 && quat_transform_calls == 1 && matrix_transform_calls == 1,
			  "direct aggregate functions were not called once each");
	ok &= check(vec_write_calls == 1 && quat_write_calls == 1 && matrix_write_calls == 1,
				"aggregate outputs were not written once each");
	ok &= check(equal(vec_output.x, 11.5) && equal(vec_output.y, 4.0) && equal(vec_output.z, 3.75),
				"vec3d did not round-trip through the borrowed span");
	ok &= check(equal(quat_output.x, -1.0) && equal(quat_output.y, 2.0) &&
					equal(quat_output.z, -3.0) && equal(quat_output.w, 4.0),
				"quatd did not round-trip through the borrowed span");
	for (index = 0; index < ARRAY_COUNT(matrix_output.values); ++index) {
		ok &= check(equal(matrix_output.values[index], (double)index * 3.0),
					"matrix did not round-trip through the borrowed span");
	}
	writes_before_rejection = vec_write_calls;
	if (ok) {
		ok &= run_source(vm, "ffi-aggregate-oob.poc", out_of_span_source,
						 sizeof(out_of_span_source) - 1, POCO_STATUS_FFI_BOUNDS, NULL);
	}
	ok &= check(vec_write_calls == writes_before_rejection,
				"out-of-span aggregate write reached native code");
	if (ok) {
		ok &= run_source(vm, "ffi-aggregate-read-only.poc", read_only_source,
						 sizeof(read_only_source) - 1, POCO_STATUS_FFI_BOUNDS, NULL);
	}
	ok &= check(vec_write_calls == writes_before_rejection,
				"read-only aggregate write reached native code");
	poco_vm_destroy(vm);
	return ok ? 0 : 1;
}
