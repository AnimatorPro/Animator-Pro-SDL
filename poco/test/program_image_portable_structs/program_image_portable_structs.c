#include "program_image.h"
#include "program_internal.h"

#include "poco_test_bytes.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int check(int condition, const char* message)
{
	if (condition) {
		return 1;
	}
	fprintf(stderr, "portable structs: %s\n", message);
	return 0;
}

static const Struct_info* find_struct(const PocoProgram* program, const char* name)
{
	const Poco_run_env* executable = program->executable;
	const Struct_info* struct_info;
	for (struct_info = executable->struct_infos; struct_info != NULL;
		 struct_info = struct_info->next) {
		if (strcmp(struct_info->name, name) == 0) {
			return struct_info;
		}
	}
	return NULL;
}

static int same_type(const Type_info* left, const Type_info* right)
{
	unsigned int index;
	if (left == NULL || right == NULL || left->comp_count != right->comp_count ||
		left->flags != right->flags || left->ido_type != right->ido_type) {
		return 0;
	}
	for (index = 0; index < left->comp_count; ++index) {
		if (left->comp[index] != right->comp[index]) {
			return 0;
		}
		if (left->comp[index] == TYPE_STRUCT) {
			const Struct_info* left_struct = left->sdims[index].pt;
			const Struct_info* right_struct = right->sdims[index].pt;
			if (left_struct == NULL || right_struct == NULL ||
				strcmp(left_struct->name, right_struct->name) != 0) {
				return 0;
			}
		} else if (left->comp[index] != TYPE_FUNCTION &&
				   left->sdims[index].l != right->sdims[index].l) {
			return 0;
		}
	}
	return 1;
}

static int same_struct(const Struct_info* left, const Struct_info* right)
{
	const Symbol* left_member;
	const Symbol* right_member;
	if (left == NULL || right == NULL || left->type != right->type || left->size != right->size ||
		left->el_count != right->el_count) {
		return 0;
	}
	left_member = left->elements;
	right_member = right->elements;
	while (left_member != NULL && right_member != NULL) {
		if (strcmp(left_member->name, right_member->name) != 0 ||
			left_member->symval.doff != right_member->symval.doff ||
			!same_type(left_member->ti, right_member->ti)) {
			return 0;
		}
		left_member = left_member->next;
		right_member = right_member->next;
	}
	return left_member == NULL && right_member == NULL;
}

static int locate_first_member_component(const uint8_t* section, size_t size, size_t* out_offset)
{
	size_t offset = 0;
	uint32_t count;
	uint32_t index;
	uint32_t total_members = 0;

	if (size < 4) {
		return 0;
	}
	count = read_u32(section);
	offset = 4;
	for (index = 0; index < count; ++index) {
		uint32_t name_length;
		uint32_t member_count;
		if (offset > size || size - offset < 8) {
			return 0;
		}
		offset += 4;
		name_length = read_u32(section + offset);
		offset += 4;
		if (offset > size || name_length > size - offset || size - offset - name_length < 4) {
			return 0;
		}
		offset += name_length;
		member_count = read_u32(section + offset);
		offset += 4;
		if (UINT32_MAX - total_members < member_count) {
			return 0;
		}
		total_members += member_count;
	}
	if (total_members == 0 || offset > size || size - offset < 4) {
		return 0;
	}
	{
		uint32_t member_name_length = read_u32(section + offset);
		offset += 4;
		if (offset > size || member_name_length > size - offset ||
			size - offset - member_name_length < 20) {
			return 0;
		}
		offset += member_name_length;
		if (read_u32(section + offset) != 1 || read_u32(section + offset + 4) == 0) {
			return 0;
		}
		*out_offset = offset + 16;
		return 1;
	}
}

int main(void)
{
	static const char source[] =
		"struct Inner { char tag; long value; };\n"
		"struct Outer { struct Inner inner; double values[2]; int count; };\n"
		"int main(void) { struct Outer out; out.inner.tag = 2; out.inner.value = 7; "
		"out.values[0] = 3.0; out.count = 5; return out.inner.tag + out.inner.value + "
		"out.values[0] + out.count; }\n";
	PocoVm* vm = NULL;
	PocoProgram* compiled = NULL;
	PocoProgram* decoded = NULL;
	PocoProgram* rejected = NULL;
	uint8_t* image = NULL;
	uint8_t* malformed = NULL;
	size_t image_size = 0;
	uint64_t struct_offset;
	uint64_t struct_size;
	size_t component_offset = SIZE_MAX;
	int32_t result = 0;
	int ok = 1;
	PocoStatus compile_status;

	ok &= check(poco_vm_create(NULL, &vm) == POCO_STATUS_OK, "VM creation failed");
	ok &= check(poco_vm_register_standard_library(vm) == POCO_STATUS_OK,
				"standard library registration failed");
	compile_status =
		poco_vm_compile_buffer(vm, "portable-structs.poc", source, sizeof(source) - 1, &compiled);
	if (compile_status != POCO_STATUS_OK) {
		fprintf(stderr, "portable structs: compile status %d: %s\n", (int)compile_status,
				poco_get_last_error(vm));
	}
	ok &= check(compile_status == POCO_STATUS_OK, "compilation failed");
	if (compiled == NULL) {
		goto CLEANUP;
	}
	ok &= check(po_program_image_encode(compiled, POCO_DEBUG_LEVEL_MINIMAL, &image, &image_size) ==
					PO_PROGRAM_IMAGE_OK,
				"image encoding failed");
	if (image == NULL) {
		goto CLEANUP;
	}
	struct_offset = read_u64(image + 144);
	struct_size = read_u64(image + 152);
	ok &= check(struct_offset <= image_size && struct_size <= image_size - struct_offset,
				"struct section is outside the image");
	ok &= check(read_u32(image + struct_offset) >= 2, "struct definitions were not serialized");
	ok &= check(locate_first_member_component(image + struct_offset, (size_t)struct_size,
											  &component_offset),
				"abstract member schema could not be parsed");
	ok &= check(po_program_image_decode(vm, image, image_size, POCO_DEBUG_LEVEL_MINIMAL, NULL,
										&decoded) == PO_PROGRAM_IMAGE_OK,
				"image decoding failed");
	if (decoded != NULL) {
		ok &= check(same_struct(find_struct(compiled, "Inner"), find_struct(decoded, "Inner")),
					"Inner layout was not recomputed identically");
		ok &= check(same_struct(find_struct(compiled, "Outer"), find_struct(decoded, "Outer")),
					"Outer layout was not recomputed identically");
		ok &= check(poco_vm_run(vm, decoded, NULL, &result) == POCO_STATUS_OK && result == 17,
					"decoded struct program did not run correctly");
	}
	malformed = malloc(image_size);
	ok &= check(malformed != NULL, "malformed-image allocation failed");
	if (malformed != NULL && component_offset <= struct_size - 4) {
		memcpy(malformed, image, image_size);
		write_u32(malformed + struct_offset + component_offset, UINT32_MAX);
		ok &= check(po_program_image_decode(vm, malformed, image_size, POCO_DEBUG_LEVEL_MINIMAL,
											NULL, &rejected) == PO_PROGRAM_IMAGE_MALFORMED,
					"unknown member type was not rejected as a hard load error");
		ok &= check(rejected == NULL, "rejected image returned a program");
	}

CLEANUP:
	poco_program_destroy(rejected);
	poco_program_destroy(decoded);
	poco_program_destroy(compiled);
	poco_vm_destroy(vm);
	free(malformed);
	free(image);
	return ok ? 0 : 1;
}
