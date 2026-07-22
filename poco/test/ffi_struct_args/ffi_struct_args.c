#include <poco/poco.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "poco.h"

typedef struct vec3d {
	double x;
	double y;
	double z;
} Vec3d;

typedef struct payload {
	Vec3d position;
	int tag;
} Payload;

static int vec_calls;
static int payload_calls;
static int short_bounds_calls;

static int inspect_vec(Vec3d value, int bias)
{
	++vec_calls;
	return fabs(value.x - 1.25) < 0.000001 &&
		fabs(value.y + 2.5) < 0.000001 &&
		fabs(value.z - 4.0) < 0.000001 && bias == 7 ? 100 : -1000;
}

static int inspect_payload(Payload value)
{
	++payload_calls;
	return fabs(value.position.x - 8.5) < 0.000001 &&
		fabs(value.position.y - 9.5) < 0.000001 &&
		fabs(value.position.z - 10.5) < 0.000001 && value.tag == 13 ? 200 : -2000;
}

static int add_scalar(int left, int right)
{
	return left + right;
}

static int reject_if_called(Vec3d value)
{
	(void)value;
	++short_bounds_calls;
	return 1;
}

static int check(int condition, const char* message)
{
	if (condition)
		return 1;
	fprintf(stderr, "poco_ffi_struct_args: %s\n", message);
	return 0;
}

static int run_public_binding_test(void)
{
	static const PocoBinding bindings[] = {
		{
			"int inspect_vec(struct Vec3dArg { double x; double y; double z; } value, int bias);",
			(PocoNativeFunction)inspect_vec,
		},
		{"int add_scalar(int left, int right);", (PocoNativeFunction)add_scalar},
	};
	static const PocoLibrary library = {
		"ffi-struct-args",
		bindings,
		sizeof(bindings) / sizeof(bindings[0]),
	};
	static const char source[] =
		"main()\n"
		"{\n"
		"  struct Vec3dArg vector;\n"
		"  vector.x = 1.25; vector.y = -2.5; vector.z = 4.0;\n"
		"  return inspect_vec(vector, 7) + add_scalar(2, 3);\n"
		"}\n";
	PocoVm* vm = NULL;
	PocoProgram* program = NULL;
	PocoVmOptions options = {0};
	int32_t result = 0;
	int ok = 1;

	ok &= check(poco_vm_create(&options, &vm) == POCO_STATUS_OK, "VM creation failed");
	if (vm != NULL)
		ok &= check(poco_vm_register_library(vm, &library) == POCO_STATUS_OK,
			"library registration failed");
	if (ok)
		ok &= check(poco_vm_compile_buffer(vm, "ffi-struct-args.poc", source,
			sizeof(source) - 1, &program) == POCO_STATUS_OK,
			poco_get_last_error(vm));
	if (program != NULL)
		ok &= check(poco_vm_run(vm, program, NULL, &result) == POCO_STATUS_OK,
			poco_get_last_error(vm));
	ok &= check(result == 105, "struct or scalar binding returned the wrong result");
	ok &= check(vec_calls == 1, "struct binding was not called exactly once");
	poco_program_destroy(program);
	poco_vm_destroy(vm);
	return ok;
}

static int run_mismatched_struct_test(void)
{
	static const PocoBinding bindings[] = {
		{
			"int inspect_vec(struct Vec3dArg { double x; double y; double z; } value, int bias);",
			(PocoNativeFunction)inspect_vec,
		},
	};
	static const PocoLibrary library = {
		"ffi-struct-args-mismatch",
		bindings,
		sizeof(bindings) / sizeof(bindings[0]),
	};
	static const char source[] =
		"struct ImpostorVec { double x; double y; double z; };\n"
		"main()\n"
		"{\n"
		"  struct ImpostorVec vector;\n"
		"  vector.x = 1.25; vector.y = -2.5; vector.z = 4.0;\n"
		"  return inspect_vec(vector, 7);\n"
		"}\n";
	PocoVm* vm = NULL;
	PocoProgram* program = NULL;
	PocoVmOptions options = {0};
	PocoStatus status;
	int calls_before = vec_calls;
	int ok = 1;

	ok &= check(poco_vm_create(&options, &vm) == POCO_STATUS_OK,
		"mismatch VM creation failed");
	if (vm != NULL)
		ok &= check(poco_vm_register_library(vm, &library) == POCO_STATUS_OK,
			"mismatch library registration failed");
	status = ok ? poco_vm_compile_buffer(vm, "ffi-struct-args-mismatch.poc", source,
		sizeof(source) - 1, &program) : POCO_STATUS_INTERNAL_ERROR;
	ok &= check(status == POCO_STATUS_REPORTED,
		"different struct tag was accepted by the binding");
	ok &= check(program == NULL, "mismatched struct produced a program");
	ok &= check(vec_calls == calls_before, "mismatched struct reached the native function");
	poco_program_destroy(program);
	poco_vm_destroy(vm);
	return ok;
}

static void init_scalar_type(Type_info* type_info, TypeComp* component,
	Pt_long* dimension, TypeComp type)
{
	*component = type;
	dimension->pt = NULL;
	type_info->comp = component;
	type_info->sdims = dimension;
	type_info->comp_alloc = 1;
	type_info->comp_count = 1;
	po_set_ido_type(type_info);
}

static void init_struct_type(Type_info* type_info, TypeComp* component,
	Pt_long* dimension, Struct_info* struct_info)
{
	init_scalar_type(type_info, component, dimension, TYPE_STRUCT);
	dimension->pt = struct_info;
}

