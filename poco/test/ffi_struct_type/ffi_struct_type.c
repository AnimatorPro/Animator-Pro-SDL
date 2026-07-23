#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include "poco.h"

#define ARRAY_COUNT(values) (sizeof(values) / sizeof((values)[0]))
#define NATIVE_ALIGNMENT(type) \
	offsetof(                  \
		struct {               \
			char prefix;       \
			type value;        \
		},                     \
		value)

typedef struct vec3d {
	double x;
	double y;
	double z;
} Vec3d;

typedef struct nested_value {
	Vec3d position;
	int id;
} NestedValue;

typedef struct scalar_value {
	char character;
	unsigned char unsigned_character;
	short short_integer;
	unsigned short unsigned_short_integer;
	int integer;
	unsigned int unsigned_integer;
	long long_integer;
	unsigned long unsigned_long_integer;
	float single;
	double real;
	void* pointer;
} ScalarValue;

static void init_type(Type_info* type_info, TypeComp* component, Pt_long* dimension, TypeComp type,
					  void* struct_info)
{
	*component = type;
	dimension->pt = struct_info;
	type_info->comp = component;
	type_info->sdims = dimension;
	type_info->comp_alloc = 1;
	type_info->comp_count = 1;
}

static void init_members(Symbol* symbols, Type_info* types, TypeComp* components,
						 Pt_long* dimensions, const TypeComp* member_types, size_t count)
{
	size_t index;

	for (index = 0; index < count; ++index) {
		init_type(&types[index], &components[index], &dimensions[index], member_types[index], NULL);
		symbols[index].ti = &types[index];
		symbols[index].next = index + 1 < count ? &symbols[index + 1] : NULL;
	}
}

static int prepare_struct_type(ffi_type* struct_type)
{
	ffi_cif cif;
	ffi_type* argument_types[] = {struct_type};

	return ffi_prep_cif(&cif, FFI_DEFAULT_ABI, 1, &ffi_type_void, argument_types) == FFI_OK;
}

static int check_vec3d_and_nested(void)
{
	const TypeComp vec_member_types[] = {TYPE_DOUBLE, TYPE_DOUBLE, TYPE_DOUBLE};
	Symbol vec_symbols[ARRAY_COUNT(vec_member_types)] = {0};
	Type_info vec_types[ARRAY_COUNT(vec_member_types)] = {0};
	TypeComp vec_components[ARRAY_COUNT(vec_member_types)] = {0};
	Pt_long vec_dimensions[ARRAY_COUNT(vec_member_types)] = {0};
	Struct_info vec_info = {0};
	const TypeComp nested_member_types[] = {TYPE_STRUCT, TYPE_INT};
	Symbol nested_symbols[ARRAY_COUNT(nested_member_types)] = {0};
	Type_info nested_types[ARRAY_COUNT(nested_member_types)] = {0};
	TypeComp nested_components[ARRAY_COUNT(nested_member_types)] = {0};
	Pt_long nested_dimensions[ARRAY_COUNT(nested_member_types)] = {0};
	Struct_info nested_info = {0};
	Po_FFI* binding = calloc(1, sizeof(*binding));
	ffi_type* ffi_nested = NULL;
	ffi_type* ffi_vec;
	int passed = 0;

	if (binding == NULL) {
		return 0;
	}
	init_members(vec_symbols, vec_types, vec_components, vec_dimensions, vec_member_types,
				 ARRAY_COUNT(vec_member_types));
	vec_info.elements = vec_symbols;
	vec_info.size = sizeof(Vec3d);
	vec_info.el_count = (SHORT)ARRAY_COUNT(vec_member_types);
	vec_info.type = TYPE_STRUCT;

	init_members(nested_symbols, nested_types, nested_components, nested_dimensions,
				 nested_member_types, ARRAY_COUNT(nested_member_types));
	nested_dimensions[0].pt = &vec_info;
	nested_info.elements = nested_symbols;
	nested_info.size = sizeof(NestedValue);
	nested_info.el_count = (SHORT)ARRAY_COUNT(nested_member_types);
	nested_info.type = TYPE_STRUCT;

	if (po_ffi_type_from_struct_info(binding, &nested_info, &ffi_nested) < Success ||
		ffi_nested == NULL || !prepare_struct_type(ffi_nested)) {
		goto done;
	}
	ffi_vec = ffi_nested->elements[0];
	passed = ffi_nested->type == FFI_TYPE_STRUCT && ffi_nested->size == sizeof(NestedValue) &&
			 ffi_nested->alignment == NATIVE_ALIGNMENT(NestedValue) && ffi_vec != NULL &&
			 ffi_vec->type == FFI_TYPE_STRUCT && ffi_vec->size == sizeof(Vec3d) &&
			 ffi_vec->alignment == NATIVE_ALIGNMENT(Vec3d) &&
			 ffi_vec->elements[0] == &ffi_type_double && ffi_vec->elements[1] == &ffi_type_double &&
			 ffi_vec->elements[2] == &ffi_type_double && ffi_vec->elements[3] == NULL &&
			 ffi_nested->elements[1] == &ffi_type_sint && ffi_nested->elements[2] == NULL;

done:
	po_ffi_delete(binding);
	return passed;
}

static int check_scalar_mapping(void)
{
	const TypeComp member_types[] = {TYPE_CHAR,  TYPE_UCHAR,  TYPE_SHORT,  TYPE_USHORT,
									 TYPE_INT,   TYPE_UINT,   TYPE_LONG,   TYPE_ULONG,
									 TYPE_FLOAT, TYPE_DOUBLE, TYPE_POINTER};
	ffi_type* expected_types[] = {&ffi_type_schar,  &ffi_type_uchar,  &ffi_type_sshort,
								  &ffi_type_ushort, &ffi_type_sint,   &ffi_type_uint,
								  &ffi_type_slong,  &ffi_type_ulong,  &ffi_type_float,
								  &ffi_type_double, &ffi_type_pointer};
	Symbol symbols[ARRAY_COUNT(member_types)] = {0};
	Type_info types[ARRAY_COUNT(member_types)] = {0};
	TypeComp components[ARRAY_COUNT(member_types)] = {0};
	Pt_long dimensions[ARRAY_COUNT(member_types)] = {0};
	Struct_info struct_info = {0};
	Po_FFI* binding = calloc(1, sizeof(*binding));
	ffi_type* ffi_struct = NULL;
	size_t index;
	int passed = 0;

	if (binding == NULL) {
		return 0;
	}
	init_members(symbols, types, components, dimensions, member_types, ARRAY_COUNT(member_types));
	struct_info.elements = symbols;
	struct_info.size = sizeof(ScalarValue);
	struct_info.el_count = (SHORT)ARRAY_COUNT(member_types);
	struct_info.type = TYPE_STRUCT;
	if (po_ffi_type_from_struct_info(binding, &struct_info, &ffi_struct) < Success ||
		ffi_struct == NULL || !prepare_struct_type(ffi_struct) ||
		ffi_struct->size != sizeof(ScalarValue) ||
		ffi_struct->alignment != NATIVE_ALIGNMENT(ScalarValue)) {
		goto done;
	}
	for (index = 0; index < ARRAY_COUNT(expected_types); ++index) {
		if (ffi_struct->elements[index] != expected_types[index]) {
			goto done;
		}
	}
	passed = ffi_struct->elements[ARRAY_COUNT(expected_types)] == NULL;

done:
	po_ffi_delete(binding);
	return passed;
}

static int check_malformed_layout_rejected(void)
{
	Struct_info struct_info = {0};
	Po_FFI* binding = calloc(1, sizeof(*binding));
	ffi_type* ffi_struct = (ffi_type*)&ffi_type_void;
	int passed;

	if (binding == NULL) {
		return 0;
	}
	struct_info.type = TYPE_STRUCT;
	struct_info.el_count = 1;
	passed = po_ffi_type_from_struct_info(binding, &struct_info, &ffi_struct) ==
				 Err_poco_ffi_invalid_binding &&
			 ffi_struct == NULL;
	po_ffi_delete(binding);
	return passed;
}

int main(void)
{
	if (!check_vec3d_and_nested()) {
		fprintf(stderr, "nested struct ffi_type did not match native layout\n");
		return 1;
	}
	if (!check_scalar_mapping()) {
		fprintf(stderr, "scalar struct ffi_type mapping did not match native layout\n");
		return 1;
	}
	if (!check_malformed_layout_rejected()) {
		fprintf(stderr, "malformed struct metadata was not rejected\n");
		return 1;
	}
	return 0;
}