static int run_nested_struct_test(void)
{
	TypeComp vec_components[3];
	Pt_long vec_dimensions[3] = {0};
	Type_info vec_types[3] = {0};
	Symbol vec_members[3] = {0};
	Struct_info vec_info = {0};
	TypeComp payload_components[2];
	Pt_long payload_dimensions[2] = {0};
	Type_info payload_types[2] = {0};
	Symbol payload_members[2] = {0};
	Struct_info payload_info = {0};
	TypeComp parameter_component;
	Pt_long parameter_dimension = {0};
	Type_info parameter_type = {0};
	TypeComp int_component;
	Pt_long int_dimension = {0};
	Type_info int_type = {0};
	Symbol parameter = {0};
	C_frame frame = {0};
	Po_FFI* binding;
	Payload value = {{8.5, 9.5, 10.5}, 13};
	union {
		double alignment;
		Popot pointer;
	} stack = {0};
	Pt_num result;
	int index;

	for (index = 0; index < 3; ++index) {
		init_scalar_type(&vec_types[index], &vec_components[index],
			&vec_dimensions[index], TYPE_DOUBLE);
		vec_members[index].ti = &vec_types[index];
		vec_members[index].next = index != 2 ? &vec_members[index + 1] : NULL;
	}
	vec_info.elements = vec_members;
	vec_info.size = sizeof(Vec3d);
	vec_info.el_count = 3;
	vec_info.type = TYPE_STRUCT;
	init_struct_type(&payload_types[0], &payload_components[0],
		&payload_dimensions[0], &vec_info);
	init_scalar_type(&payload_types[1], &payload_components[1],
		&payload_dimensions[1], TYPE_INT);
	payload_members[0].ti = &payload_types[0];
	payload_members[0].next = &payload_members[1];
	payload_members[1].ti = &payload_types[1];
	payload_info.elements = payload_members;
	payload_info.size = sizeof(Payload);
	payload_info.el_count = 2;
	payload_info.type = TYPE_STRUCT;
	init_struct_type(&parameter_type, &parameter_component, &parameter_dimension,
		&payload_info);
	init_scalar_type(&int_type, &int_component, &int_dimension, TYPE_INT);
	parameter.ti = &parameter_type;
	frame.name = "inspect_payload";
	frame.code_pt = (void*)inspect_payload;
	frame.type = CFF_C;
	frame.return_type = &int_type;
	frame.parameters = &parameter;
	frame.pcount = 1;
	binding = po_ffi_new(&frame);
	if (!check(binding != NULL, "nested struct descriptor did not build"))
		return 0;
	stack.pointer.pt = &value;
	stack.pointer.min = &value;
	stack.pointer.max = (char*)&value + sizeof(value) - 1;
	result = po_ffi_call(binding, (const Pt_num*)&stack, NULL, NULL);
	po_ffi_delete(binding);
	return check(result.i == 200 && payload_calls == 1,
		"nested struct members were not delivered by value");
}

static int run_short_bounds_test(void)
{
	TypeComp member_components[3];
	Pt_long member_dimensions[3] = {0};
	Type_info member_types[3] = {0};
	Symbol members[3] = {0};
	TypeComp struct_component = TYPE_STRUCT;
	Pt_long struct_dimension = {0};
	Type_info struct_type = {0};
	TypeComp int_component = TYPE_INT;
	Pt_long int_dimension = {0};
	Type_info int_type = {0};
	Struct_info struct_info = {0};
	Symbol parameter = {0};
	C_frame frame = {0};
	Po_FFI* binding;
	Vec3d value = {1.0, 2.0, 3.0};
	union {
		double alignment;
		Popot pointer;
	} stack = {0};
	int index;

	for (index = 0; index < 3; ++index) {
		init_scalar_type(&member_types[index], &member_components[index],
			&member_dimensions[index], TYPE_DOUBLE);
		members[index].ti = &member_types[index];
		members[index].next = index != 2 ? &members[index + 1] : NULL;
	}
	struct_info.elements = members;
	struct_info.size = sizeof(value);
	struct_info.el_count = 3;
	struct_info.type = TYPE_STRUCT;
	struct_dimension.pt = &struct_info;
	struct_type.comp = &struct_component;
	struct_type.sdims = &struct_dimension;
	struct_type.comp_alloc = 1;
	struct_type.comp_count = 1;
	po_set_ido_type(&struct_type);
	init_scalar_type(&int_type, &int_component, &int_dimension, TYPE_INT);
	parameter.ti = &struct_type;
	frame.name = "reject_if_called";
	frame.code_pt = (void*)reject_if_called;
	frame.type = CFF_C;
	frame.return_type = &int_type;
	frame.parameters = &parameter;
	frame.pcount = 1;
	binding = po_ffi_new(&frame);
	if (!check(binding != NULL, "short-bounds descriptor did not build"))
		return 0;
	stack.pointer.pt = &value;
	stack.pointer.min = &value;
	stack.pointer.max = (char*)&value + sizeof(double) - 1;
	(void)po_ffi_call(binding, (const Pt_num*)&stack, NULL, NULL);
	po_ffi_delete(binding);
	return check(short_bounds_calls == 0,
		"undersized struct bounds reached the native function");
}

int main(void)
{
	return run_public_binding_test() && run_mismatched_struct_test() &&
		run_nested_struct_test() &&
		run_short_bounds_test() ? 0 : 1;
}
